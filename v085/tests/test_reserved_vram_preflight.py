#!/usr/bin/env python3
"""All synthetic sysfs/proc fixtures. No hardware or privileges needed."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
script = root / "v085/tools/reserved_vram_preflight.py"
spec = spec_from_file_location("v085_reserved_preflight", script)
assert spec and spec.loader
mod = module_from_spec(spec)
spec.loader.exec_module(mod)
MIB = 1024 * 1024


def fixture(root: Path, used_mib: int = 420,
            available_mib: int = 4500,
            swap: str = "/dev/zram0") -> tuple[Path, Path, Path]:
    pci_root = root / "pci"
    card = pci_root / "0000:03:00.0"
    card.mkdir(parents=True)
    (card / "vendor").write_text("0x1002\n")
    (card / "class").write_text("0x030000\n")
    (card / "mem_info_vram_total").write_text(str(16304 * MIB))
    (card / "mem_info_vram_used").write_text(str(used_mib * MIB))
    (card / "mem_info_gtt_used").write_text(str(256 * MIB))
    mem = root / "meminfo"
    mem.write_text(f"MemTotal: 7706392 kB\nMemAvailable: {available_mib*1024} kB\n")
    swaps = root / "swaps"
    swaps.write_text(
        "Filename\tType\tSize\tUsed\tPriority\n" +
        f"{swap}\tpartition\t7705596\t599864\t100\n"
    )
    return pci_root, mem, swaps


with TemporaryDirectory() as tmp:
    r = Path(tmp)
    p, m, s = fixture(r)
    ok = mod.evaluate(p, m, s)
    assert ok["status"] == "USERSPACE_EXPERIMENT_ELIGIBLE", ok
    assert ok["requested_pool_mib"] == 4096
    assert ok["hypothetical_after_pool_mib"] == 12208
    assert ok["free_after_pool_mib"] == 11788
    assert ok["safety_gate"] == "KERNEL_SWAP_BLOCKED"
    assert ok["physical_vram_residency_guaranteed"] is False
    assert ok["application_vram_capacity_modified"] is False
    assert ok["gpu_memory_allocated"] is False
    assert ok["kernel_swap_authorized"] is False
    assert ok["active_swap_devices"] == ["/dev/zram0"]

    # A 4GiB reserve on a busy 16GiB GPU is denied.
    (p / "0000:03:00.0" / "mem_info_vram_used").write_text(str(13000 * MIB))
    busy = mod.evaluate(p, m, s)
    assert "INSUFFICIENT_VRAM_HEADROOM" in busy["reasons"]
    (p / "0000:03:00.0" / "mem_info_vram_used").write_text(str(420 * MIB))

    m.write_text("MemAvailable: 512000 kB\n")
    assert "HOST_RAM_HEADROOM_TOO_LOW" in mod.evaluate(p,m,s)["reasons"]
    m.write_text("MemAvailable: 4608000 kB\n")

    s.write_text("Filename\tType\tSize\tUsed\tPriority\n"
                 "/dev/ublkb23\tpartition\t4194304\t0\t200\n")
    assert "GPU_OR_NBD_KERNEL_SWAP_ALREADY_ACTIVE" in mod.evaluate(p,m,s)["reasons"]
    s.write_text("Filename\tType\tSize\tUsed\tPriority\n")

    second = p / "0000:0a:00.0"
    second.mkdir()
    for key in ("vendor","class","mem_info_vram_total",
                "mem_info_vram_used","mem_info_gtt_used"):
        (second / key).write_text((p / "0000:03:00.0" / key).read_text())
    assert "VRAM_PROBE_FAILED" in mod.evaluate(p,m,s)["reasons"]
    explicit = mod.evaluate(p,m,s,pci="0000:03:00.0")
    assert explicit["status"] == "USERSPACE_EXPERIMENT_ELIGIBLE"
    assert "HEADROOM_BELOW_V085_POLICY_MINIMUM" in mod.evaluate(
        p,m,s,min_free_mib=1024)["reasons"]
    assert "UNSUPPORTED_POOL_SIZE" in mod.evaluate(p,m,s,pool_mib=320,pci="0000:03:00.0")["reasons"]
    (p / "0000:03:00.0" / "mem_info_vram_used").unlink()
    assert "VRAM_PROBE_FAILED" in mod.evaluate(p,m,s,pci="0000:03:00.0")["reasons"]

    # CLI also needs a zero-privilege, synthetic-sysfs success case.
    (p / "0000:03:00.0" / "mem_info_vram_used").write_text(str(420*MIB))
    completed = subprocess.run([
        sys.executable, str(script), "--pci-root",str(p),
        "--meminfo",str(m),"--swaps-file",str(s),
        "--pci","0000:03:00.0"
    ],capture_output=True,text=True,timeout=10)
    assert completed.returncode == 0, completed.stderr
    assert "V085_RESERVED_PREFLIGHT_PASS" in completed.stdout
    assert "kernel_swap=BLOCKED" in completed.stdout

print("V085_PREFLIGHT_FIXTURE_PASS boot_budget=YES"
      " insufficient_headroom_denied=YES multi_gpu_ambiguous_denied=YES"
      " live_gpu_swap_denied=YES no_device_changes=YES")
