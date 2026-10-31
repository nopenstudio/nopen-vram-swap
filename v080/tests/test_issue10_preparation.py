#!/usr/bin/env python3
"""Safely smoke-test the next-swap audit and fsynced telemetry on CI.
NEVER starts ublk, touches swaps, or creates GPU allocations.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
AUDIT = ROOT / "v080/tools/issue10_next_swap_readiness.py"
MONITOR = ROOT / "v080/tools/v081_direct4g_telemetry.py"
LAUNCH = ROOT / "v080/tools/v081_direct4g_kernel_lab.sh"


def require(ok: bool, name: str) -> None:
    if not ok:
        raise AssertionError(name)


def main() -> int:
    audit = subprocess.run(
        [sys.executable, str(AUDIT)], capture_output=True, text=True,
        timeout=10, check=False)
    require(audit.returncode in (0, 2), "audit returned unexpected code")
    payload, end = json.JSONDecoder().raw_decode(audit.stdout)
    require("ISSUE10_PREP_" in audit.stdout[end:], "audit status marker missing")
    for key in ("kernel_swap_activated", "kernel_swap_activation_authorized",
                "host_disks_modified", "existing_zram_modified",
                "reclaim_forward_progress_proven"):
        require(payload[key] is False, f"audit contract {key}")
    require(payload["preparation_gate"] in ("PASS", "BLOCKED"), "gate status")
    require(isinstance(payload["failures"], list), "failure list")
    with tempfile.TemporaryDirectory(prefix="nopen-issue10-") as folder:
        destination = Path(folder) / "monitor.jsonl"
        destination.touch()
        run = subprocess.run(
            [sys.executable, str(MONITOR), "--output-file", str(destination),
             "--samples", "2", "--interval", "0"],
            capture_output=True, text=True, timeout=15, check=False)
        require(run.returncode == 0, f"telemetry failure: {run.stderr}")
        rows = [json.loads(line) for line in destination.read_text().splitlines()]
        require(len(rows) == 2, "fsync output line count")
        require([row["sample"] for row in rows] == [0, 1],
                "telemetry sequence")
        require(all(row["schema"] == "NOPEN_V081_DIRECT4G_TELEMETRY"
                    for row in rows), "telemetry schema")
    source = LAUNCH.read_text(encoding="utf-8")
    require('--output-file "$monitor_log"' in source,
            "launcher must use fsynced telemetry")
    require("NOPEN_DIAG_IO_TRACE=1" in source,
            "opt-in ublk tracing must be available")
    for name in (AUDIT, MONITOR):
        src = name.read_text()
        for bad in ("subprocess.Popen(", "os.system(", "os.execv("):
            require(bad not in src, f"unexpected process operation in {name}")
    print("ISSUE10_PREPARATION_TEST_PASS audit_readonly=YES "
          "telemetry_fsync_path=YES kernel_swap=NO gpu_allocations=NO "
          "system_swap_unchanged=YES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
