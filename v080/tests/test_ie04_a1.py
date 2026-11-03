#!/usr/bin/env python3
"""IE-04/A1 mock-only policy and 256MiB direct benchmark integration tests.

NO OpenCL initialization, no GPU pressure, no ublk/NBD/swap, no shell sudo.
All preflight negatives use synthetic sysfs and proc fixtures. CI launches
ONLY the existing C binary with --backend mock (256MiB, 4KiB and 64KiB).
"""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "v080/tools/ie04_a1_runner.py"
SHELL = ROOT / "v080/tools/test_alpha10_vram_direct.sh"
BENCH_SRC = ROOT / "v080/src/nopen_vram_direct_bench.c"
PCI = "0000:03:00.0"
MIB = 1024 * 1024

spec = importlib.util.spec_from_file_location("ie04_a1_runner", RUNNER)
assert spec and spec.loader
runner = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = runner
spec.loader.exec_module(runner)


def fake_stat(pid: int, parent: int, start: int) -> str:
    fields = ["S"] + ["0"] * 24
    fields[1] = str(parent)
    fields[19] = str(start)
    return f"{pid} (gpu worker (test)) " + " ".join(fields) + "\n"


def provision(root: Path):
    pci_root = root / "pci"
    device = pci_root / PCI
    device.mkdir(parents=True)
    (device / "vendor").write_text("0x1002\n")
    (device / "class").write_text("0x030000\n")
    (device / "mem_info_vram_total").write_text(str(16304 * MIB))
    (device / "mem_info_vram_used").write_text(str(600 * MIB))
    (device / "mem_info_gtt_used").write_text(str(125 * MIB))
    (device / "gpu_busy_percent").write_text("3\n")
    (device / "current_link_speed").write_text("16.0 GT/s PCIe\n")
    (device / "current_link_width").write_text("16\n")
    meminfo = root / "meminfo"
    meminfo.write_text("MemAvailable: 3000000 kB\n")
    swaps = root / "swaps"
    swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 65536 0 100\n")
    return pci_root, device, meminfo, swaps


def argv(pci_root: Path, meminfo: Path, swaps: Path, *options: str):
    return ["--pci", PCI, "--pci-root", str(pci_root),
            "--meminfo", str(meminfo), "--swaps-file", str(swaps),
            *options]


with TemporaryDirectory() as dirname:
    root = Path(dirname)
    pci_root, gpu, meminfo, swaps = provision(root)
    fake_args = runner.make_parser().parse_args(argv(pci_root, meminfo, swaps))
    policy = runner.policy(fake_args, get_euid=lambda: 1000)
    assert policy["status"] == "ELIGIBLE_FOR_LOCAL_NO_SWAP_BENCH", policy
    assert policy["backend"] == "mock"
    assert policy["capacity_mib"] == 256
    assert policy["requested_io_kib"] == [4, 64]
    assert policy["ssd_data_io"] == "NONE"
    assert policy["kernel_swap"] == "BLOCKED"
    assert policy["physical_vram_pin_proven"] is False
    assert "ROOT_FORBIDDEN" in runner.policy(fake_args,
                                            get_euid=lambda: 0)["reasons"]

    bad = runner.make_parser().parse_args(
        argv(pci_root, meminfo, swaps, "--capacity-mib", "4096"))
    assert "EXACT_256_MIB_ONLY" in runner.policy(
        bad, get_euid=lambda: 1000)["reasons"]
    bad = runner.make_parser().parse_args(
        argv(pci_root, meminfo, swaps, "--backend", "opencl"))
    refusal = runner.policy(bad, get_euid=lambda: 1000)["reasons"]
    assert "REAL_OPENCL_REQUIRES_EXPLICIT_MANUAL_ACK" in refusal
    assert "REAL_GPU_MUST_USE_REAL_HOST_SENSORS" in refusal
    bad = runner.make_parser().parse_args(
        argv(pci_root, meminfo, swaps, "--backend", "opencl",
             "--real-opencl-ack", runner.APPROVAL))
    assert "REAL_GPU_MUST_USE_REAL_HOST_SENSORS" in runner.policy(
        bad, get_euid=lambda: 1000)["reasons"]

    bad = runner.make_parser().parse_args(
        argv(pci_root, meminfo, swaps).replace(PCI, "0000:05:00.0"))
    assert "VRAM_PROBE_FAILED" in runner.policy(
        bad, get_euid=lambda: 1000)["reasons"]

    swaps.write_text(
        "Filename Type Size Used Priority\n"
        "/dev/ublkb23 partition 1024 0 100\n"
    )
    assert "GPU_OR_NBD_KERNEL_SWAP_ALREADY_ACTIVE" in runner.policy(
        fake_args, get_euid=lambda: 1000)["reasons"]
    swaps.write_text("Filename Type Size Used Priority\n/dev/nbd0 partition 8 0 100\n")
    assert "GPU_OR_NBD_KERNEL_SWAP_ALREADY_ACTIVE" in runner.policy(
        fake_args, get_euid=lambda: 1000)["reasons"]
    swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 65536 0 100\n")
    (gpu / "mem_info_vram_used").write_text(str(15000 * MIB))
    assert "INSUFFICIENT_VRAM_HEADROOM" in runner.policy(
        fake_args, get_euid=lambda: 1000)["reasons"]
    (gpu / "mem_info_vram_used").write_text(str(600 * MIB))
    meminfo.write_text("MemAvailable: 700000 kB\n")
    assert "HOST_RAM_HEADROOM_TOO_LOW" in runner.policy(
        fake_args, get_euid=lambda: 1000)["reasons"]
    meminfo.write_text("MemAvailable: 3000000 kB\n")

    # Static, non-GPU worker & PID fixture. The benchmark itself has no DRM
    # fdinfo; ONLY its GPU-owning child should be counted.
    proc = root / "proc"
    parent, child = 3001, 3002
    worker = Path("/fake/nopen-isolated-worker")
    (proc / str(parent) / "task" / str(parent)).mkdir(parents=True)
    (proc / str(parent) / "task" / str(parent) / "children").write_text(
        str(child) + "\n")
    (proc / str(parent) / "stat").write_text(fake_stat(parent, 1, 1122))
    (proc / str(parent) / "status").write_text("VmRSS: 512 kB\n")
    (proc / str(child) / "fdinfo").mkdir(parents=True)
    (proc / str(child) / "stat").write_text(fake_stat(child, parent, 5566))
    (proc / str(child) / "status").write_text("VmRSS: 16384 kB\n")
    (proc / str(child) / "cmdline").write_bytes(
        (str(worker) + "\0mock\04\0").encode())
    fdinfo = proc / str(child) / "fdinfo" / "3"
    fdinfo.write_text(
        f"drm-driver: amdgpu\n"
        f"drm-pdev: {PCI}\n"
        "drm-client-id: 7\n"
        "drm-resident-vram: 80 MiB\n"
        "drm-total-vram: 256 MiB\n"
        "drm-resident-gtt: 4 MiB\n"
        "drm-total-gtt: 6 MiB\n"
    )
    snap = runner.record_snapshot(
        pci_root, meminfo, swaps, proc, PCI, parent, "1122", worker)
    assert snap["worker_pid"] == child, snap
    assert snap["drm_pid"] == child
    assert snap["drm_evidence"] == "COMPLETE_CLIENT_COUNTERS_NOT_PHYSICAL_BO_PROOF"
    assert snap["drm_resident_vram_mib"] == 80.0
    assert snap["drm_total_vram_mib"] == 256.0
    assert snap["bench_rss_mib"] == 0.5
    assert snap["worker_rss_mib"] == 16.0
    assert snap["physical_vram_pin_proven"] is False

    fdinfo.write_text(
        f"drm-driver: amdgpu\n"
        f"drm-pdev: {PCI}\n"
        "drm-client-id: 7\n"
        "drm-resident-vram: 80 MiB\n"
        "drm-resident-gtt: 4 MiB\n"
    )
    snap = runner.record_snapshot(
        pci_root, meminfo, swaps, proc, PCI, parent, "1122", worker)
    assert snap["drm_evidence"] == "UNKNOWN_DRM_COUNTERS"
    assert snap["drm_total_vram_mib"] == "UNKNOWN"
    fdinfo.unlink()
    snap = runner.record_snapshot(
        pci_root, meminfo, swaps, proc, PCI, parent, "1122", worker)
    assert snap["drm_evidence"].startswith("UNKNOWN")
    assert snap["drm_resident_vram_mib"] == "UNKNOWN"
    (proc / str(child) / "stat").write_text(fake_stat(child, parent, 8888))
    assert runner.child_worker(proc, parent, worker) == child
    (proc / str(parent) / "stat").write_text(fake_stat(parent, 1, 3333))
    try:
        runner.record_snapshot(pci_root, meminfo, swaps, proc, PCI,
                               parent, "1122", worker)
    except ValueError as exc:
        assert "PID identity changed" in str(exc)
    else:
        raise AssertionError("PID reuse must refuse accounting")

    # Mock vs real cannot be mixed into the same evidence tier.
    assert runner.parse_pass(
        "NOPEN_VRAM_DIRECT_PASS backend=mock capacity_mib=256 io_kib=4 "
        "shadow_file=NONE ssd_write_through=NO ssd_data_io=NONE "
        "kernel_swap=NO spread_across_vram=YES "
        "benchmark_data_authority=VRAM_ONLY physical_vram_pin=UNPROVEN "
        "evidence_level=MOCK_ONLY read_operations=4 "
        "checksum_verified_reads=4 zero_page_checks=1 segment_edge_checks=3 "
        "write_p50_us=10 write_p95_us=11 write_p99_us=12 "
        "read_p50_us=10 read_p95_us=11 read_p99_us=12 "
        "write_mib_s=1 read_mib_s=1 write_iops=200 read_iops=200",
        4, "mock")["evidence_level"] == "MOCK_ONLY"
    try:
        runner.parse_pass(
            "NOPEN_VRAM_DIRECT_PASS backend=mock capacity_mib=256",
            4, "opencl")
    except ValueError:
        pass
    else:
        raise AssertionError("mock evidence must not count as real GPU")

    # Executable runner rejects 4096 MiB BEFORE starting build or GPU.
    if os.geteuid() != 0:
        args_bad = [sys.executable, str(RUNNER),
                    *argv(pci_root, meminfo, swaps, "--capacity-mib", "4096",
                          "--dry-run")]
        p = subprocess.run(args_bad, text=True, capture_output=True, timeout=8)
        assert p.returncode == 2 and "EXACT_256_MIB_ONLY" in p.stderr, p

    assert SHELL.read_text().count('ie04_a1_runner.py') == 1
    assert "4096" not in SHELL.read_text()
    bench_source = BENCH_SRC.read_text()
    assert "nopen_isolated_write(gpu" in bench_source
    assert "nopen_isolated_read(gpu" in bench_source
    assert "nopen_page_checksum(actual,bytes)" in bench_source
    assert "write_p99_us" in bench_source and "read_p99_us" in bench_source
    for forbidden in ("nopen_shadow_write(", "nopen_shadow_read(",
                      "nopen_durable_write(", "nopen_durable_open(",
                      "swapon(", "swapoff(", "mkswap(", "clCreateBuffer("):
        assert forbidden not in bench_source
    assert "run_alpha2_gpu.sh" not in RUNNER.read_text()
    assert "KERNEL_SWAP_BLOCKED" in RUNNER.read_text()

    if len(sys.argv) > 1:
        assert len(sys.argv) == 3, "expected benchmark and worker executable"
        bench_bin = Path(sys.argv[1])
        worker_bin = Path(sys.argv[2])
        assert bench_bin.is_file() and worker_bin.is_file()
        if os.geteuid() == 0:
            raise AssertionError("mock benchmark must run non-root")
        # Same fake GPU counters, but a REAL --backend mock C worker.
        command = [sys.executable, str(RUNNER),
                   *argv(pci_root, meminfo, swaps),
                   "--backend", "mock", "--io-kib", "both",
                   "--operations", "500", "--skip-build",
                   "--bench-bin", str(bench_bin),
                   "--worker-bin", str(worker_bin),
                   "--max-seconds", "60"]
        p = subprocess.run(command, capture_output=True, text=True,
                           timeout=140, check=False)
        assert p.returncode == 0, (p.returncode, p.stdout[-5000:], p.stderr)
        records = [x for x in p.stdout.splitlines() if x.startswith("{")]
        assert len(records) == 1, p.stdout
        result = json.loads(records[0])
        assert result["status"] == "PASS"
        assert result["evidence_level"] == "MOCK_ONLY"
        assert result["residency_acceptance"] == "BLOCKED_MOCK_HAS_NO_PHYSICAL_GPU_EVIDENCE"
        assert result["kernel_swap"] == "NO"
        assert result["ssd_data_io"] == "NONE"
        assert len(result["runs"]) == 2
        assert [r["io_kib"] for r in result["runs"]] == [4, 64]
        for item in result["runs"]:
            assert item["status"] == "PASS", item
            attrs = item["metrics"]
            assert attrs["capacity_mib"] == "256"
            assert attrs["spread_across_vram"] == "YES"
            assert int(attrs["checksum_verified_reads"]) >= 1024
            assert int(attrs["zero_page_checks"]) > 0
            assert int(attrs["segment_edge_checks"]) > 0
            assert float(attrs["write_p99_us"]) > 0
            assert float(attrs["read_p99_us"]) > 0
            assert item["physical_vram_allocation_proven"] is False

print("IE04_A1_MOCK_GUARD_PASS root_denied=YES swap_denied=YES"
      " low_vram_denied=YES low_ram_denied=YES wrong_pci_denied=YES"
      " four_gib_denied=YES drm_missing=UNKNOWN"
      " pid_reuse_denied=YES backend_evidence_separate=YES"
      " capacity_256m_integrity=YES io_4k_64k=YES"
      " actual_gpu=NOT_TESTED kernel_swap=BLOCKED")
