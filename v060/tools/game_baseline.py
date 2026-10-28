#!/usr/bin/env python3
"""Read-only RAM, process-tree and GPU VRAM baseline for a running game.

This DOES NOT launch/modify Bannerlord, inject code, register block devices,
measure FPS, or enable swap. For FPS and 1% low, use a separate frametime
recorder (e.g. MangoHud). RSS sums double-count shared pages across children;
PSS would be needed for precise physical memory attribution.
"""
from __future__ import annotations
import argparse
import csv
import os
from pathlib import Path
import sys
import time

def process_info(pid: int) -> tuple[int, int, int] | None:
    try:
        status=Path(f"/proc/{pid}/status").read_text(encoding="ascii")
    except (OSError,UnicodeError):
        return None
    parent,rss,swap=0,0,0
    try:
        for line in status.splitlines():
            if line.startswith("PPid:"):
                parent=int(line.split()[1])
            elif line.startswith("VmRSS:"):
                rss=int(line.split()[1])
            elif line.startswith("VmSwap:"):
                swap=int(line.split()[1])
    except (IndexError,ValueError):
        return None
    return parent,rss,swap

def family(root: int) -> tuple[int,int,int]:
    pids={}
    for path in Path("/proc").iterdir():
        if path.name.isdecimal():
            value=process_info(int(path.name))
            if value is not None:pids[int(path.name)]=value
    if root not in pids:
        raise ValueError("target process no longer exists")
    children={root};frontier=[root]
    while frontier:
        current=frontier.pop()
        for pid,(ppid,_,_) in pids.items():
            if pid not in children and ppid==current:
                children.add(pid)
                frontier.append(pid)
    return len(children),sum(pids[p][1] for p in children),sum(pids[p][2] for p in children)

def memory_available() -> int|None:
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemAvailable:"):
                return int(line.split()[1])
    except (OSError,UnicodeError,ValueError,IndexError):
        pass
    return None

def amd_counters() -> tuple[int|None,int|None]:
    """Device-wide; ambiguous multiple AMD GPUs return unknown."""
    candidates=[]
    try:
        for device in Path("/sys/bus/pci/devices").iterdir():
            if ((device/"vendor").read_text().strip().lower()=="0x1002"
                and (device/"class").read_text().strip().lower().startswith("0x03")):
                candidates.append(device)
    except (OSError,UnicodeError):
        return None,None
    if len(candidates)!=1:return None,None
    try:
        return (int((candidates[0]/"mem_info_vram_used").read_text())//1024,
                int((candidates[0]/"mem_info_gtt_used").read_text())//1024)
    except (OSError,ValueError):
        return None,None

def main() -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pid",type=int,required=True,help="Existing game PID (do not use root)")
    ap.add_argument("--duration-seconds",type=int,default=60)
    ap.add_argument("--interval-ms",type=int,default=1000)
    ap.add_argument("--csv",type=Path,required=True)
    args=ap.parse_args()
    if (args.pid<2 or args.duration_seconds<1 or args.duration_seconds>1800
        or args.interval_ms<250 or args.interval_ms>5000):
        ap.error("duration 1..1800s; interval 250..5000ms; PID>=2")
    if os.geteuid()==0:
        print("BASELINE_REFUSED root is not necessary",file=sys.stderr);return 2
    if args.csv.exists():
        print("BASELINE_REFUSED will not overwrite an existing result",file=sys.stderr)
        return 2
    try:
        family(args.pid)
    except ValueError as error:
        print(f"BASELINE_REFUSED {error}",file=sys.stderr);return 2
    columns=("elapsed_s","process_count","rss_sum_kib","swap_sum_kib",
             "mem_available_kib","device_vram_used_kib","device_gtt_used_kib")
    rows=[]
    interval=args.interval_ms/1000.0
    start=time.monotonic()
    next_tick=start
    print(f"GAME_BASELINE_START pid={args.pid} duration_s={args.duration_seconds} "
          "read_only=YES frame_times_not_measured=YES",flush=True)
    while True:
        try:
            count,rss,swap=family(args.pid)
        except ValueError:
            print("GAME_BASELINE_STOP target_exited=YES",flush=True);break
        vram,gtt=amd_counters()
        rows.append((round(time.monotonic()-start,3),count,rss,swap,
                     memory_available(),vram,gtt))
        next_tick+=interval
        if next_tick-start > args.duration_seconds:
            break
        time.sleep(max(0,next_tick-time.monotonic()))
    if not rows:
        print("GAME_BASELINE_FAILED no data",file=sys.stderr);return 3
    try:
        with args.csv.open("x",encoding="utf-8",newline="") as fh:
            writer=csv.writer(fh)
            writer.writerow(columns)
            writer.writerows(rows)
    except OSError as error:
        print(f"GAME_BASELINE_FAILED csv={error}",file=sys.stderr);return 3
    print(f"GAME_BASELINE_PASS samples={len(rows)} "
          f"rss_sum_peak_kib={max(x[2] for x in rows)} "
          f"swap_sum_peak_kib={max(x[3] for x in rows)} "
          f"file={args.csv} no_global_swap=YES "
          "rss_shared_double_count_possible=YES fps_not_measured=YES")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
