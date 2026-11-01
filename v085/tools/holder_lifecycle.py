#!/usr/bin/env python3
"""Single-session GPU-only Holder lifecycle telemetry.

Real mode permits only the existing 1GiB OpenCL Holder with explicit ACK.
No SWAP, kernel module, driver configuration, boot edit, root, or GPU pin.
Sampled AMDGPU counters are GLOBAL, not attributable to Nopen uniquely.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from reserved_vram_observe import select_gpu, snapshot
from reserved_vram_preflight import evaluate

ACK = "I_ACCEPT_1GIB_LIFECYCLE_GPU_ONLY_NO_SWAP"
CHILD_ACK = "I_ACCEPT_1GIB_HOLDER_NO_SWAP"
MIB = 1024 * 1024


def event(phase, index, gpu, meminfo, swaps, baseline, running):
    row = snapshot(gpu, meminfo, swaps)
    return {
        "schema": "NOPEN_V085_HOLDER_LIFECYCLE",
        "phase": phase, "sample": index,
        "mono_seconds": round(time.monotonic(), 3),
        "holder_process_running": running,
        "global_vram_used_mib": round(row["vram_used_bytes"] / MIB, 3),
        "global_gtt_used_mib": round(row["gtt_used_bytes"] / MIB, 3),
        "global_vram_delta_vs_baseline_mib": round(
            (row["vram_used_bytes"] - baseline["vram_used_bytes"]) / MIB, 3),
        "host_mem_available_mib": round(row["host_mem_available_bytes"] / MIB, 3),
        "gpu_ownership_attributed": False,
        "exclusive_physical_pin_proven": False,
        "swap_modified": False,
        "safety_gate": "KERNEL_SWAP_BLOCKED",
    }


def run_lifecycle(*, gpu, meminfo, swaps, executable, backend, seconds,
                  post_samples, interval, emit=print, sleep=time.sleep):
    baseline = snapshot(gpu, meminfo, swaps)
    rows = []

    def record(phase, index, running):
        row = event(phase, index, gpu, meminfo, swaps, baseline, running)
        rows.append(row)
        emit(json.dumps(row, ensure_ascii=False), flush=True)

    record("before", 0, False)
    sleep(interval)
    record("before", 1, False)
    pool = 1024 if backend == "opencl" else 8
    cmd = [str(executable), "--backend", backend, "--pool-mib", str(pool),
           "--seconds", str(seconds)]
    if backend == "opencl":
        cmd += ["--ack", CHILD_ACK]
    child = None
    ret = -1
    try:
        with tempfile.TemporaryFile(mode="w+t", encoding="utf-8") as log:
            child = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
            start = time.monotonic()
            timeout = seconds + (35 if backend == "opencl" else 10)
            count = 0
            while child.poll() is None:
                record("holding", count, True)
                count += 1
                if time.monotonic() - start > timeout:
                    raise TimeoutError(
                        "Holder exceeded watchdog; GPU-driver completion is NOT guaranteed")
                sleep(interval)
            ret = child.wait()
            log.seek(0)
            for line in log:
                emit("V085_HOLDER_CHILD " + line.rstrip("\n"), flush=True)
            record("after_exit", 0, False)
            for i in range(1, post_samples + 1):
                sleep(interval)
                record("after_exit", i, False)
    finally:
        if child is not None and child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                child.kill()
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    pass  # D-state GPU-driver call may ignore termination

    last = rows[-1]
    emit("V085_LIFECYCLE_" + ("PASS" if ret == 0 else "FAILED") +
         " holder_rc=" + str(ret) +
         " post_exit_samples=" + str(post_samples + 1) +
         " final_vram_delta_mib=" +
         str(last["global_vram_delta_vs_baseline_mib"]) +
         " global_counter_only=YES physical_pin=UNPROVEN kernel_swap=BLOCKED",
         flush=True)
    return ret, rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--holder", required=True, type=Path)
    p.add_argument("--backend", choices=("opencl", "mock"), default="opencl")
    p.add_argument("--seconds", type=int, default=15)
    p.add_argument("--post-seconds", type=int, default=30)
    p.add_argument("--interval", type=float, default=1)
    p.add_argument("--pci", default="0000:03:00.0")
    p.add_argument("--ack")
    p.add_argument("--pci-root", type=Path, default=Path("/sys/bus/pci/devices"))
    p.add_argument("--meminfo", type=Path, default=Path("/proc/meminfo"))
    p.add_argument("--swaps-file", type=Path, default=Path("/proc/swaps"))
    a = p.parse_args()

    if os.geteuid() == 0:
        p.error("Refusing to run as root")
    exe = a.holder.resolve()
    if (exe.name != "nopen-v085-reserved-holder" or
            not exe.is_file() or not os.access(exe, os.X_OK)):
        p.error("--holder must be the built nopen-v085-reserved-holder")
    if a.backend == "opencl":
        if (not 5 <= a.seconds <= 30 or
                not 20 <= a.post_seconds <= 90 or
                not 0.5 <= a.interval <= 5 or a.ack != ACK):
            p.error("OpenCL requires 5..30s hold, 20..90s post, "
                    "0.5..5s interval, exact ACK")
        if (a.pci_root != Path("/sys/bus/pci/devices") or
                a.meminfo != Path("/proc/meminfo") or
                a.swaps_file != Path("/proc/swaps")):
            p.error("Real GPU mode refuses alternate synthetic PCI/proc roots")
        decision = evaluate(a.pci_root, a.meminfo, a.swaps_file,
                            pool_mib=1024, pci=a.pci)
        if decision["reasons"]:
            p.error("Hardware preflight refused: " + ",".join(decision["reasons"]))
        post_samples = math.ceil(a.post_seconds / a.interval)
    else:
        if (a.seconds not in (0, 1) or
                not 1 <= a.post_seconds <= 3 or a.interval != 0):
            p.error("Mock requires seconds 0..1, post-seconds 1..3 and interval 0")
        post_samples = a.post_seconds
    try:
        gpu = select_gpu(a.pci_root, a.pci)
        rc, _ = run_lifecycle(gpu=gpu, meminfo=a.meminfo, swaps=a.swaps_file,
                              executable=exe, backend=a.backend,
                              seconds=a.seconds, post_samples=post_samples,
                              interval=a.interval)
        return 0 if rc == 0 else 3
    except (OSError, ValueError, TimeoutError) as exc:
        print("V085_LIFECYCLE_REFUSED " + str(exc), file=sys.stderr, flush=True)
        return 4


if __name__ == "__main__":
    sys.exit(main())
