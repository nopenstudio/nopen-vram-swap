#!/usr/bin/env python3
"""IE-04/A1 REWORK F1-F5 negative regressions: NO OpenCL, GPU, or swap.

These tests were added following audit FAIL #38. In particular a real CPU
subprocess exits leaving a child holding stdout open; own-PGID cleanup must
kill it. Other cases are deterministic mock/synthetic host state.
"""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "v080/tools/ie04_a1_runner.py"
spec = importlib.util.spec_from_file_location("ie04_a1_rework_runner", SCRIPT)
assert spec and spec.loader
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)
MIB = 1024 * 1024
PCI = "0000:03:00.0"


def fixture(root: Path):
    devices = root / "pci"
    gpu = devices / PCI
    gpu.mkdir(parents=True)
    (gpu / "vendor").write_text("0x1002\n")
    (gpu / "class").write_text("0x030000\n")
    (gpu / "mem_info_vram_total").write_text(str(16304 * MIB))
    (gpu / "mem_info_vram_used").write_text(str(500 * MIB))
    (gpu / "mem_info_gtt_used").write_text(str(100 * MIB))
    (gpu / "gpu_busy_percent").write_text("0\n")
    meminfo = root / "meminfo"
    meminfo.write_text("MemAvailable: 3200000 kB\n")
    swaps = root / "swaps"
    swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 64 0 100\n")
    proc = root / "proc"
    proc.mkdir()
    return devices, gpu, meminfo, swaps, proc


def args(devices, meminfo, swaps, proc, *extras):
    return ["--pci", PCI,
            "--pci-root", str(devices),
            "--meminfo", str(meminfo),
            "--swaps-file", str(swaps),
            "--proc-root", str(proc),
            *extras]


def invoke(argsv):
    capture = io.StringIO()
    with contextlib.redirect_stdout(capture):
        status = runner.main(argsv, get_euid=lambda: 1000)
    return status, capture.getvalue()


# F1: child CPU process survives the parent; both share the group and stdout
# pipe. The parent has *already exited* before abort is called. Cleanup must
# signal the previously created dedicated process group and bound pipe waits.
# try/finally in test ensures even the OLD BUG can never leave an orphan behind.
child_code = (
    "import signal,time\n"
    "signal.signal(signal.SIGTERM,signal.SIG_IGN)\n"
    "print('CHILD_READY',flush=True)\n"
    "time.sleep(30)\n"
)
parent_code = (
    "import subprocess,sys\n"
    f"subprocess.Popen([sys.executable,'-c',{child_code!r}])\n"
)
p = subprocess.Popen([sys.executable, "-c", parent_code], text=True,
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     start_new_session=True)
try:
    assert p.stdout is not None
    assert p.stdout.readline().strip() == "CHILD_READY"
    assert p.wait(timeout=4) == 0
    begun = time.monotonic()
    stdout, stderr = runner.abort_owned_benchmark(p)
    assert "CHILD_READY" in stdout
    assert time.monotonic() - begun < 8, "pipe cleanup unexpectedly unbounded"
    try:
        os.killpg(p.pid, 0)
    except ProcessLookupError:
        pass
    else:
        # A process group with a live, SIGTERM-ignoring child would be here.
        raise AssertionError("F1: orphan worker process group remains alive")
finally:
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        p.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        if p.stdout:
            p.stdout.close()
        if p.stderr:
            p.stderr.close()

with TemporaryDirectory() as tmp:
    root = Path(tmp)
    devices, gpu, meminfo, swaps, proc = fixture(root)
    argv = args(devices, meminfo, swaps, proc, "--skip-build")

    # F2: first 4KiB returns FAIL (not exception); 64KiB must NEVER start.
    first_calls = []
    real_build = runner.build_once
    real_bench = runner.benchmark_once
    runner.build_once = lambda a: (Path("/mock/bench"), Path("/mock/worker"))
    def failing_first(a, bench, worker, kib):
        first_calls.append(kib)
        return {"io_kib": kib, "status": "FAIL", "failure": "simulated",
                "metrics": "UNKNOWN",
                "drm_counters_complete_during_benchmark": False}
    runner.benchmark_once = failing_first
    try:
        status, report = invoke(argv)
        result = json.loads(next(x for x in report.splitlines() if x.startswith("{")))
        assert status != 0 and first_calls == [4], (status, first_calls)
        assert [r["status"] for r in result["runs"]] == [
            "FAIL", "NOT_STARTED_DUE_TO_PRIOR_FAILURE"
        ]
    finally:
        runner.build_once = real_build
        runner.benchmark_once = real_bench

    # F3: even with a matching BDF in sysfs, the legacy v012 worker picks the
    # first discrete GPU. A1 cannot bind it: explicit BLOCKED before allocation.
    second = devices / "0000:05:00.0"
    second.mkdir()
    (second / "vendor").write_text("0x1002\n")
    (second / "class").write_text("0x030000\n")
    for name, value in (("mem_info_vram_total", 8192 * MIB),
                        ("mem_info_vram_used", 100 * MIB),
                        ("mem_info_gtt_used", 50 * MIB)):
        (second / name).write_text(str(value))
    opencl = runner.make_parser().parse_args(
        args(devices, meminfo, swaps, proc, "--backend", "opencl",
             "--real-opencl-ack", runner.APPROVAL))
    assert "OPENCL_DEVICE_PCI_IDENTITY_UNVERIFIED" in runner.policy(
        opencl, get_euid=lambda: 1000)["reasons"]
    second.rename(devices / "0000:05:00.1")
    assert "OPENCL_DEVICE_PCI_IDENTITY_UNVERIFIED" in runner.policy(
        opencl, get_euid=lambda: 1000)["reasons"]

    # F4a: simulate resource change DURING CMake build. The second policy
    # gate must refuse prior to calling even a mocked benchmark function.
    did_run = []
    runner.build_once = lambda a: (
        meminfo.write_text("MemAvailable: 500000 kB\n") or
        (Path("/mock/bench"), Path("/mock/worker"))
    )
    runner.benchmark_once = lambda *a: did_run.append(True)
    try:
        result_code, _ = invoke(argv)
        assert result_code != 0 and not did_run, (
            result_code, did_run)
    finally:
        runner.build_once = real_build
        runner.benchmark_once = real_bench
    meminfo.write_text("MemAvailable: 3200000 kB\n")

    # F4b: even after a passing first run, a newly activated NBD swap
    # blocks the second run; no 64KiB process may begin.
    second_calls = []
    runner.build_once = lambda a: (Path("/mock/bench"), Path("/mock/worker"))
    def first_then_swap(a, bench, worker, kib):
        second_calls.append(kib)
        swaps.write_text(
            "Filename Type Size Used Priority\n"
            "/dev/nbd0 partition 64 0 100\n")
        return {"io_kib": kib, "status": "PASS", "failure": None,
                "metrics": {"evidence_level": "MOCK_ONLY"},
                "drm_counters_complete_during_benchmark": False}
    runner.benchmark_once = first_then_swap
    try:
        result_code, _ = invoke(argv)
        assert result_code != 0 and second_calls == [4], second_calls
    finally:
        runner.build_once = real_build
        runner.benchmark_once = real_bench
        swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 64 0 100\n")

    # F4c: arbitrary skip-build binary cannot be used for real GPU mode,
    # even when a manual ACK was supplied.
    opencl_args = runner.make_parser().parse_args(
        args(devices, meminfo, swaps, proc, "--backend", "opencl",
             "--real-opencl-ack", runner.APPROVAL, "--skip-build"))
    assert "REAL_OPENCL_SKIP_BUILD_FORBIDDEN" in runner.policy(
        opencl_args, get_euid=lambda: 1000)["reasons"]

    # F5: exactly one complete sample followed by unknown MUST NOT count
    # as accepted evidence. Require *entire* process + worker identity
    # continuity and full four-counter coverage across multiple samples.
    complete = {
        "bench_pid": 700, "bench_starttime_ticks": "11",
        "worker_pid": 701, "worker_starttime_ticks": "22",
        "drm_evidence": "COMPLETE_CLIENT_COUNTERS_NOT_PHYSICAL_BO_PROOF",
        **{f"drm_{metric}_{region}_mib": 2.0
           for metric in ("resident", "total")
           for region in ("vram", "gtt")}
    }
    unknown = dict(complete, drm_evidence="UNKNOWN_DRM_COUNTERS",
                   drm_total_vram_mib="UNKNOWN")
    changed = dict(complete, worker_starttime_ticks="33")
    assert runner.assess_drm_coverage([complete, unknown])["acceptance"].startswith("BLOCKED")
    assert runner.assess_drm_coverage([complete, changed])["acceptance"].startswith("BLOCKED")
    assert runner.assess_drm_coverage([complete])["acceptance"].startswith("BLOCKED")
    good = runner.assess_drm_coverage([complete, dict(complete)])
    assert good["acceptance"] == "COUNTERS_ONLY_NOT_BO_PIN_PROOF"
    assert good["all_samples_complete"] is True
    assert good["physical_vram_pin_proven"] is False

    # Hardware execution is never imported by this test.
    assert "v012/src/nopen_store.c" not in str(SCRIPT)
    assert "nopen_durable_write" not in SCRIPT.read_text()

print("IE04_A1_REWORK_F1_F5_PASS orphan_group_cleanup=BOUNDED"
      " fail_fast=YES pci_binding_unverified=BLOCKED"
      " build_race=BLOCKED second_run_race=BLOCKED"
      " real_skip_build=FORBIDDEN partial_drm=BLOCKED"
      " evidence=MOCK_ONLY kernel_swap=BLOCKED")
