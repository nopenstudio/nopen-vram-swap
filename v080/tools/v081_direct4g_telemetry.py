#!/usr/bin/env python3
"""Read-only, bounded 1Hz physical GPU-only SWAP incident telemetry.
Logs basic /proc/swaps, VRAM/GTT, MemAvailable. Never edits host storage.
Does not claim to identify every GPU driver hang.
"""
from __future__ import annotations
import json
from pathlib import Path
import time
import sys
from datetime import datetime, timezone

MIB = 1024 * 1024

def mem() -> dict:
    out = {}
    try:
        for row in Path("/proc/meminfo").read_text().splitlines():
            fields = row.split()
            if fields and fields[0].rstrip(":") in ("MemAvailable","MemFree","SwapTotal","SwapFree"):
                out[fields[0].rstrip(":")+"_mib"] = round(int(fields[1])/1024,1)
    except (OSError,ValueError,IndexError):
        pass
    return out

def swaps() -> list:
    rows = []
    try:
        for line in Path("/proc/swaps").read_text().splitlines()[1:]:
            cells = line.split()
            if len(cells) >= 5:
                rows.append({"name":cells[0],"size_kib":int(cells[2]),
                             "used_kib":int(cells[3]),"priority":int(cells[4])})
    except (OSError,ValueError,IndexError):
        pass
    return rows

def gpu() -> list:
    entries=[]
    try:
        for d in Path("/sys/bus/pci/devices").iterdir():
            try:
                if (d/"vendor").read_text().strip().lower() != "0x1002":
                    continue
                if not (d/"class").read_text().strip().lower().startswith("0x03"):
                    continue
                counters={}
                for tag in ("mem_info_vram_total","mem_info_vram_used",
                            "mem_info_gtt_used","mem_info_vis_vram_used"):
                    try:
                        counters[tag+"_mib"] = round(int((d/tag).read_text())/MIB,1)
                    except (OSError,ValueError):
                        pass
                entries.append({"pci":d.name,**counters})
            except (OSError,ValueError):
                continue
    except (OSError,ValueError):
        pass
    return entries

def main() -> int:
    interval=1.0
    for index in range(900):
        row={"schema":"NOPEN_V081_DIRECT4G_TELEMETRY",
             "wall_utc":datetime.now(timezone.utc).isoformat(),
             "mono":round(time.monotonic(),3),
             "sample":index,
             "mem":mem(),"swap":swaps(),"gpu":gpu()}
        print(json.dumps(row,separators=(",",":")),flush=True)
        if index<899: time.sleep(interval)
    return 0

if __name__=="__main__":
    sys.exit(main())
