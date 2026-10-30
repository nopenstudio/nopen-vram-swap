#!/usr/bin/env python3
"""Mock holding works; reject dangerous GPU hold parameters before OpenCL init."""
import subprocess
from pathlib import Path
import sys
exe=Path(sys.argv[1])
def invoke(*args):
    return subprocess.run([str(exe),*args],text=True,capture_output=True,timeout=10)
p=invoke("--backend","mock","--mode","direct","--pool-mib","16",
         "--operations","32","--full-passes","2","--hold-seconds","1")
assert p.returncode==0 and "V080_GAME_HOLD_READY" in p.stdout and "V080_GAME_HOLD_PASS" in p.stdout, p
base=("--backend","opencl","--mode","direct","--pool-mib","4096",
      "--operations","32","--full-passes","2","--ack","I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP")
for seconds in ("1","301"):
    p=invoke(*base,"--hold-seconds",seconds)
    assert p.returncode==2 and "V080_GPU_BENCH_REFUSED" in p.stderr, p
p=invoke("--backend","opencl","--mode","pinned","--pool-mib","4096",
         "--operations","32","--full-passes","2",
         "--ack","I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP","--hold-seconds","60")
assert p.returncode==2 and "V080_GPU_BENCH_REFUSED" in p.stderr, p
print("V080_ALPHA4_HOLD_GUARD_PASS mock_integrity=YES timeout_bounds=YES pinned_blocked=YES kernel_swap=NO")
