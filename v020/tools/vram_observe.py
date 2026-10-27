#!/usr/bin/env python3
"""Read-only, process-isolated AMDGPU VRAM/GTT and RSS telemetry during a GPU benchmark.

Device-wide used-memory counters are corroborating evidence, not per-buffer
physical-placement proof. Does not require root, swapon, NBD or kernel tweaks.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

MIB = 1024 * 1024
FIELDS = ("mem_info_vram_used", "mem_info_gtt_used")
TOTAL_FIELDS = ("mem_info_vram_total", "mem_info_gtt_total")


def discover_gpu(pci_root: Path) -> Path:
    candidates = []
    for device in sorted(pci_root.glob("*")):
        try:
            vendor = (device / "vendor").read_text(encoding="ascii").strip().lower()
            display_class = (device / "class").read_text(encoding="ascii").strip().lower()
            if vendor == "0x1002" and display_class.startswith("0x03"):
                candidates.append(device)
        except (OSError, UnicodeError):
            continue
    if len(candidates) != 1:
        raise ValueError(
            f"Expected exactly one AMD PCI display GPU, found {len(candidates)}; "
            "the memory counters cannot safely be attributed to a selected device"
        )
    return candidates[0]


def snapshot(device: Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for name in (*FIELDS, *TOTAL_FIELDS):
        try:
            values[name] = int((device / name).read_text(encoding="ascii").strip())
        except (OSError, UnicodeError, ValueError) as error:
            raise ValueError(f"AMDGPU counter {name} unavailable: {error}") from error
        if values[name] < 0:
            raise ValueError(f"Negative AMDGPU counter: {name}")
    return values


def rss_kib(pid: int) -> int | None:
    try:
        status = Path(f"/proc/{pid}/status").read_text(encoding="ascii")
        for line in status.splitlines():
            if line.startswith("VmRSS:"):
                return int(line.split()[1])
    except (OSError, ValueError, IndexError):
        pass
    return None


def active_nbd_swap() -> bool:
    try:
        lines = Path("/proc/swaps").read_text(encoding="ascii").splitlines()[1:]
    except (OSError, UnicodeError):
        raise ValueError("Unable to check active swap devices")
    return any(line.split() and line.split()[0].startswith("/dev/nbd") for line in lines)


def fmt_mib(value: int) -> str:
    return f"{value / MIB:.2f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build-v020/nopen-link-bench"))
    parser.add_argument("--pool-mib", type=int, choices=(64, 128, 256), default=64)
    parser.add_argument("--chunk-kib", type=int, choices=(4, 16, 64, 256, 1024, 4096), default=4096)
    parser.add_argument("--passes", type=int, choices=range(1, 11), default=5)
    parser.add_argument("--hold-ms", type=int, choices=range(250, 3001), default=2000)
    args = parser.parse_args()

    if args.chunk_kib > args.pool_mib * 1024:
        parser.error("Chunk cannot exceed GPU pool")
    if not args.binary.is_file():
        parser.error(f"Executable not found: {args.binary}")
    if hasattr(__import__("os"), "geteuid") and __import__("os").geteuid() == 0:
        print("REFUSED: never run hardware residency diagnostics as root", file=sys.stderr)
        return 2

    try:
        if active_nbd_swap():
            raise ValueError("Active NBD swap detected; benchmark refused")
        device = discover_gpu(Path("/sys/bus/pci/devices"))
        before = snapshot(device)
    except ValueError as error:
        print(f"VRAM_OBSERVE_REFUSED {error}", file=sys.stderr)
        return 2

    binary = args.binary.resolve()
    cmd = [str(binary), "--backend", "opencl", "--pool-mib", str(args.pool_mib),
           "--chunk-kib", str(args.chunk_kib), "--passes", str(args.passes),
           "--hold-ms", str(args.hold_ms)]
    print(f"VRAM_OBSERVE_START device={device.name} pool_mib={args.pool_mib} "
          f"chunk_kib={args.chunk_kib} hold_ms={args.hold_ms}", flush=True)
    print(f"VRAM_BEFORE vram_used_mib={fmt_mib(before[FIELDS[0]])} "
          f"gtt_used_mib={fmt_mib(before[FIELDS[1]])} "
          f"vram_total_mib={fmt_mib(before[TOTAL_FIELDS[0]])} "
          f"gtt_total_mib={fmt_mib(before[TOTAL_FIELDS[1]])}", flush=True)

    peak = dict(before)
    peak_rss = 0
    samples = 0
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as output:
        try:
            process = subprocess.Popen(cmd, stdout=output, stderr=subprocess.STDOUT)
        except OSError as error:
            print(f"VRAM_OBSERVE_FAILED start: {error}", file=sys.stderr)
            return 3
        try:
            while process.poll() is None:
                sample = snapshot(device)
                for key in FIELDS:
                    peak[key] = max(peak[key], sample[key])
                if (rss := rss_kib(process.pid)) is not None:
                    peak_rss = max(peak_rss, rss)
                samples += 1
                time.sleep(0.02)
            code = process.wait()
        except (ValueError, KeyboardInterrupt) as error:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            print(f"VRAM_OBSERVE_FAILED sampling: {error}", file=sys.stderr)
            return 4
        output.seek(0)
        log = output.read()
    print(log, end="" if log.endswith("\n") else "\n")
    try:
        after = snapshot(device)
    except ValueError as error:
        print(f"VRAM_OBSERVE_FAILED post: {error}", file=sys.stderr)
        return 5
    print(f"VRAM_PEAK delta_vram_mib={fmt_mib(peak[FIELDS[0]] - before[FIELDS[0]])} "
          f"delta_gtt_mib={fmt_mib(peak[FIELDS[1]] - before[FIELDS[1]])} "
          f"peak_child_rss_mib={peak_rss / 1024:.2f} samples={samples}")
    print(f"VRAM_AFTER delta_vram_mib={fmt_mib(after[FIELDS[0]] - before[FIELDS[0]])} "
          f"delta_gtt_mib={fmt_mib(after[FIELDS[1]] - before[FIELDS[1]])}")
    if code != 0 or "BENCH_PASS backend=opencl" not in log or "BENCH_READY phase=post_warmup backend=opencl" not in log:
        print(f"VRAM_OBSERVE_FAILED benchmark_exit={code} verified=NO", file=sys.stderr)
        return 6
    print("VRAM_OBSERVE_PASS scope=device_wide_counters "
          "per_buffer_residency_proven=NO external_gpu_activity_can_affect_delta=YES "
          "no_global_swap=YES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
