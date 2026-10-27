#!/usr/bin/env python3
"""Strict offline parser for Nopen v0.2 OpenCL bandwidth matrices.

Only a complete, verified nine-run OpenCL matrix can produce a result table.
No networking or GPU required; this script never invokes the benchmark.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

EXPECTED = {
    ("queued", 4), ("queued", 16), ("queued", 64),
    ("queued", 256), ("queued", 1024), ("queued", 4096),
    ("blocking", 4), ("blocking", 256), ("blocking", 4096),
}
MIB_TO_GB = (1024 * 1024) / 1_000_000_000


def fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([A-Za-z0-9_]+)=([^\s]+)", line))


def analyze(raw: str) -> tuple[list[dict[str, object]], float | None]:
    current: tuple[str, int] | None = None
    result: dict[str, str] | None = None
    seen: dict[tuple[str, int], dict[str, object]] = {}
    complete = False
    link_gt: float | None = None
    width: int | None = None
    sha = False
    for line in raw.splitlines():
        if line.startswith("git_revision="):
            if not re.fullmatch(r"git_revision=[0-9a-f]{40}", line):
                raise ValueError("Invalid source revision")
            sha = True
        if line.startswith("current_link_speed="):
            m = re.fullmatch(r"current_link_speed=([0-9.]+) GT/s PCIe", line)
            if m:
                link_gt = float(m.group(1))
        if line.startswith("current_link_width="):
            m = re.fullmatch(r"current_link_width=(\d+)", line)
            if m:
                width = int(m.group(1))
        if line.startswith("MATRIX_RUN "):
            if current is not None:
                raise ValueError("Previous run missing validation")
            kv = fields(line)
            try:
                current = (kv["mode"], int(kv["chunk_kib"]))
            except (KeyError, ValueError) as exc:
                raise ValueError("Malformed matrix run") from exc
            if current not in EXPECTED or current in seen:
                raise ValueError(f"Unexpected or duplicate matrix run {current}")
            result = None
        elif line.startswith("LINK_RESULT "):
            if current is None or result is not None:
                raise ValueError("Orphan or duplicate LINK_RESULT")
            kv = fields(line)
            if (kv.get("version") != "0.2.0-dev"
                or kv.get("backend") != "opencl"
                or kv.get("no_global_swap") != "YES"
                or kv.get("mode") != current[0]
                or kv.get("pool_mib") != "64"
                or kv.get("chunk_kib") != str(current[1])
                or kv.get("passes") != "3"):
                raise ValueError("Result metadata does not match the planned run")
            try:
                if float(kv["h2d_mib_s"]) <= 0 or float(kv["d2h_mib_s"]) <= 0:
                    raise ValueError("Non-positive throughput")
            except (KeyError, ValueError) as exc:
                raise ValueError("Invalid throughput fields") from exc
            result = kv
        elif line.startswith("BENCH_PASS "):
            if current is None or result is None:
                raise ValueError("Orphan BENCH_PASS or missing LINK_RESULT")
            kv = fields(line)
            if (kv.get("backend") != "opencl"
                or kv.get("verified_bytes") != str(64 * 1024 * 1024)
                or kv.get("passes") != "3"
                or kv.get("no_global_swap") != "YES"):
                raise ValueError("Incomplete or unexpected BENCH_PASS")
            seen[current] = {
                "mode": current[0],
                "chunk_kib": current[1],
                "h2d_gb_s": float(result["h2d_mib_s"]) * MIB_TO_GB,
                "d2h_gb_s": float(result["d2h_mib_s"]) * MIB_TO_GB,
                "peak_rss_mib": int(result["process_maxrss_kib"]) / 1024,
            }
            current, result = None, None
        elif line.startswith("MATRIX_PASS "):
            if current is not None or fields(line).get("all_opencl_runs_complete") != "YES":
                raise ValueError("Invalid MATRIX_PASS")
            complete = True
    if not complete or current or not sha or set(seen) != EXPECTED:
        raise ValueError(f"Incomplete matrix: {len(seen)}/{len(EXPECTED)} runs")
    # PCIe 3.0+ encodes 128b/130b; theoretical one-way bytes/s
    ceiling = None
    if link_gt in (8.0, 16.0, 32.0, 64.0) and width is not None and width > 0:
        ceiling = link_gt * 1_000_000_000 * (128.0 / 130.0) / 8 * width / 1e9
    return [seen[key] for key in sorted(EXPECTED, key=lambda k: (k[0] != "queued", k[1]))], ceiling


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path, help="The complete bench_matrix.sh log")
    args = ap.parse_args()
    try:
        rows, ceiling = analyze(args.log.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        print(f"MATRIX_INVALID: {exc}", file=sys.stderr)
        return 2
    print("MODE      CHUNK(KiB)   H2D(GB/s)   D2H(GB/s)   RSS(MiB)")
    for row in rows:
        print(f"{row['mode']:<9} {row['chunk_kib']:>10}   "
              f"{row['h2d_gb_s']:>9.2f}   {row['d2h_gb_s']:>9.2f}   "
              f"{row['peak_rss_mib']:>8.1f}")
    best_write = max(rows, key=lambda row: row["h2d_gb_s"])
    best_read = max(rows, key=lambda row: row["d2h_gb_s"])
    print(f"BEST_H2D gb_s={best_write['h2d_gb_s']:.3f} "
          f"chunk_kib={best_write['chunk_kib']} mode={best_write['mode']}")
    print(f"BEST_D2H gb_s={best_read['d2h_gb_s']:.3f} "
          f"chunk_kib={best_read['chunk_kib']} mode={best_read['mode']}")
    if ceiling is not None:
        print(f"PCIE_ENCODING_CEILING single_direction_gb_s={ceiling:.3f} "
              f"best_h2d_percent={best_write['h2d_gb_s'] / ceiling * 100:.1f} "
              f"best_d2h_percent={best_read['d2h_gb_s'] / ceiling * 100:.1f} "
              "excludes_protocol_overhead=YES")
    print("MATRIX_VERIFIED complete_runs=9 no_global_swap=YES not_fault_latency=YES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
