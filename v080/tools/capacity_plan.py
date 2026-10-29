#!/usr/bin/env python3
"""Nopen v0.8 alpha.1: read-only capacity feasibility; NEVER activates swap.

Stage plans do not authorize physical Linux kernel swap. A passing result
only shows budgets/offsets are plausible at the CURRENT memory snapshot.
Actual GPU memory residency, AMDGPU stability and usable performance
require separate experimental evidence.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys

STAGES_MIB = (1024, 2048, 4096)
MIB = 1024 * 1024
MIN_OS_MEM_AVAILABLE_MIB = 1024
DISK_MARGIN_MIB = 512
MIN_VRAM_HEADROOM_MIB = 4096
GPU_MAX_FRACTION_NUM = 3
GPU_MAX_FRACTION_DEN = 4
PAGE_SIZE = 4096

def _read_uint(path: Path) -> int:
    text = path.read_text(encoding="ascii").strip()
    if not text or not text.isdecimal():
        raise ValueError(f"invalid unsigned sysfs counter: {path}")
    return int(text)

def amd_discrete_vram(sysfs: Path = Path("/sys/bus/pci/devices")) -> tuple[int, int]:
    candidates = []
    for d in sysfs.iterdir():
        try:
            v = (d / "vendor").read_text().strip().lower()
            cls = (d / "class").read_text().strip().lower()
            if v != "0x1002" or not cls.startswith("0x03"):
                continue
            total = _read_uint(d / "mem_info_vram_total")
            used = _read_uint(d / "mem_info_vram_used")
            if used > total or total == 0:
                raise ValueError("inconsistent AMD VRAM counters")
            candidates.append((total // MIB, (used + MIB - 1) // MIB))
        except (FileNotFoundError, PermissionError):
            continue
    if len(candidates) != 1:
        raise ValueError("exactly one AMD GPU with VRAM counters is required")
    return candidates[0]

def memory_available_mib(path: Path = Path("/proc/meminfo")) -> int:
    for row in path.read_text(encoding="ascii").splitlines():
        if row.startswith("MemAvailable:"):
            words = row.split()
            if len(words) < 2 or not words[1].isdecimal():
                break
            return int(words[1]) // 1024
    raise ValueError("MemAvailable not present")

def gpu_kernel_swap_active(path: Path = Path("/proc/swaps")) -> bool:
    rows = path.read_text(encoding="ascii").splitlines()
    if not rows or not rows[0].startswith("Filename"):
        raise ValueError("cannot validate /proc/swaps")
    for line in rows[1:]:
        fields = line.split()
        if fields and (fields[0].startswith("/dev/ublkb")
                       or fields[0].startswith("/dev/nbd")):
            return True
    return False

def free_disk_mib(path: Path) -> int:
    import os
    st = os.statvfs(path)
    return st.f_bavail * st.f_frsize // MIB

def evaluate(capacity_mib: int, total_vram_mib: int, used_vram_mib: int,
             disk_free_mib: int, mem_available_mib: int) -> dict:
    values = (capacity_mib, total_vram_mib, used_vram_mib,
              disk_free_mib, mem_available_mib)
    if any(type(x) is not int or x < 0 for x in values):
        raise ValueError("all inputs must be nonnegative integers")
    if capacity_mib not in STAGES_MIB:
        raise ValueError("only 1024, 2048, 4096 MiB offline stages are allowed")
    if used_vram_mib > total_vram_mib:
        raise ValueError("VRAM used exceeds total")
    pages = capacity_mib * MIB // PAGE_SIZE
    # Existing Nopen shadow uint64 checksums + GPU blk_meta.
    # blk_meta = uint64 checksum + uint8 state, typically padded to 16;
    # 24 bytes/page is a conservative known-metadata estimate.
    metadata_mib = (pages * 24 + MIB - 1) // MIB
    projected_used = used_vram_mib + capacity_mib
    headroom = max(MIN_VRAM_HEADROOM_MIB,
                   (total_vram_mib + 3) // 4)
    gates = {
        "gpu_le_75pct_total": capacity_mib * GPU_MAX_FRACTION_DEN <= total_vram_mib * GPU_MAX_FRACTION_NUM,
        "gpu_headroom_after_reservation": projected_used + headroom <= total_vram_mib,
        "ssd_shadow_plus_margin": disk_free_mib >= capacity_mib + DISK_MARGIN_MIB,
        "os_mem_available": mem_available_mib >= MIN_OS_MEM_AVAILABLE_MIB,
        "64bit_offsets": capacity_mib * MIB <= (1 << 63) - 1,
    }
    return {
        "schema": "NOPEN_V080_ALPHA1_CAPACITY_PLAN",
        "stage_mib": capacity_mib,
        "logical_pages": pages,
        "highest_page_byte_offset": (pages - 1) * PAGE_SIZE,
        "vram_total_mib": total_vram_mib,
        "vram_used_mib": used_vram_mib,
        "vram_headroom_reserved_mib": headroom,
        "vram_projected_used_mib": projected_used,
        "ssd_free_mib": disk_free_mib,
        "ssd_required_mib": capacity_mib + DISK_MARGIN_MIB,
        "mem_available_mib": mem_available_mib,
        "known_metadata_estimate_mib": metadata_mib,
        "gates": gates,
        "feasible_now": all(gates.values()),
        "actual_gpu_allocated": False,
        "kernel_swap_activated_by_planner": False,
        "performance_gain_proven": False,
        "long_term_stability_proven": False,
    }

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--stage-mib", type=int, choices=STAGES_MIB, required=True)
    ap.add_argument("--vram-total-mib", type=int)
    ap.add_argument("--vram-used-mib", type=int)
    ap.add_argument("--disk-free-mib", type=int)
    ap.add_argument("--mem-available-mib", type=int)
    ap.add_argument("--shadow-directory", type=Path, default=Path.home())
    ns = ap.parse_args()
    try:
        if gpu_kernel_swap_active():
            raise ValueError("kernel GPU/NBD swap is active; stop previous experiment first")
        if (ns.vram_total_mib is None) != (ns.vram_used_mib is None):
            raise ValueError("override both VRAM counters together")
        total,used = (amd_discrete_vram() if ns.vram_total_mib is None
                      else (ns.vram_total_mib, ns.vram_used_mib))
        disk = (free_disk_mib(ns.shadow_directory) if ns.disk_free_mib is None
                else ns.disk_free_mib)
        mem = (memory_available_mib() if ns.mem_available_mib is None
               else ns.mem_available_mib)
        report = evaluate(ns.stage_mib,total,used,disk,mem)
    except (ValueError, OSError) as exc:
        print(f"V080_PLAN_REFUSED {exc}", file=sys.stderr)
        return 2
    print(json.dumps(report,ensure_ascii=False,indent=2))
    print("V080_PLAN_" + ("PASS" if report["feasible_now"] else "BLOCKED") +
          " stage_mib="+str(ns.stage_mib)+" kernel_swap=NO")
    return 0 if report["feasible_now"] else 3

if __name__ == "__main__":
    raise SystemExit(main())
