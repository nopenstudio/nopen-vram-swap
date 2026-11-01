#!/usr/bin/env python3
"""v0.8.5 read-only AMDGPU VRAM/GTT observation.

Observes OS-wide counters while a separately launched, explicit GPU-only
benchmark/lease is running. Does NOT attribute changes to Nopen, prove
GPU page pinning, allocate VRAM or activate kernel SWAP.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time
from reserved_vram_preflight import (
    MIB, gpu_devices, read_counter, read_mem_available, read_swaps,
)


def select_gpu(root: Path, pci: str | None) -> Path:
    devices = gpu_devices(root)
    if pci is not None:
        devices = [d for d in devices if d.name == pci]
    if len(devices) != 1:
        raise ValueError("Need exactly one AMD GPU; specify --pci when ambiguous")
    return devices[0]


def snapshot(gpu: Path, meminfo: Path, swaps_file: Path) -> dict:
    active_swaps = read_swaps(swaps_file)
    if any(name.startswith(("/dev/ublk", "/dev/nbd"))
           for name in active_swaps):
        raise ValueError("Physical ublk/NBD swap active: observation refused")
    total = read_counter(gpu, "mem_info_vram_total")
    vram = read_counter(gpu, "mem_info_vram_used")
    gtt = read_counter(gpu, "mem_info_gtt_used")
    available = read_mem_available(meminfo)
    if total <= 0 or vram > total:
        raise ValueError("Invalid VRAM counters")
    return {
        "pci": gpu.name,
        "vram_total_bytes": total,
        "vram_used_bytes": vram,
        "gtt_used_bytes": gtt,
        "host_mem_available_bytes": available,
        "swap_devices": active_swaps,
    }


def observe(gpu: Path, meminfo: Path, swaps_file: Path,
            samples: int, interval_seconds: float,
            sleep=time.sleep) -> list[dict]:
    if not 1 <= samples <= 600 or not 0 <= interval_seconds <= 60:
        raise ValueError("Invalid sampling bounds")
    records = []
    first = None
    for i in range(samples):
        measurement = snapshot(gpu, meminfo, swaps_file)
        if first is None:
            first = measurement
        if measurement["vram_total_bytes"] != first["vram_total_bytes"]:
            raise ValueError("VRAM capacity changed during observation")
        report = {
            "schema": "NOPEN_V085_RESERVED_VRAM_OBSERVATION",
            "sample": i,
            "pci": measurement["pci"],
            "vram_used_mib": round(measurement["vram_used_bytes"] / MIB, 3),
            "gtt_used_mib": round(measurement["gtt_used_bytes"] / MIB, 3),
            "host_mem_available_mib": round(measurement["host_mem_available_bytes"] / MIB, 3),
            "vram_used_delta_mib": round(
                (measurement["vram_used_bytes"] - first["vram_used_bytes"]) / MIB, 3),
            "gtt_used_delta_mib": round(
                (measurement["gtt_used_bytes"] - first["gtt_used_bytes"]) / MIB, 3),
            "host_mem_available_delta_mib": round(
                (measurement["host_mem_available_bytes"] -
                 first["host_mem_available_bytes"]) / MIB, 3),
            "changes_attributable_to_nopen": False,
            "physical_residency_or_pin_proven": False,
            "live_swap_tested": False,
            "kernel_swap_authorized": False,
            "safety_gate": "KERNEL_SWAP_BLOCKED",
        }
        records.append(report)
        if i + 1 < samples:
            sleep(interval_seconds)
    return records


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pci-root", type=Path, default=Path("/sys/bus/pci/devices"))
    p.add_argument("--meminfo", type=Path, default=Path("/proc/meminfo"))
    p.add_argument("--swaps-file", type=Path, default=Path("/proc/swaps"))
    p.add_argument("--pci")
    p.add_argument("--samples", type=int, default=5)
    p.add_argument("--interval-seconds", type=float, default=1.0)
    args = p.parse_args()
    try:
        gpu = select_gpu(args.pci_root, args.pci)
        for record in observe(gpu,args.meminfo,args.swaps_file,
                              args.samples,args.interval_seconds):
            print(json.dumps(record, ensure_ascii=False))
    except (OSError,ValueError) as exc:
        print(f"V085_RESERVED_OBSERVE_REFUSED {exc}",file=sys.stderr)
        return 2
    print("V085_RESERVED_OBSERVE_PASS read_only=YES"
          " changes_attribution=UNPROVEN gpu_pin=UNPROVEN kernel_swap=BLOCKED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
