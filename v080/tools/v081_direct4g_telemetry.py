#!/usr/bin/env python3
"""Read-only, bounded 1Hz physical GPU-only SWAP incident telemetry.
Logs basic /proc/swaps, VRAM/GTT, MemAvailable. Never edits host storage.
Does not claim to identify every GPU driver hang.
"""
from __future__ import annotations
import argparse
import json
import os
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
    parser=argparse.ArgumentParser(description="Read-only Nopen swap/GPU telemetry")
    parser.add_argument("--output-file", type=Path, default=None,
                        help="Existing absolute log file; durable fsync each sample")
    parser.add_argument("--samples",type=int,default=900)
    parser.add_argument("--interval",type=float,default=1.0)
    args=parser.parse_args()
    if args.samples<1 or args.samples>900 or args.interval<0.0 or args.interval>60.0:
        parser.error("samples must be 1..900; interval must be 0..60 seconds")
    if args.output_file is not None and (
        not args.output_file.is_absolute() or not args.output_file.is_file()):
        parser.error("--output-file must name an existing absolute regular file")
    stream=None
    try:
        if args.output_file is not None:
            stream=args.output_file.open("a",encoding="utf-8",buffering=1)
        for index in range(args.samples):
            row={"schema":"NOPEN_V081_DIRECT4G_TELEMETRY",
                 "wall_utc":datetime.now(timezone.utc).isoformat(),
                 "mono":round(time.monotonic(),3),
                 "sample":index,
                 "mem":mem(),"swap":swaps(),"gpu":gpu()}
            encoded=json.dumps(row,separators=(",",":"))
            if stream is None:
                print(encoded,flush=True)
            else:
                stream.write(encoded+"\\n")
                stream.flush()
                os.fsync(stream.fileno())
            if index+1<args.samples: time.sleep(args.interval)
    except OSError as exc:
        print(f"NOPEN_V081_MONITOR_IO_FATAL {type(exc).__name__}: {exc}",
              file=sys.stderr,flush=True)
        return 4
    finally:
        if stream is not None: stream.close()
    return 0

if __name__=="__main__":
    sys.exit(main())
