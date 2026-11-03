#!/usr/bin/env python3
"""IE-04: deterministic filesystem fixtures; NO physical AMDGPU or kernel swap."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools/ie04_residency_observe.py"
spec = importlib.util.spec_from_file_location("nopen_ie04", SOURCE)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
M = 1024 * 1024
PCI = "0000:03:00.0"
PID = 43210


def expect_refusal(fn, phrase: str = ""):
    try:
        fn()
    except (ValueError, OSError) as exc:
        if phrase:
            assert phrase in str(exc), (phrase, exc)
        return
    raise AssertionError("unsafe/inconsistent evidence unexpectedly accepted")


def setup(root: Path):
    device = root / "pci" / PCI
    device.mkdir(parents=True)
    (device / "vendor").write_text("0x1002\n")
    (device / "class").write_text("0x030000\n")
    def counter(name: str, mib: int):
        (device / name).write_text(str(mib * M) + "\n")
    counter("mem_info_vram_total", 16304)
    counter("mem_info_vram_used", 3000)
    counter("mem_info_gtt_used", 150)
    meminfo = root / "meminfo"
    meminfo.write_text("MemAvailable: 2500000 kB\n")
    swaps = root / "swaps"
    swaps.write_text("Filename\tType\tSize\tUsed\tPriority\n/dev/zram0 partition 1 0 100\n")
    proc = root / "proc"
    fdd = proc / str(PID) / "fdinfo"
    fdd.mkdir(parents=True)
    fields = ["S"] + ["0"] * 25
    fields[19] = "123456"
    (proc / str(PID) / "stat").write_text(
        f"{PID} (opencl (holder)) " + " ".join(fields) + "\n"
    )
    sample = (
        f"drm-driver: amdgpu\n"
        f"drm-pdev: {PCI}\n"
        f"drm-client-id: 7\n"
        "drm-total-vram: 1024 MiB\n"
        "drm-resident-vram: 512 MiB\n"
        "drm-resident-gtt: 64 MiB\n"
        "drm-active-vram: 10 MiB\n"
        "drm-shared-vram: 0 MiB\n"
    )
    (fdd / "3").write_text(sample)
    (fdd / "4").write_text(sample)  # duplicated DRM client descriptor
    (fdd / "5").write_text(
        "drm-driver: amdgpu\n"
        "drm-pdev: 0000:05:00.0\n"
        "drm-client-id: 999\n"
        "drm-resident-vram: 8000 MiB\n"
    )
    return device, proc, meminfo, swaps, counter, fdd


with TemporaryDirectory() as temp:
    root = Path(temp)
    gpu, proc, mem, swaps, write_counter, fdd = setup(root)

    assert mod.select_gpu(root / "pci", PCI) == gpu
    expect_refusal(lambda: mod.select_gpu(root / "pci", "invalid"), "PCI")
    expect_refusal(lambda: mod.select_gpu(root / "pci", "0000:02:00.0"),
                   "exactly one")
    assert mod.proc_starttime(proc, PID) == "123456"
    record = mod.inspect_pid(proc, PID, PCI, "123456")
    assert record["unique_clients"] == 1 and record["duplicate_fds"] == 1
    client = record["clients"][0]
    assert client["resident_vram_bytes"] == 512 * M
    assert client["resident_gtt_bytes"] == 64 * M
    assert client["total_vram_bytes"] == 1024 * M
    assert record["visible_sum_bytes"]["resident_vram_bytes"] == 512 * M
    assert record["sum_is_exclusive_physical_bytes"] is False
    assert record["missing_client_means_no_gpu_memory"] is False
    assert record["all_visible_clients_reported_metric"]["resident_vram_bytes"]
    assert not record["all_visible_clients_reported_metric"]["total_gtt_bytes"]

    # Snapshot 2: correlation of resident VRAM down and GTT up in same DRM
    # client; still NOT proof of movement of the exact same buffer object.
    def change(_):
        write_counter("mem_info_vram_used", 2900)
        write_counter("mem_info_gtt_used", 200)
        new_text = (fdd / "3").read_text().replace("512 MiB", "480 MiB").replace(
            "64 MiB", "96 MiB"
        )
        (fdd / "3").write_text(new_text)
        (fdd / "4").write_text(new_text)

    series = mod.observe(gpu, mem, swaps, proc, PID, 2, 0, sleep=change)
    assert len(series) == 2
    assert not series[0]["possible_bo_migration_observed"]
    assert series[1]["possible_bo_migration_observed"]
    candidate = series[1]["correlated_client_counter_changes"][0]
    assert candidate["client_id"] == 7
    assert candidate["vram_delta_bytes"] == -32 * M
    assert candidate["gtt_delta_bytes"] == 32 * M
    assert series[1]["global_vram_delta_mib"] == -100
    assert series[1]["global_gtt_delta_mib"] == 50
    for sample in series:
        assert sample["gate"] == "KERNEL_SWAP_BLOCKED"
        assert not sample["physical_vram_pinning_proven"]
        assert not sample["bo_migration_proven"]
        assert not sample["particular_opencl_allocation_identified"]
        assert not sample["kernel_swap_authorized"]
        assert not sample["gpu_allocations_started"]
        assert not sample["driver_changed"]

    # Snapshot without a PID is explicitly system-wide, never attributed.
    alone = mod.observe(gpu, mem, swaps, proc, None, 1, 0)
    assert alone[0]["target"] is None
    assert not alone[0]["possible_bo_migration_observed"]

    # Live kernel ublk/NBD swap gate remains enforced on every sample.
    for name in ("/dev/ublkb23", "/dev/nbd0"):
        swaps.write_text(
            "Filename Type Size Used Priority\n"
            f"{name} partition 1 0 100\n"
        )
        expect_refusal(
            lambda: mod.observe(gpu, mem, swaps, proc, PID, 1, 0),
            "active ublk/NBD swap",
        )
    swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 1 0 100\n")

    # Process replacement must be detected even when PID stays unchanged.
    def switch(_):
        stat = proc / str(PID) / "stat"
        stat.write_text(stat.read_text().replace("123456", "987654"))
    expect_refusal(lambda: mod.observe(gpu, mem, swaps, proc, PID, 2, 0,
                                       sleep=switch),
                   "starttime changed")
    # Restore for the rest of tests.
    stat = proc / str(PID) / "stat"
    stat.write_text(stat.read_text().replace("987654", "123456"))

    # Even if DRM fdinfo is inaccessible, missing client data never means 0.
    (fdd / "3").unlink()
    (fdd / "4").unlink()
    partial = mod.inspect_pid(proc, PID, PCI, "123456")
    assert partial["unique_clients"] == 0
    assert partial["visible_sum_bytes"]["resident_vram_bytes"] is None
    assert partial["drm_coverage"] == "NO_MATCHING_DRM_CLIENT"
    assert not partial["missing_client_means_no_gpu_memory"]

    expect_refusal(lambda: mod.observe(gpu, mem, swaps, proc, PID, 0, 1),
                   "outside read-only bounds")
    expect_refusal(lambda: mod.observe(gpu, mem, swaps, proc, PID, 121, 1),
                   "outside read-only bounds")
    expect_refusal(lambda: mod.observe(gpu, mem, swaps, proc, PID, 1, 11),
                   "outside read-only bounds")

    # CLI must be useful on synthetic fixtures and remain read-only.
    result = subprocess.run([
        sys.executable, str(SOURCE), "--pci", PCI,
        "--pci-root", str(root / "pci"), "--proc-root", str(proc),
        "--meminfo", str(mem), "--swaps-file", str(swaps),
        "--pid", str(PID), "--samples", "1", "--interval-seconds", "0",
    ], capture_output=True, text=True, check=False, timeout=5)
    assert result.returncode == 0, result.stderr
    assert "IE04_READONLY_OBSERVATION_PASS" in result.stdout
    assert "physical_pin=UNPROVEN" in result.stdout

# Avoid accidental process creation in production observer; test only.
source_text = SOURCE.read_text(encoding="utf-8")
for forbidden in ("subprocess.", "os.system(", "os.popen(", "os.kill(",
                  "ctypes", "pyopencl", "swapon(", "swapoff(", "mkswap(",
                  "modprobe ", "amdgpu.vramlimit=", "debugfs/"):
    assert forbidden not in source_text, forbidden

print("IE04_RESIDENCY_OBSERVER_FIXTURE_PASS"
      " fdinfo_client_dedup=YES correlated_change_not_migration=YES"
      " pid_reuse_refused=YES active_swap_refused=YES"
      " physical_pin=UNPROVEN kernel_swap=BLOCKED")
