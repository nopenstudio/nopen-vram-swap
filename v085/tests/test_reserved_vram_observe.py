#!/usr/bin/env python3
"""Hardware-free synthetic sampling of sysfs counters."""
from pathlib import Path
from tempfile import TemporaryDirectory
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / "v085/tools"))
import reserved_vram_observe as ob  # noqa: E402

MIB = 1024*1024
with TemporaryDirectory() as tmp:
    root = Path(tmp)
    pci = root / "pci" / "0000:03:00.0"
    pci.mkdir(parents=True)
    (pci / "vendor").write_text("0x1002")
    (pci / "class").write_text("0x030000")
    (pci / "mem_info_vram_total").write_text(str(16304*MIB))
    (pci / "mem_info_vram_used").write_text(str(320*MIB))
    (pci / "mem_info_gtt_used").write_text(str(120*MIB))
    mem = root / "meminfo"
    mem.write_text("MemAvailable: 4608000 kB\n")
    swaps = root / "swaps"
    swaps.write_text("Filename\tType\tSize\tUsed\tPriority\n"
                     "/dev/zram0\tpartition\t7705596\t0\t100\n")
    def advance(_seconds: float) -> None:
        current = int((pci / "mem_info_vram_used").read_text())
        gtt = int((pci / "mem_info_gtt_used").read_text())
        (pci / "mem_info_vram_used").write_text(str(current + 32*MIB))
        (pci / "mem_info_gtt_used").write_text(str(gtt + 16*MIB))
    records = ob.observe(pci,mem,swaps,samples=3,interval_seconds=0,sleep=advance)
    assert len(records)==3
    assert records[0]["vram_used_delta_mib"]==0
    assert records[1]["vram_used_delta_mib"]==32
    assert records[2]["vram_used_delta_mib"]==64
    assert records[2]["gtt_used_delta_mib"]==32
    assert all(not s["physical_residency_or_pin_proven"] for s in records)
    assert all(not s["changes_attributable_to_nopen"] for s in records)
    assert all(s["safety_gate"]=="KERNEL_SWAP_BLOCKED" for s in records)
    swaps.write_text("Filename\tType\tSize\tUsed\tPriority\n"
                     "/dev/ublkb23\tpartition\t1024\t0\t200\n")
    try:
        ob.snapshot(pci,mem,swaps)
    except ValueError as exc:
        assert "swap active" in str(exc)
    else:
        raise AssertionError("Must reject active kernel ublk swap")
    swaps.write_text("Filename\tType\tSize\tUsed\tPriority\n")
    r = subprocess.run([
        sys.executable,str(repo/"v085/tools/reserved_vram_observe.py"),
        "--pci-root",str(root/"pci"),"--meminfo",str(mem),
        "--swaps-file",str(swaps),"--samples","1","--interval-seconds","0"
    ],capture_output=True,text=True,timeout=10)
    assert r.returncode==0,r.stderr
    assert "V085_RESERVED_OBSERVE_PASS" in r.stdout
    assert "kernel_swap=BLOCKED" in r.stdout
print("V085_OBSERVER_FIXTURE_PASS synthetic_samples=3"
      " vram_delta=YES gtt_delta=YES no_attribution=YES"
      " kernel_swap_gate=BLOCKED")
