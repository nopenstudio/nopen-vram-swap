#!/usr/bin/env python3
"""Hardware-free AMDGPU counter fixtures + actual 8MiB MOCK Holder child."""
from pathlib import Path
import json
import subprocess
import sys
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[2]
script = root / "v085/tools/holder_lifecycle.py"
assert len(sys.argv) == 2
holder = Path(sys.argv[1]).resolve()
assert holder.name == "nopen-v085-reserved-holder"
MIB = 1024 * 1024

with TemporaryDirectory() as tmp:
    work = Path(tmp)
    gpu = work / "pci" / "0000:03:00.0"
    gpu.mkdir(parents=True)
    (gpu / "vendor").write_text("0x1002\n")
    (gpu / "class").write_text("0x030000\n")
    (gpu / "mem_info_vram_total").write_text(str(16304 * MIB))
    (gpu / "mem_info_vram_used").write_text(str(400 * MIB))
    (gpu / "mem_info_gtt_used").write_text(str(64 * MIB))
    mem = work / "meminfo"
    mem.write_text("MemAvailable: 4500000 kB\n")
    swaps = work / "swaps"
    swaps.write_text("Filename\tType\tSize\tUsed\tPriority\n"
                     "/dev/zram0\tpartition\t7705596\t0\t100\n")
    common = ["--holder", str(holder), "--pci-root", str(work / "pci"),
              "--meminfo", str(mem), "--swaps-file", str(swaps),
              "--pci", "0000:03:00.0"]
    mock = subprocess.run([
        sys.executable, str(script), *common, "--backend", "mock",
        "--seconds", "0", "--post-seconds", "2", "--interval", "0",
    ], capture_output=True, text=True, timeout=10)
    assert mock.returncode == 0, (mock.returncode, mock.stdout, mock.stderr)
    assert "V085_HOLDER_FULL_ARENA_PASS" in mock.stdout, mock.stdout
    assert "V085_LIFECYCLE_PASS" in mock.stdout, mock.stdout
    assert "post_exit_samples=3" in mock.stdout
    events = [json.loads(row) for row in mock.stdout.splitlines()
              if row.startswith("{")]
    assert len([row for row in events if row["phase"] == "before"]) == 2
    assert len([row for row in events if row["phase"] == "after_exit"]) == 3
    assert all(row["global_vram_delta_vs_baseline_mib"] == 0 for row in events)
    assert all(not row["exclusive_physical_pin_proven"] for row in events)
    assert all(not row["gpu_ownership_attributed"] for row in events)
    assert all(row["safety_gate"] == "KERNEL_SWAP_BLOCKED" for row in events)

    # No explicit opt-in: must refuse *before* fake hardware access.
    invalid = subprocess.run([
        sys.executable, str(script), *common, "--backend", "opencl",
        "--seconds", "15", "--post-seconds", "30", "--interval", "1",
    ], capture_output=True, text=True, timeout=5)
    assert invalid.returncode != 0
    assert "exact ACK" in invalid.stderr
    # Even with the acknowledgment, real mode may not use mock sysfs.
    invalid = subprocess.run([
        sys.executable, str(script), *common, "--backend", "opencl",
        "--seconds", "15", "--post-seconds", "30", "--interval", "1",
        "--ack", "I_ACCEPT_1GIB_LIFECYCLE_GPU_ONLY_NO_SWAP",
    ], capture_output=True, text=True, timeout=5)
    assert invalid.returncode != 0
    assert "alternate synthetic PCI/proc roots" in invalid.stderr

    swaps.write_text("Filename\tType\tSize\tUsed\tPriority\n"
                     "/dev/ublkb0\tpartition\t1024\t0\t200\n")
    refused = subprocess.run([
        sys.executable, str(script), *common, "--backend", "mock",
        "--seconds", "0", "--post-seconds", "2", "--interval", "0",
    ], capture_output=True, text=True, timeout=5)
    assert refused.returncode != 0
    assert "V085_LIFECYCLE_REFUSED" in refused.stderr

source = script.read_text()
assert "swapon(" not in source
assert "swapoff(" not in source
assert "os.system(" not in source
assert "shell=True" not in source
assert '"KERNEL_SWAP_BLOCKED"' in source
print("V085_LIFECYCLE_MOCK_PASS before=2 after_exit=3"
      " real_gpu_without_ack=REFUSED"
      " synthetic_gpu_override=REFUSED"
      " existing_ublk_swap=REFUSED"
      " kernel_swap=BLOCKED")
