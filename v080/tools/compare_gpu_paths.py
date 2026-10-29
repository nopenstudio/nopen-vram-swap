#!/usr/bin/env python3
"""Compare matched real OpenCL transfer benchmarks without claiming causality."""
import argparse
import json
from pathlib import Path

SIZES = (4, 64, 1024, 4096)

def parse(path: Path, expected_mode: str) -> dict[int, dict]:
    data: dict[int,dict] = {}
    final = False
    full_arena = False
    for line in path.read_text(encoding="utf-8",errors="replace").splitlines():
        if line.startswith("V080_GPU_FULL_ARENA_VERIFIED"):
            pairs = dict(x.split("=",1) for x in line.split()[1:] if "=" in x)
            full_arena |= pairs.get("mode")==expected_mode and \
                          pairs.get("backend")=="opencl" and \
                          pairs.get("capacity_mib")=="1024"
        if line.startswith("V080_GPU_BENCH_RESULT"):
            pairs = dict(x.split("=",1) for x in line.split()[1:] if "=" in x)
            if pairs.get("backend")!="opencl" or pairs.get("mode")!=expected_mode:
                raise ValueError(f"{path}: wrong backend/mode")
            if (pairs.get("capacity_mib")!="1024" or
                    pairs.get("all_bytes_verified")!="YES" or
                    pairs.get("kernel_swap")!="NO"):
                raise ValueError(f"{path}: missing integrity/no-swap proof")
            size=int(pairs["chunk_kib"])
            if size in data:
                raise ValueError(f"{path}: duplicate chunk {size}")
            data[size] = {
                "write_mib_s": float(pairs["write_mib_s"]),
                "read_mib_s": float(pairs["read_mib_s"]),
                "write_p95_us": float(pairs["write_p95_us"]),
                "read_p95_us": float(pairs["read_p95_us"]),
                "operations": int(pairs["operations"]),
            }
            if any(x<=0 for x in (data[size]["write_mib_s"],
                                   data[size]["read_mib_s"],
                                   data[size]["write_p95_us"],
                                   data[size]["read_p95_us"])):
                raise ValueError(f"{path}: invalid non-positive metric")
        if line.startswith("V080_GPU_BENCH_PASS"):
            pairs=dict(x.split("=",1) for x in line.split()[1:] if "=" in x)
            final=(pairs.get("backend")=="opencl" and
                   pairs.get("mode")==expected_mode and
                   pairs.get("capacity_mib")=="1024" and
                   pairs.get("full_arena_integrity")=="YES" and
                   pairs.get("kernel_swap")=="NO")
    if not final or not full_arena or sorted(data)!=list(SIZES):
        raise ValueError(f"{path}: missing full 1GiB GPU+integrity pass or size samples")
    return data

def compare(direct: dict[int,dict], pinned: dict[int,dict]) -> dict:
    chunks=[]
    for size in SIZES:
        a,b=direct[size],pinned[size]
        if a["operations"]!=b["operations"]:
            raise ValueError(f"unmatched {size}KiB operation counts")
        def ratio(x,y):
            return round((y/x-1)*100,2)
        chunks.append({
            "chunk_kib":size,
            "direct_write_mib_s":a["write_mib_s"],
            "pinned_write_mib_s":b["write_mib_s"],
            "observed_write_percent_change":ratio(a["write_mib_s"],b["write_mib_s"]),
            "direct_read_mib_s":a["read_mib_s"],
            "pinned_read_mib_s":b["read_mib_s"],
            "observed_read_percent_change":ratio(a["read_mib_s"],b["read_mib_s"]),
            "direct_write_p95_us":a["write_p95_us"],
            "pinned_write_p95_us":b["write_p95_us"],
            "direct_read_p95_us":a["read_p95_us"],
            "pinned_read_p95_us":b["read_p95_us"],
        })
    return {
        "type":"NOPEN_V080_ALPHA2_GPU_TRANSFER_COMPARISON",
        "backend":"opencl", "capacity_mib":1024,
        "comparison":"single_pair_not_a_causal_performance_proof",
        "kernel_swap":False,
        "ssd_shadow_included":False,
        "notes":"A faster bulk OpenCL path does NOT demonstrate faster 4KiB write-through ublk SWAP, FPS gains, or stable Linux reclaim.",
        "chunks":chunks,
    }

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--direct",type=Path,required=True)
    parser.add_argument("--pinned",type=Path,required=True)
    parser.add_argument("--json",type=Path)
    ns=parser.parse_args()
    try:
        result=compare(parse(ns.direct,"direct"),parse(ns.pinned,"pinned"))
    except (OSError,ValueError) as exc:
        parser.error(str(exc))
    rendered=json.dumps(result,indent=2,ensure_ascii=False)
    print(rendered)
    if ns.json:
        if ns.json.exists():
            parser.error("output JSON already exists; never overwrite benchmark")
        ns.json.write_text(rendered+"\n",encoding="utf-8")
    print("V080_ALPHA2_COMPARE_PASS measured_direct_and_pinned=YES"
          " fps_gain_proven=NO kernel_swap=NO")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
