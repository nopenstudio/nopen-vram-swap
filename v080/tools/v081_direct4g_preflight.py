#!/usr/bin/env python3
"""Read-only preflight for Nopen's EXPERIMENTAL 4GiB direct-VRAM kernel swap.
No disk/partition/zram edits. This cannot prove reclaim safety.
"""
from __future__ import annotations
import json
from pathlib import Path
import sys

MIB = 1024 * 1024

def num(path: Path) -> int:
    value = path.read_text().strip()
    if not value.isdecimal():
        raise ValueError(f"bad numeric sysfs value: {path}")
    return int(value)

def main() -> int:
    try:
        swaps = Path("/proc/swaps").read_text().splitlines()
        if not swaps or not swaps[0].startswith("Filename"):
            raise ValueError("invalid /proc/swaps")
        for s in swaps[1:]:
            if s.split() and s.split()[0].startswith(("/dev/ublkb", "/dev/nbd")):
                raise ValueError("another Nopen / NBD kernel SWAP is active")
        targets = []
        for dev in Path("/sys/bus/pci/devices").iterdir():
            try:
                if (dev / "vendor").read_text().strip().lower() != "0x1002":
                    continue
                if not (dev / "class").read_text().strip().lower().startswith("0x03"):
                    continue
                total = num(dev / "mem_info_vram_total") // MIB
                used = (num(dev / "mem_info_vram_used") + MIB - 1) // MIB
                try:
                    gtt_used = (num(dev / "mem_info_gtt_used") + MIB - 1) // MIB
                except (ValueError, OSError):
                    gtt_used = None
                targets.append(dict(pci=dev.name, vram_total_mib=total,
                                    vram_used_mib=used, gtt_used_mib=gtt_used))
            except (OSError, ValueError):
                continue
        if len(targets) != 1:
            raise ValueError("expected precisely one discrete AMD GPU with VRAM counters")
        gpu = targets[0]
        headroom = max(4096, (gpu["vram_total_mib"] + 3)//4)
        remaining = gpu["vram_total_mib"] - gpu["vram_used_mib"] - 4096
        if remaining < headroom:
            raise ValueError(f"4GiB reservation would leave only {remaining}MiB"
                             f" GPU VRAM free; need {headroom}MiB headroom")
        mem_available = None
        for line in Path("/proc/meminfo").read_text().splitlines():
            if line.startswith("MemAvailable:"):
                mem_available = int(line.split()[1]) // 1024
                break
        if mem_available is None or mem_available < 2048:
            raise ValueError(f"minimum 2048MiB MemAvailable required before starting,"
                             f" observed={mem_available}")
        result = dict(schema="NOPEN_V081_DIRECT4G_PREFLIGHT",
                      gpu=gpu, mem_available_mib=mem_available,
                      capacity_mib=4096, vram_headroom_after_mib=remaining,
                      kernel_swap_activated=False, disks_modified=False,
                      zram_modified=False, reclaim_forward_progress_proven=False)
    except (OSError, ValueError, IndexError) as e:
        print("V081_DIRECT4G_PREFLIGHT_REFUSED "+str(e),file=sys.stderr)
        return 2
    print(json.dumps(result,ensure_ascii=False,indent=2))
    print("V081_DIRECT4G_PREFLIGHT_PASS kernel_swap=NO ssd_shadow=NO")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
