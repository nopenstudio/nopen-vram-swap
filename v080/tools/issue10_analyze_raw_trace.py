#!/usr/bin/env python3
"""Read-only Issue #10 raw ublk/IPC/GPU trace consistency analyzer.

Consumes the *entire* service log, or a clipped grep excerpt. Never launches
ublk, enables SWAP, allocates VRAM, or modifies the input.
"""
from __future__ import annotations
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

EVENT = re.compile(r"NOPEN_DIAG_(UBLK|IPC|WORKER)\s+phase=(\w+)")
FIELD = re.compile(r"(?:^|\s)([a-z_][a-z0-9_]*)=([^\s]+)")
REQUIRED = (
    ("UBLK", "BEGIN"), ("IPC", "BEGIN"), ("WORKER", "BEGIN"),
    ("WORKER", "GPU_ENTER"), ("WORKER", "GPU_RETURN"),
    ("WORKER", "RESPONSE_SENT"), ("IPC", "END"), ("UBLK", "END"),
)


def analyze(lines: list[str], full: bool) -> dict:
    phases = Counter()
    ops = Counter()
    bytes_by_op = Counter()
    failures: list[str] = []
    incomplete: list[str] = []
    events = 0
    completed = 0
    first_time: int | None = None
    last_time: int | None = None
    current: list[tuple[str, str, dict]] = []
    completed_paths: list[tuple[str, int, int]] = []
    for i, raw in enumerate(lines, 1):
        found = EVENT.search(raw)
        if not found:
            # Non-trace lines in systemd's service log are intentionally ignored.
            continue
        layer, phase = found.groups()
        events += 1
        # Existing C logging can concatenate "bytes=4096status=0" and
        # "rc=0elapsed_ms=0". Restore lexical delimiters before field parsing.
        line = re.sub(r"(?<=\d)(?=(?:status|elapsed_ms)=)", " ", raw)
        fields = dict(FIELD.findall(line))
        phases[f"{layer}:{phase}"] += 1
        clock = fields.get("t_ms")
        if clock and clock.isdecimal():
            t = int(clock)
            first_time = t if first_time is None else min(first_time, t)
            last_time = t if last_time is None else max(last_time, t)
        code = fields.get("rc", fields.get("status"))
        if code is not None and code.lstrip("-").isdecimal() and int(code) != 0:
            failures.append(f"line={i} {layer}:{phase} error={code}")
        if layer == "UBLK" and phase == "BEGIN":
            if current:
                incomplete.append(f"line={i} new UBLK BEGIN while previous path open")
            current = []
        current.append((layer, phase, fields))
        if layer == "UBLK" and phase == "END":
            completed += 1
            actual = tuple((l, p) for l, p, _ in current)
            if actual != REQUIRED:
                incomplete.append(f"line={i} path has {actual!r}")
            begins = [f for l, p, f in current if l == "UBLK" and p == "BEGIN"]
            if begins:
                first = begins[0]
                raw_op = first.get("op", "?")
                op = "READ" if raw_op == "0" else "WRITE" if raw_op == "1" else raw_op
                try:
                    offset = int(first["offset"])
                    size = int(first["bytes"])
                    last = fields
                    if (last.get("op") != raw_op
                            or last.get("offset") != str(offset)
                            or last.get("bytes") != str(size)):
                        failures.append(f"line={i} begin/end mismatch")
                    for l, p, f in current:
                        if l in ("IPC", "WORKER") and f.get("op") not in (op,):
                            failures.append(f"line={i} op mismatch stage={l}:{p}")
                        if (f.get("offset") not in (str(offset), None)
                                or f.get("bytes") not in (str(size), None)):
                            failures.append(f"line={i} offset/size mismatch stage={l}:{p}")
                    ops[op] += 1
                    bytes_by_op[op] += size
                    completed_paths.append((op, offset, size))
                except (KeyError, ValueError):
                    failures.append(f"line={i} invalid ublk offset/size/op")
            current = []
    if current:
        incomplete.append("trace ends with a partial path")
    if not events:
        failures.append("no NOPEN_DIAG events found")
    # A command-output excerpt may begin halfway through an existing request.
    # That is not proof of a device error; only a full original service log
    # can be used for conclusive completeness assertions.
    if full and incomplete:
        failures.append(f"{len(incomplete)} incomplete trace paths in full log")
    # The script writes three pages, so this also helps detect a truncated
    # excerpt. Additional automatic metadata reads are expected from udev.
    if full and ops["WRITE"] < 3:
        failures.append("fewer than three 4KiB raw writes in full service log")
    status = ("FAIL" if failures else
              "EXCERPT_CLEAN_NOT_COMPLETE" if incomplete or not full else
              "FULL_TRACE_PASS")
    return {
        "schema": "NOPEN_ISSUE10_UBLK_RAW_TRACE",
        "status": status,
        "full_service_log": full,
        "trace_events": events,
        "ublk_completed": completed,
        "complete_eight_stage_paths": len(completed_paths),
        "operation_counts": dict(ops),
        "bytes_by_operation": dict(bytes_by_op),
        "phase_counts": dict(sorted(phases.items())),
        "t_ms_span_observed": None if first_time is None else last_time - first_time,
        "timestamps_are_millisecond_resolution": True,
        "incomplete_paths": incomplete[:10],
        "errors": failures[:10],
        "conclusions": [
            "This is raw block IO, not kernel SWAP or reclaim",
            "An excerpt cannot prove how many I/O requests happened in total",
            "0ms timestamps are below 1ms resolution, not zero latency",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path,
                        help="original service log or pasted grep excerpt")
    parser.add_argument("--full-service-log", action="store_true",
                        help="assert all phases and three writes are present")
    args = parser.parse_args()
    try:
        raw = args.trace.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"ISSUE10_TRACE_READ_FAILED {e}", file=sys.stderr)
        return 2
    result = analyze(raw.splitlines(), args.full_service_log)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    print(f"ISSUE10_TRACE_{result['status']} kernel_swap=NOT_TESTED "
          f"device_created=NO files_modified=NO")
    return 0 if result["status"] != "FAIL" else 2


if __name__ == "__main__":
    raise SystemExit(main())
