#!/usr/bin/env python3
"""Static test-contract guard, no privileged operations."""
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
def read(path: str) -> str:
    return (repo / path).read_text(encoding="utf-8")

protocol = read("v060/src/isolated_protocol.h")
worker = read("v060/src/nopen_isolated_worker.c")
parent = read("v060/src/nopen_isolated_io.c")
test = read("v083/tests/test_v083_worker_failure.c")
build = read("v083/CMakeLists.txt")
workflow = read(".github/workflows/v083-mock-reliability.yml")
for opcode in ("NOPEN_IPC_HANG_TEST_ONLY",
               "NOPEN_IPC_EXIT_TEST_ONLY",
               "NOPEN_IPC_BAD_REPLY_TEST_ONLY"):
    assert opcode in protocol and opcode in worker and opcode in parent, opcode
assert "if(backend!=NOPEN_STORE_MOCK)break;" in worker
for fn in ("nopen_isolated_inject_crash", "nopen_isolated_inject_bad_reply"):
    assert fn in parent and fn in test
assert "if(r->backend!=NOPEN_STORE_MOCK || !r->alive)" in parent
assert "_exit(79)" in worker
assert 'msg.magic ^= UINT32_C(1)' in worker
assert "stats.child_alive==0" in test
assert "stats.io_errors==1" in test
assert "dst[i]==0xA5" in test
assert "NOPEN_STORE_MOCK" in test
assert "NOPEN_OPENCL=OFF" in workflow
assert "ctest --test-dir build-v083-mock -R '^v083_'" in workflow
assert "nopen-v083-test-worker-failure" in build
assert "v083_reclaim_forward_progress_blocker_audit" in build
for token in ("/dev/ublk", "mkswap", "swapon ", "swapoff ", "modprobe "):
    assert token not in test, token
print("V083_FAULT_POLICY_PASS mock_only=YES worker_exit=YES malformed=YES "
      "deadline=YES failure_closed=YES kernel_swap=NO")
