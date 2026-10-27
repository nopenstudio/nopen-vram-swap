#!/usr/bin/env python3
"""Compare equal checked 4KiB page contents in CPU RAM and a cooperative GPU store.

Strict acceptance: each mode must finish and check all pages. Process RSS is
NOT equivalent to whole-system memory saved; GPU VRAM, GTT, driver/kernel
overheads, caches, and userfaultfd integration are not fully counted here.
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import re
import subprocess
import sys

def parse_fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([A-Za-z_0-9]+)=([^\s]+)", line))

def run_mode(binary: Path, backend: str, mib: int, ram: int, prefetch: int,
             timeout: float) -> dict[str, str]:
    cmd = [str(binary), "--backend", backend, "--logical-mib", str(mib),
           "--ram-pages", str(ram), "--prefetch-pages", str(prefetch)]
    print(f"ECONOMICS_RUN backend={backend} logical_mib={mib}", flush=True)
    try:
        run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True, timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise ValueError(f"mode {backend} did not finish: {exc}") from exc
    print(run.stdout, end="" if run.stdout.endswith("\n") else "\n", flush=True)
    results = [parse_fields(x) for x in run.stdout.splitlines()
               if x.startswith("ECONOMICS_RESULT ")]
    passed = [parse_fields(x) for x in run.stdout.splitlines()
              if x.startswith("ECONOMICS_PASS ")]
    if run.returncode != 0 or len(results) != 1 or len(passed) != 1:
        raise ValueError(f"mode {backend} failed or emitted incomplete results")
    result = results[0]
    done = passed[0]
    required = mib * 256
    try:
        good = (result.get("version") == "0.4.0-dev"
                and result.get("backend") == backend
                and int(result["logical_mib"]) == mib
                and int(result["logical_pages"]) == required
                and int(result["resident_limit_pages"]) == ram
                and int(result["prefetch_pages"]) == prefetch
                and int(result["verified_pages"]) == required
                and result.get("no_global_swap") == "YES"
                and result.get("scope") == "explicit_cooperative_process_only"
                and result.get("gpu_vram_excluded_from_rss") == "YES"
                and int(result["process_maxrss_kib"]) > 0
                and math.isfinite(float(result["init_s"]))
                and math.isfinite(float(result["verify_s"]))
                and float(result["init_s"]) > 0
                and float(result["verify_s"]) > 0
                and done.get("version") == "0.4.0-dev"
                and done.get("backend") == backend
                and int(done["verified_pages"]) == required
                and int(done["logical_mib"]) == mib
                and done.get("no_global_swap") == "YES")
        if backend != "cpu":
            good = good and int(result["page_ins"]) == required \
                and int(result["prefetch_wasted"]) == 0 \
                and int(result["prefetch_hits"]) == (required if prefetch else 0)
    except (KeyError, ValueError):
        good = False
    if not good:
        raise ValueError(f"mode {backend} produced inconsistent data")
    return result

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, default=Path("build-v040/nopen-economics"))
    ap.add_argument("--logical-mib", type=int, choices=(4,64,256,1024), default=64)
    ap.add_argument("--ram-pages", type=int, default=32)
    ap.add_argument("--prefetch-pages", type=int, choices=(0,4,16), default=16)
    ap.add_argument("--backend", choices=("opencl","mock"), default="opencl")
    ap.add_argument("--timeout-seconds", type=int, default=240)
    args = ap.parse_args()
    if (args.ram_pages < 16 or args.ram_pages > 1024 or
        args.ram_pages >= args.logical_mib * 256 or
        args.timeout_seconds < 5 or args.timeout_seconds > 600):
        ap.error("Invalid bounded benchmark options")
    binary = args.binary.resolve()
    if not binary.is_file():
        ap.error(f"Binary missing: {binary}")
    try:
        cpu = run_mode(binary,"cpu",args.logical_mib,args.ram_pages,
                       args.prefetch_pages,args.timeout_seconds)
        gpu = run_mode(binary,args.backend,args.logical_mib,args.ram_pages,
                       args.prefetch_pages,args.timeout_seconds)
    except ValueError as exc:
        print(f"COMPARISON_FAILED {exc}", file=sys.stderr)
        return 2
    cr = int(cpu["process_maxrss_kib"])
    gr = int(gpu["process_maxrss_kib"])
    saving = cr-gr
    pct = (saving/cr)*100
    print(f"ECONOMICS_COMPARISON version=0.4.0-dev baseline=cpu backend={args.backend} "
          f"logical_mib={args.logical_mib} cpu_maxrss_kib={cr} "
          f"paged_maxrss_kib={gr} process_rss_saved_kib={saving} "
          f"process_rss_saved_percent={pct:.2f} "
          f"gpu_extra_vram_mib={args.logical_mib if args.backend=='opencl' else 0} "
          "kernel_and_driver_not_accounted=YES whole_system_saving_proven=NO "
          "not_transparent_swap=YES no_global_swap=YES")
    print(f"COMPARISON_PASS verified_modes=2 verified_pages_each={args.logical_mib*256} "
          f"process_rss_delta_sign={'SAVED' if saving>0 else 'NOT_SAVED'} "
          "whole_system_saving_proven=NO")
    return 0

if __name__ == "__main__":
    sys.exit(main())
