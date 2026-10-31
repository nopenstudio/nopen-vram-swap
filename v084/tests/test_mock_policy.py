#!/usr/bin/env python3
"""Static mock-only safety contract. No host mutation or GPU use."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
def read(name: str) -> str:
    return (root / name).read_text(encoding="utf-8")

worker = read("v060/src/nopen_isolated_worker.c")
test = read("v084/tests/test_mock_reclaim_stall.c")
model = read("v084/tools/reclaim_cycle.py")
workflow = read(".github/workflows/v084-reclaim-cycle-mock.yml")
cmake = read("v084/CMakeLists.txt")
assert "NOPEN_V084_MOCK_WAIT_ON_RECLAIM" in worker and "NOPEN_V084_MOCK_WAIT_ON_RECLAIM" in test
assert "backend==NOPEN_STORE_MOCK" in worker
assert "(msg.op==NOPEN_IPC_READ || msg.op==NOPEN_IPC_WRITE)" in worker
assert "synthetic_dependency=YES" in worker
assert "actual_reclaim=NO" in worker
assert "NOPEN_STORE_MOCK" in test
assert "rc==-ETIMEDOUT" in test
assert "dst[j]==0xA5" not in test  # fail if refactor accidentally bypasses direct buffer check
assert "dest[j]==0xA5" in test
assert "stats.child_alive==0" in test
assert "NOPEN_OPENCL=OFF" in workflow
assert "v084_mock_alive_worker_stalled_read_write_timeout" in cmake
assert "physical_kernel_swap=BLOCKED" in model
assert '"kernel_swap_authorized": False' in model
for p in ("v084/tools/reclaim_cycle.py", "v084/tests/test_reclaim_cycle.py",
          "v084/tests/test_mock_reclaim_stall.c"):
    text=read(p)
    # No native block device paths or host-mutating syscalls.
    for banned in ("/dev/ublk", "/dev/nbd", "systemctl ", "modprobe ",
                   "swapon(", "swapoff(", "mkswap ", "subprocess.Popen("):
        assert banned not in text, (p, banned)
print("V084_MOCK_POLICY_PASS backend=mock_only synthetic_reclaim=YES"
      " no_host_pressure=YES kernel_swap=BLOCKED")
