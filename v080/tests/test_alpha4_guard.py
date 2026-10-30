#!/usr/bin/env python3
"""Reject unsupported GPU test conditions; mock tests never allocate VRAM."""
from pathlib import Path
import subprocess
import sys
binary=Path(sys.argv[1])
base=["--backend","opencl","--mode","direct","--pool-mib","4096"]
tests={
    "wrong_ack":base+["--full-passes","2","--ack","I_ACCEPT_2GIB_GPU_ONLY_NO_SWAP"],
    "missing_ack":base+["--full-passes","2"],
    "single_pass":base+["--full-passes","1","--ack","I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP"],
    "oversized":base[:-1]+["8192","--full-passes","2","--ack","I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP"],
}
for name,args in tests.items():
    p=subprocess.run([str(binary),*args],text=True,capture_output=True,
                     timeout=8,check=False)
    assert p.returncode==2 and "V080_GPU_BENCH_REFUSED" in p.stderr, (name,p)
    assert "V080_GPU_BENCH_START" not in p.stdout
print("V080_ALPHA4_GUARD_PASS strict_4g_ack=YES two_passes=YES over_4g_blocked=YES kernel_swap=NO")
