#!/usr/bin/env python3
"""Compare Bannerlord Nopen OFF/ON CSV captures. Only proven metrics are reported.

Memory deltas do not prove FPS speedup; /proc/swaps reports system-wide
swap usage, not per-game attribution. Optional plain frametime_ms CSV files
enable average FPS and 1% low. Never changes system settings or swap state.
"""
from __future__ import annotations
import argparse
import csv
import json
import math
from pathlib import Path
import statistics

REQ={"mode","rss_sum_kib","mem_available_kib","device_vram_used_kib",
     "nopen_swap_active","nopen_swap_used_kib","process_swap_sum_kib"}

def rows(path:Path,mode:str)->list[dict]:
    with path.open(newline="",encoding="utf-8") as fp:
        reader=csv.DictReader(fp)
        if not reader.fieldnames or REQ.difference(reader.fieldnames):
            raise ValueError(f"{path}: missing CSV columns")
        data=list(reader)
    if len(data)<2:raise ValueError(f"{path}: minimum 2 samples required")
    for item in data:
        if item["mode"]!=mode:raise ValueError(f"{path}: mode mismatch")
        if int(item["nopen_swap_active"]) != int(mode=="on"):
            raise ValueError(f"{path}: swap state mismatch")
        for key in REQ.difference({"mode","mem_available_kib","device_vram_used_kib"}):
            if key not in ("mode",) and int(item[key])<0:
                raise ValueError(f"{path}: negative {key}")
    return data

def as_float(row:dict,key:str):
    try:
        x=float(row[key])
        return x if math.isfinite(x) else None
    except (ValueError,TypeError,KeyError):
        return None

def avg(data:list[dict],key:str)->float|None:
    values=[as_float(item,key) for item in data]
    vals=[x for x in values if x is not None]
    return round(statistics.mean(vals),2) if vals else None

def frametimes(path:Path)->dict:
    """Accept simple numeric frametime_ms OR raw MangoHud CSV (fps,frametime,...).

    MangoHud headers may start after system metadata rows. Require every frame
    log_interval=0 in MangoHud for true frame-based 1% low statistics.
    """
    values=[]
    index=None
    with path.open(newline="",encoding="utf-8") as fp:
        for row in csv.reader(fp):
            if not row:continue
            names=[cell.strip().lower() for cell in row]
            if index is None and "frametime" in names:
                index=names.index("frametime")
                values=[]
                continue
            if index is not None:
                if len(row)<=index:
                    raise ValueError(f"{path}: malformed MangoHud CSV frame row")
                try:
                    values.append(float(row[index]))
                except ValueError as exc:
                    raise ValueError(f"{path}: invalid MangoHud frametime row") from exc
            elif len(row)==1:
                try:
                    values.append(float(row[0]))
                except ValueError:
                    # Header/metadata before MangoHud fps,frametime,... block.
                    pass
    if len(values)<100 or not all(math.isfinite(x) and x>0 for x in values):
        raise ValueError(f"{path}: require >=100 positive frame-time ms values")
    slowest=sorted(values,reverse=True)[:max(1,math.ceil(len(values)*.01))]
    return {"frames":len(values),"average_fps":round(1000/statistics.mean(values),2),
            "one_percent_low_fps":round(1000/statistics.mean(slowest),2),
            "p99_frametime_ms":round(sorted(values)[math.ceil(len(values)*.99)-1],3)}

def compute(off:list[dict],on:list[dict],
            off_frames:Path|None=None,on_frames:Path|None=None)->dict:
    peak_used=max(int(row["nopen_swap_used_kib"]) for row in on)
    start_used=int(on[0]["nopen_swap_used_kib"])
    off_rss=avg(off,"rss_sum_kib")
    on_rss=avg(on,"rss_sum_kib")
    report={
        "type":"NOPEN_V070_BANNERLORD_COMPARISON",
        "samples":{"off":len(off),"on":len(on)},
        "rss_sum_kib_mean":{"off":off_rss,"on":on_rss,
                            "difference_on_minus_off":round(on_rss-off_rss,2)},
        "mem_available_kib_mean":{"off":avg(off,"mem_available_kib"),
                                  "on":avg(on,"mem_available_kib")},
        "device_vram_used_kib_mean":{"off":avg(off,"device_vram_used_kib"),
                                     "on":avg(on,"device_vram_used_kib")},
        "nopen_swap_used_kib_peak":peak_used,
        "nopen_swap_used_kib_growth":peak_used-start_used,
        "kernel_gpu_swap_usage_observed":peak_used>0,
        "game_specific_swap_attribution_proven":False,
        "net_host_ram_saving_proven":False,
        "fps_speedup_proven":False,
        "notes":"Use same scene, resolution, settings, run length. /proc/swaps USED is system-wide; process tree RSS can double count shared pages."
    }
    if off_frames and on_frames:
        report["frametimes"]={"off":frametimes(off_frames),"on":frametimes(on_frames)}
        before=report["frametimes"]["off"]
        after=report["frametimes"]["on"]
        report["observed_avg_fps_delta"]=round(after["average_fps"]-before["average_fps"],2)
        report["observed_1pct_low_fps_delta"]=round(after["one_percent_low_fps"]-before["one_percent_low_fps"],2)
        # A single A/B difference is measured, not a causal improvement proof.
        report["fps_speedup_observed"]=report["observed_avg_fps_delta"]>0
        report["fps_speedup_proven"]=False
        report["notes"]+=" A single A/B comparison is not causal proof: repeat A/B/A or multiple controlled runs."
    return report

def main()->int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--off",type=Path,required=True)
    ap.add_argument("--on",type=Path,required=True)
    ap.add_argument("--frametime-off",type=Path)
    ap.add_argument("--frametime-on",type=Path)
    ap.add_argument("--json",type=Path)
    ns=ap.parse_args()
    if bool(ns.frametime_off)!=bool(ns.frametime_on):
        ap.error("provide BOTH frametime files or neither")
    result=compute(rows(ns.off,"off"),rows(ns.on,"on"),
                   ns.frametime_off,ns.frametime_on)
    out=json.dumps(result,indent=2,ensure_ascii=False)
    print(out)
    if ns.json:
        with ns.json.open("x",encoding="utf-8") as fp:fp.write(out+"\n")
    print("GAME_COMPARE_PASS kernel_gpu_swap_usage_observed="+
          ("YES" if result["kernel_gpu_swap_usage_observed"] else "NO")+
          " game_specific_attribution=NO no_global_change=YES")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
