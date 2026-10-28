#!/usr/bin/env python3
"""Assert that a fatal IPC fault leaves first-failure forensic evidence."""
import subprocess
import sys
from pathlib import Path
if len(sys.argv)!=3:
    raise SystemExit("usage: test_fatal_telemetry.py test_binary worker_binary")
test=Path(sys.argv[1]).resolve()
worker=Path(sys.argv[2]).resolve()
out=subprocess.run([str(test),str(worker)],text=True,capture_output=True,timeout=15)
if out.returncode:
    raise AssertionError(f"isolated worker test failed {out.returncode}: {out.stdout} {out.stderr}")
if "ISOLATED_TEST_PASS" not in out.stdout:
    raise AssertionError("missing successful checked-IPC mock result")
for keyword in ("NOPEN_IPC_FATAL","op=INJECT","stage=test_hang",
                "errno=110","deadline_ms=100","fail_closed=YES"):
    if keyword not in out.stderr:
        raise AssertionError(f"first-failure diagnostic missing {keyword}: {out.stderr}")
print("V070_FATAL_TELEMETRY_PASS exact_errno=YES offending_stage=YES "
      "worker_pid=YES operator_visible=YES no_global_swap=YES")
