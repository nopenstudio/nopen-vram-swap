#!/usr/bin/env python3
"""Nopen v0.8.5 Reserved VRAM: READ-ONLY preflight and capacity accounting.

This does not allocate/pin GPU VRAM, configure amdgpu, start OpenCL, issue
block I/O, touch zram, or enable swap. It only evaluates whether a future,
explicit, userspace-only reservation experiment could be *considered*.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

MIB = 1024 * 1024
VRAM_FIELDS = (
    "mem_info_vram_total",
    "mem_info_vram_used",
    "mem_info_gtt_used",
)
SUPPORTED_POOL_MIB = (64, 128, 256, 512, 1024, 2048, 4096)


def read_counter(device: Path, name: str, required: bool = True) -> int | None:
    try:
        value = int((device / name).read_text(encoding="ascii").strip(), 10)
        if value < 0:
            raise ValueError("negative sysfs value")
        return value
    except (OSError, UnicodeError, ValueError) as exc:
        if required:
            raise ValueError(f"{device.name}/{name}: {exc}") from exc
        return None


def gpu_devices(pci_root: Path) -> list[Path]:
    try:
        entries = sorted(pci_root.iterdir())
    except OSError as exc:
        raise ValueError(f"PCI sysfs is unavailable: {exc}") from exc
    matches = []
    for path in entries:
        try:
            vendor = int((path / "vendor").read_text().strip(), 16)
            class_code = int((path / "class").read_text().strip(), 16)
        except (ValueError, OSError, UnicodeError):
            continue
        if vendor == 0x1002 and (class_code >> 16) == 0x03:
            matches.append(path)
    return matches


def read_mem_available(meminfo: Path) -> int:
    try:
        rows = meminfo.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ValueError(f"Cannot read MemAvailable: {exc}") from exc
    for row in rows:
        fields = row.split()
        if len(fields) == 3 and fields[0] == "MemAvailable:" and fields[2] == "kB":
            try:
                amount = int(fields[1])
                if amount >= 0:
                    return amount * 1024
            except ValueError:
                pass
    raise ValueError("MemAvailable not present or invalid")


def read_swaps(swaps_file: Path) -> list[str]:
    try:
        lines = swaps_file.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise ValueError(f"Cannot inspect active swaps: {exc}") from exc
    if not lines or not lines[0].startswith("Filename"):
        raise ValueError("Unexpected /proc/swaps format")
    names = []
    for row in lines[1:]:
        fields = row.split()
        if len(fields) < 5:
            raise ValueError("Malformed /proc/swaps entry")
        names.append(fields[0])
    return names


def evaluate(pci_root: Path, meminfo: Path, swaps_file: Path,
             pool_mib: int = 4096, min_free_mib: int = 4096,
             min_mem_available_mib: int = 1024,
             pci: str | None = None) -> dict:
    reasons = []
    selected = None
    snap: dict = {}
    try:
        candidates = gpu_devices(pci_root)
        if pci is not None:
            candidates = [d for d in candidates if d.name == pci]
        if len(candidates) != 1:
            raise ValueError("Select exactly one AMD graphics PCI device; "
                             f"found {len(candidates)} candidates")
        selected = candidates[0].name
        counters = {key: read_counter(candidates[0], key)
                    for key in VRAM_FIELDS}
        total = counters["mem_info_vram_total"]
        used = counters["mem_info_vram_used"]
        if total <= 0 or used > total:
            raise ValueError("Invalid VRAM counters")
        snap = {
            "gpu_pci": selected,
            "vram_total_mib": total // MIB,
            "vram_used_mib": (used + MIB - 1) // MIB,
            "gtt_used_mib": counters["mem_info_gtt_used"] // MIB,
            "hypothetical_after_pool_mib": max(0, (total - pool_mib * MIB) // MIB),
            "free_after_pool_mib": (
                (total - used - pool_mib * MIB) // MIB
                if total >= used + pool_mib * MIB else -1
            ),
        }
        if pool_mib not in SUPPORTED_POOL_MIB:
            reasons.append("UNSUPPORTED_POOL_SIZE")
        if used + (pool_mib + min_free_mib) * MIB > total:
            reasons.append("INSUFFICIENT_VRAM_HEADROOM")
        if pool_mib * MIB > total * 3 // 4:
            reasons.append("POOL_EXCEEDS_75_PERCENT")
    except ValueError as exc:
        reasons.append("VRAM_PROBE_FAILED")
        snap["probe_error"] = str(exc)
    try:
        available = read_mem_available(meminfo)
        snap["host_mem_available_mib"] = available // MIB
        if available < min_mem_available_mib * MIB:
            reasons.append("HOST_RAM_HEADROOM_TOO_LOW")
    except ValueError as exc:
        reasons.append("HOST_RAM_PROBE_FAILED")
        snap["host_memory_error"] = str(exc)
    try:
        swaps = read_swaps(swaps_file)
        snap["active_swap_devices"] = swaps
        if any(name.startswith(("/dev/ublk", "/dev/nbd"))
               for name in swaps):
            reasons.append("GPU_OR_NBD_KERNEL_SWAP_ALREADY_ACTIVE")
    except ValueError as exc:
        reasons.append("SWAP_PROBE_FAILED")
        snap["swap_probe_error"] = str(exc)
    if min_free_mib < 4096 or min_mem_available_mib < 1024:
        reasons.append("HEADROOM_BELOW_V085_POLICY_MINIMUM")
    passed = not reasons
    return {
        "schema": "NOPEN_V085_RESERVED_VRAM_PREFLIGHT",
        "requested_pool_mib": pool_mib,
        "min_graphics_free_mib": min_free_mib,
        "min_host_mem_available_mib": min_mem_available_mib,
        **snap,
        "status": ("USERSPACE_EXPERIMENT_ELIGIBLE" if passed else "REFUSED"),
        "reasons": reasons,
        "application_vram_capacity_modified": False,
        "driver_vram_partition_created": False,
        "physical_vram_residency_guaranteed": False,
        "gpu_memory_allocated": False,
        "kernel_swap_authorized": False,
        "system_swap_modified": False,
        "safety_gate": "KERNEL_SWAP_BLOCKED",
        "interpretation": (
            "Numbers are current AMDGPU sysfs accounting, not an exclusive "
            "partition, physical pinning guarantee or a new Vulkan/OpenCL "
            "device capacity visible to games."
        ),
    }


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pci-root", type=Path,
                   default=Path("/sys/bus/pci/devices"))
    p.add_argument("--meminfo", type=Path, default=Path("/proc/meminfo"))
    p.add_argument("--swaps-file", type=Path, default=Path("/proc/swaps"))
    p.add_argument("--pci", help="Explicit GPU PCI address if more than one AMD GPU")
    p.add_argument("--pool-mib", type=int, default=4096)
    p.add_argument("--min-free-mib", type=int, default=4096)
    p.add_argument("--min-mem-available-mib", type=int, default=1024)
    args = p.parse_args()
    result = evaluate(args.pci_root, args.meminfo, args.swaps_file,
                      args.pool_mib, args.min_free_mib,
                      args.min_mem_available_mib, args.pci)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    marker = "V085_RESERVED_PREFLIGHT_PASS" if not result["reasons"] else "V085_RESERVED_PREFLIGHT_REFUSED"
    print(f"{marker} pool_mib={args.pool_mib} "
          f"gpu_pinned=NO app_vram_modified=NO kernel_swap=BLOCKED "
          f"reasons={len(result['reasons'])}")
    return 0 if not result["reasons"] else 2


if __name__ == "__main__":
    sys.exit(main())
