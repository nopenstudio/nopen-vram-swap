#!/usr/bin/env python3
"""Check that risky holder modes are rejected BEFORE OpenCL init."""
from pathlib import Path
import subprocess
import sys

assert len(sys.argv)==2
exe=Path(sys.argv[1]).resolve()
assert exe.is_file()
def rejected(args):
    p=subprocess.run([str(exe),*args],capture_output=True,text=True,timeout=4)
    assert p.returncode != 0, (args,p.stdout,p.stderr)
    assert "V085_HOLDER_REFUSED" in p.stderr, (args,p.stdout,p.stderr)
for args in [
    ["--backend","opencl","--pool-mib","4096","--seconds","5"],
    ["--backend","opencl","--pool-mib","1024","--seconds","5"],
    ["--backend","opencl","--pool-mib","1024","--seconds","5",
     "--ack","I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP"],
    ["--backend","opencl","--pool-mib","1024","--seconds","120",
     "--ack","I_ACCEPT_1GIB_HOLDER_NO_SWAP"],
    ["--backend","mock","--pool-mib","4096","--seconds","0"],
    ["--backend","mock","--pool-mib","8","--seconds","10"],
]:
    rejected(args)

source=(Path(__file__).resolve().parents[1]/"src/nopen_reserved_holder.c").read_text()
assert "getenv(" not in source
assert "swapon(" not in source
assert "swapoff(" not in source
assert "system(" not in source
assert "no_nopen_kernel_swap()" in source
assert "I_ACCEPT_1GIB_HOLDER_NO_SWAP" in source
assert "pinned_physical_vram=UNPROVEN" in source
assert "kernel_swap=NO" in source
print("V085_HOLDER_POLICY_PASS unauthorized_opencl_refused=YES"
      " kernel_swap=BLOCKED physical_residency=UNPROVEN")
