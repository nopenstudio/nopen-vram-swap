#!/usr/bin/env python3
"""Bounded userspace watchdog for Nopen experimental pager/recovery child tests.

No kernel configuration, ublk registration, swap activation, GPU reset or shell.
Detecting a dead child or timing it out does NOT guarantee kernel/GPU-driver
forward progress; processes blocked uninterruptibly may survive SIGKILL.
"""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

ALLOWED = {"nopen-recovery-lab": "RECOVERY_LAB_PASS",
           "nopen-vram-demand": "STRESS_PASS",
           "nopen-watchdog-fixture": "WATCHDOG_FIXTURE_PASS"}
MAX_REPORT = 1024 * 1024


def no_nbd() -> bool:
    try:
        lines = Path("/proc/swaps").read_text(encoding="ascii").splitlines()[1:]
    except (OSError, UnicodeError):
        return False
    return not any(line.split() and line.split()[0].startswith("/dev/nbd")
                   for line in lines)


def terminate_group(child: subprocess.Popen, grace: float) -> bool:
    # Process-group containment is explicit: signal the entire fresh session.
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        child.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            child.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            # Uninterruptible kernel waits are NOT solved by userspace kill.
            return False
    return True


def run(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout-seconds", type=float, default=10.0)
    parser.add_argument("--grace-seconds", type=float, default=0.5)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("child_args", nargs=argparse.REMAINDER)
    ns = parser.parse_args(argv)
    if not (1.0 <= ns.timeout_seconds <= 60.0 and
            0.1 <= ns.grace_seconds <= 2.0):
        parser.error("timeout must be 1..60s, grace must be 0.1..2s")
    target = ns.binary.resolve()
    if target.name not in ALLOWED or not target.is_file() or not os.access(target, os.X_OK):
        print("WATCHDOG_REFUSED executable not an allowed Nopen lab/test target",
              file=sys.stderr)
        return 2
    if os.geteuid() == 0 or not no_nbd():
        print("WATCHDOG_REFUSED root or active NBD swap", file=sys.stderr)
        return 2
    args = ns.child_args
    if args and args[0] == "--":
        args = args[1:]
    if not args or any(x.startswith("--backend") and x != "--backend" for x in args):
        print("WATCHDOG_REFUSED expected explicit child arguments", file=sys.stderr)
        return 2
    if target.name == "nopen-recovery-lab":
        try:
            idx = args.index("--backend")
            if args[idx+1] not in ("mock", "opencl"):
                raise ValueError("unexpected backend")
        except (ValueError, IndexError):
            print("WATCHDOG_REFUSED explicit mock/opencl backend required",
                  file=sys.stderr)
            return 2
    print(f"WATCHDOG_START target={target.name} timeout_s={ns.timeout_seconds:g} "
          "kernel_swap_untouched=YES", flush=True)
    with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as log:
        try:
            child = subprocess.Popen([str(target), *args], stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=subprocess.STDOUT,
                                     start_new_session=True, close_fds=True)
        except OSError as exc:
            print(f"WATCHDOG_FAILED launch={exc}", file=sys.stderr)
            return 3
        timed_out = False
        unreaped = False
        try:
            child.wait(timeout=ns.timeout_seconds)
        except subprocess.TimeoutExpired:
            timed_out = True
            unreaped = not terminate_group(child, ns.grace_seconds)
        except KeyboardInterrupt:
            terminate_group(child, ns.grace_seconds)
            print("WATCHDOG_ABORT interrupted", file=sys.stderr)
            return 130
        log.seek(0)
        observed = log.read(MAX_REPORT + 1)
    if len(observed) > MAX_REPORT:
        print("WATCHDOG_FAILED oversized child log; no false pass", file=sys.stderr)
        return 4
    if observed:
        print(observed, end="" if observed.endswith("\n") else "\n", flush=True)
    if unreaped:
        print("WATCHDOG_UNREAPED uninterruptible_or_unresponsive_child=YES "
              "kernel_liveness_not_guaranteed=YES", file=sys.stderr)
        return 5
    if timed_out:
        print("WATCHDOG_TIMEOUT child_terminated=YES "
              "real_gpu_reset_tested=NO", file=sys.stderr)
        return 6
    if child.returncode != 0:
        print(f"WATCHDOG_FAILED child_exit={child.returncode}", file=sys.stderr)
        return 7
    expected = ALLOWED[target.name]
    matches = [ln for ln in observed.splitlines() if ln.startswith(expected+" ")]
    if len(matches) != 1 or "no_global_swap=YES" not in matches[0]:
        print("WATCHDOG_FAILED child_exit_zero_but_no_verified_pass=YES",
              file=sys.stderr)
        return 8
    print(f"WATCHDOG_PASS verified_token={expected} child_exit=0 "
          "kernel_swap_untouched=YES real_gpu_reset_tested=NO")
    return 0


if __name__ == "__main__":
    raise SystemExit(run())
