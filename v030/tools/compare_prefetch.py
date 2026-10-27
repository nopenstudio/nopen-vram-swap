#!/usr/bin/env python3
"""Three independent, checked GPU/cooperative pager workloads; no global swap."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

def fields(line: str) -> dict[str, str]:
    return dict(re.findall(r"([a-zA-Z0-9_]+)=([^\s]+)", line))

def main() -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary",type=Path,default=Path("build-v030/nopen-pager-bench"))
    ap.add_argument("--backend",choices=["mock","opencl"],default="opencl")
    ap.add_argument("--pool-mib",type=int,default=64)
    ap.add_argument("--logical-pages",type=int,default=1024)
    ap.add_argument("--ram-pages",type=int,default=32)
    ap.add_argument("--passes",type=int,default=2)
    args=ap.parse_args()
    if (args.pool_mib<1 or args.pool_mib>256 or args.logical_pages<2 or
        args.logical_pages>args.pool_mib*256 or args.ram_pages<16 or
        args.ram_pages>=args.logical_pages or args.passes<1 or args.passes>5):
        ap.error("Invalid bounded benchmark parameters (ram-pages must >=16)")
    exe=args.binary.resolve()
    if not exe.is_file():
        ap.error(f"Binary missing: {exe}")
    results={}
    for ahead in (0,4,16):
        cmd=[str(exe),"--backend",args.backend,"--pool-mib",str(args.pool_mib),
             "--logical-pages",str(args.logical_pages),"--ram-pages",str(args.ram_pages),
             "--prefetch-pages",str(ahead),"--passes",str(args.passes)]
        print(f"COMPARE_RUN prefetch_pages={ahead} backend={args.backend}",flush=True)
        try:
            run=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                               text=True,timeout=120,check=False)
        except (OSError,subprocess.TimeoutExpired) as exc:
            print(f"COMPARE_FAILED prefetch_pages={ahead} reason={exc}",file=sys.stderr)
            return 2
        print(run.stdout,end="" if run.stdout.endswith("\n") else "\n",flush=True)
        result_lines=[x for x in run.stdout.splitlines() if x.startswith("PAGER_RESULT ")]
        pass_lines=[x for x in run.stdout.splitlines() if x.startswith("PAGER_PASS ")]
        if run.returncode!=0 or len(result_lines)!=1 or len(pass_lines)!=1:
            print("COMPARE_FAILED unverified or crashed pager",file=sys.stderr);return 3
        f=fields(result_lines[0])
        p=fields(pass_lines[0])
        expected=args.logical_pages*args.passes
        try:
            actual=float(f["elapsed_s"])
            verified=int(f["verified_pages"])
            required=(f.get("backend")==args.backend and f.get("prefetch_pages")==str(ahead)
                and f.get("logical_pages")==str(args.logical_pages)
                and f.get("passes")==str(args.passes)
                and f.get("no_global_swap")=="YES" and f.get("prefetch_wasted")=="0"
                and verified==expected and int(f["prefetch_hits"])==(expected if ahead else 0)
                and int(f["demand_page_ins"])==(0 if ahead else expected)
                and p.get("backend")==args.backend and int(p["verified_pages"])==expected
                and p.get("no_global_swap")=="YES" and actual>0)
        except (KeyError,ValueError):
            required=False
        if not required:
            print("COMPARE_FAILED accounting, integrity or metadata mismatch",file=sys.stderr)
            return 4
        results[ahead]=(actual,f)
    base=results[0][0]
    for ahead in (4,16):
        secs=results[ahead][0]
        print(f"COMPARE_RESULT prefetch_pages={ahead} speedup_vs_demand={base/secs:.3f} "
              f"elapsed_s={secs:.6f} baseline_s={base:.6f} "
              f"backend={args.backend} sequential_workload_only=YES")
    print(f"COMPARE_PASS verified_runs=3 backend={args.backend} no_global_swap=YES "
          "not_system_swap=YES")
    return 0

if __name__=="__main__":
    sys.exit(main())
