#!/usr/bin/env python3
"""Offline CLI guard test. NEVER requests an actual 2GiB GPU allocation."""
import subprocess
import sys
from pathlib import Path

binary = Path(sys.argv[1])
cases = (
    ("reject_4g", ["--backend", "opencl", "--mode", "direct",
                   "--pool-mib", "4096", "--full-passes", "2",
                   "--ack", "I_ACCEPT_2GIB_GPU_ONLY_NO_SWAP"]),
    ("reject_2g_weak_passes", ["--backend", "opencl", "--mode", "direct",
                   "--pool-mib", "2048", "--full-passes", "1",
                   "--ack", "I_ACCEPT_2GIB_GPU_ONLY_NO_SWAP"]),
    ("reject_2g_wrong_ack", ["--backend", "opencl", "--mode", "direct",
                   "--pool-mib", "2048", "--full-passes", "2",
                   "--ack", "I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP"]),
)
for name, args in cases:
    p = subprocess.run([str(binary), *args], capture_output=True,
                       text=True, timeout=10, check=False)
    if (p.returncode != 2 or "V080_GPU_BENCH_REFUSED" not in p.stderr
            or "V080_GPU_BENCH_START" in p.stdout):
        raise AssertionError(f"{name}: unexpected execution: {p.returncode} {p.stdout} {p.stderr}")
print("V080_ALPHA3_CLI_GUARD_PASS 4g_blocked=YES two_pass_minimum=YES strict_ack=YES")
