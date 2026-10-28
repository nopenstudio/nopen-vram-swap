#!/usr/bin/env python3
"""Read-only Bannerlord/proton process-tree memory telemetry with real swap proof.

Nopen-specific USED KiB comes from kernel /proc/swaps (system-wide); it does
NOT attribute swapped pages to the target game's PID. FPS and frame pacing
require an independent frame-time logger. Never enables or alters Linux swap.
"""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
from pathlib import Path
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"v060"/"tools"))
from game_baseline import family, memory_available, amd_counters  # noqa: E402
from find_bannerlord import find_game_pid

def swap_snapshot(dev: str) -> tuple[bool,int,int]:
    all_used=0
    nopen_used=0
    active=False
    for row in Path("/proc/swaps").read_text(encoding="ascii").splitlines()[1:]:
        fields=row.split()
        if len(fields)<5:
            raise ValueError(f"invalid /proc/swaps row {row!r}")
        size,used=int(fields[2]),int(fields[3])
        if used<0 or used>size:
            raise ValueError("invalid swap used counter")
        all_used+=used
        if fields[0]==dev:
            active=True
            nopen_used=used
    return active,nopen_used,all_used

def main(argv: list[str]|None=None) -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pid",default="auto",help="actual Bannerlord game PID or auto (default); excludes Steam/TaleWorlds launcher")
    ap.add_argument("--mode",choices=("off","on"),required=True)
    ap.add_argument("--duration-seconds",type=int,default=120)
    ap.add_argument("--interval-ms",type=int,default=1000)
    ap.add_argument("--device",default="/dev/ublkb23")
    ap.add_argument("--csv",type=Path,required=True)
    opts=ap.parse_args(argv)
    try:
        opts.pid=find_game_pid() if opts.pid=="auto" else int(opts.pid)
    except (ValueError, OSError) as exc:
        print(f"GAME_PROBE_REFUSED Bannerlord_pid={exc}",file=sys.stderr)
        return 3
    if(opts.pid<2 or not opts.device.startswith("/dev/ublkb") or
       opts.duration_seconds<1 or opts.duration_seconds>1800 or
       opts.interval_ms<250 or opts.interval_ms>5000):
        ap.error("invalid PID, swap-device, duration or sampling interval")
    try:
        active,used,total=swap_snapshot(opts.device)
        family(opts.pid)
    except (OSError, ValueError) as exc:
        print(f"GAME_PROBE_REFUSED preflight={exc}",file=sys.stderr)
        return 3
    if bool(opts.mode=="on")!=active:
        print(f"GAME_PROBE_REFUSED mode={opts.mode} nopen_swap_active={active}",
              file=sys.stderr)
        return 3
    if opts.csv.exists():
        print("GAME_PROBE_REFUSED CSV already exists",file=sys.stderr)
        return 3
    cols=("utc","elapsed_s","mode","pid","process_count","rss_sum_kib",
          "process_swap_sum_kib","mem_available_kib","device_vram_used_kib",
          "device_gtt_used_kib","nopen_swap_active","nopen_swap_used_kib",
          "all_swap_used_kib")
    rows=[]
    start=time.monotonic()
    next_sample=start
    last_active=True
    print(f"GAME_PROBE_START mode={opts.mode} pid={opts.pid} "
          f"device={opts.device} kernel_swap_active={active} "
          "read_only=YES per_pid_swap_attribution=NO",flush=True)
    while True:
        try:
            count,rss,vm_swap=family(opts.pid)
            now_active,nopen_used,total_used=swap_snapshot(opts.device)
        except (ValueError,OSError):
            break
        if now_active!=active:
            last_active=False
            break
        vram,gtt=amd_counters()
        rows.append((datetime.now(timezone.utc).isoformat(),
                     round(time.monotonic()-start,3),opts.mode,opts.pid,
                     count,rss,vm_swap,memory_available(),vram,gtt,
                     int(now_active),nopen_used,total_used))
        next_sample+=opts.interval_ms/1000
        if next_sample-start>opts.duration_seconds:break
        time.sleep(max(0.0,next_sample-time.monotonic()))
    if not rows or not last_active:
        print("GAME_PROBE_FAILED game_exited_or_swap_state_changed",file=sys.stderr)
        return 4
    try:
        with opts.csv.open("x",newline="",encoding="utf-8") as fh:
            wr=csv.writer(fh);wr.writerow(cols);wr.writerows(rows)
    except OSError as exc:
        print(f"GAME_PROBE_FAILED csv_io={exc}",file=sys.stderr)
        return 4
    max_nopen=max(int(row[11]) for row in rows)
    delta_nopen=max_nopen-int(rows[0][11])
    print(f"GAME_PROBE_PASS mode={opts.mode} samples={len(rows)} "
          f"nopen_used_peak_kib={max_nopen} nopen_used_growth_kib={delta_nopen} "
          f"kernel_swap_active={'YES' if active else 'NO'} "
          "nopen_swap_is_system_wide=YES fps_measured=NO "
          f"csv={opts.csv}")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
