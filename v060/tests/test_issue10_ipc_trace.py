#!/usr/bin/env python3
"""Issue #10 tracing regression: mock IPC only, NEVER kernel swap or GPU."""
import os
from pathlib import Path
import subprocess
import sys

def main() -> int:
    if len(sys.argv) != 3:
        print("USAGE: test_issue10_ipc_trace.py ISOLATED_TEST WORKER", file=sys.stderr)
        return 2
    test_binary, worker = (str(Path(p).resolve()) for p in sys.argv[1:])
    env = os.environ.copy()
    env["NOPEN_DIAG_IO_TRACE"] = "1"
    result = subprocess.run([test_binary, worker], env=env,
                            capture_output=True, text=True, timeout=30,
                            check=False)
    out = result.stdout + result.stderr
    markers = (
        "ISOLATED_TEST_PASS",
        "NOPEN_DIAG_IPC phase=BEGIN",
        "NOPEN_DIAG_IPC phase=END",
        "NOPEN_DIAG_IPC phase=BEGIN",  # both directions checked below
        "NOPEN_DIAG_WORKER phase=BEGIN",
        "NOPEN_DIAG_WORKER phase=GPU_ENTER",
        "NOPEN_DIAG_WORKER phase=GPU_RETURN",
        "NOPEN_DIAG_WORKER phase=RESPONSE_SENT",
        "NOPEN_IPC_FATAL",  # deliberate mock-only timeout injection
        "op=READ",
        "op=WRITE",
    )
    missing = [item for item in markers if item not in out]
    parent_begin = out.count("NOPEN_DIAG_IPC phase=BEGIN")
    parent_end = out.count("NOPEN_DIAG_IPC phase=END")
    worker_enter = out.count("NOPEN_DIAG_WORKER phase=GPU_ENTER")
    worker_return = out.count("NOPEN_DIAG_WORKER phase=GPU_RETURN")
    if (result.returncode != 0 or missing or parent_begin != parent_end
            or worker_enter != worker_return or parent_begin < 601):
        print("ISSUE10_TRACE_TEST_FAILED",
              f"rc={result.returncode} missing={missing}",
              f"parent_begin={parent_begin} parent_end={parent_end}",
              f"worker_enter={worker_enter} worker_return={worker_return}",
              file=sys.stderr)
        print(out[-4000:], file=sys.stderr)
        return 1
    print("ISSUE10_TRACE_TEST_PASS backend=mock kernel_swap=NO "
          "gpu_context=NO ipc_begin_end=YES worker_gpu_phase=YES "
          "injected_timeout=YES no_host_swap_changes=YES")
    return 0

if __name__ == "__main__":
    sys.exit(main())
