#!/usr/bin/env python3
"""Synthetic /proc fdinfo regression: read-only, no real GPU or processes."""
from pathlib import Path
from tempfile import TemporaryDirectory
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"v085/tools"))
import drm_vram_owners as owners  # noqa: E402

P="0000:03:00.0"
assert owners.parse_size("1048576") == 1048576
assert owners.parse_size("4 MiB") == 4 * 1024 * 1024
assert owners.parse_size("7 KiB") == 7 * 1024
assert owners.parse_size("1 GiB") == 1024 * 1024 * 1024
assert owners.parse_size("-1 MiB") is None
assert owners.parse_size("not-a-number") is None

with TemporaryDirectory() as temp:
    proc=Path(temp)
    def make(pid, fd, text, comm="fixture"):
        d=proc/str(pid)
        (d/"fdinfo").mkdir(parents=True,exist_ok=True)
        (d/"comm").write_text(comm+"\n")
        (d/"fdinfo"/str(fd)).write_text(text)
    first=("drm-driver: amdgpu\n"
           "drm-pdev: 0000:03:00.0\n"
           "drm-client-id: 24\n"
           "drm-resident-vram: 1024 MiB\n"
           "drm-memory-vram: 1024 MiB\n"
           "drm-resident-gtt: 64 MiB\n")
    make(101,3,first,comm="Nopen-Test")
    make(101,4,first,comm="Nopen-Test")  # duplicate FD reference
    make(102,5,("drm-driver: amdgpu\n"
                "drm-pdev: 0000:03:00.0\n"
                "drm-client-id: 25\n"
                "drm-memory-vram: 128 MiB\n"
                "drm-memory-gtt: 1 MiB\n"))
    make(103,3,first.replace(P,"0000:04:00.0"))  # different GPU
    make(104,3,first.replace("drm-pdev: 0000:03:00.0\n",""))  # ambiguous GPU
    make(105,3,first.replace("drm-client-id: 24\n",""))  # missing dedup key
    make(106,3,first.replace("drm-driver: amdgpu","drm-driver: i915"))
    result=owners.scan(proc,P)
    assert result["unique_drm_clients_found"]==2,result
    assert [x["client_id"] for x in result["clients"]]==[24,25]
    assert result["clients"][0]["resident_vram_mib"]==1024
    assert result["clients"][1]["resident_vram_mib"]==128
    assert result["clients"][0]["resident_gtt_mib"]==64
    assert result["clients_may_share_backing_memory"] is True
    assert result["per_client_counters_prove_exclusive_physical_residency"] is False
    assert result["missing_clients_prove_no_gpu_usage"] is False
    assert result["kernel_swap_authorized"] is False
    assert result["safety_gate"]=="KERNEL_SWAP_BLOCKED"
    assert len(owners.scan(proc,P,limit=1)["clients"])==1
    try:
        owners.scan(proc,"../../etc/passwd")
    except ValueError:
        pass
    else:
        raise AssertionError("PCI validation must fail closed")
    x=subprocess.run([
        sys.executable,str(ROOT/"v085/tools/drm_vram_owners.py"),
        "--proc-root",str(proc),"--pci",P,"--top","2"
    ],capture_output=True,text=True,timeout=5)
    assert x.returncode==0,(x.stdout,x.stderr)
    assert "V085_DRM_OWNERS_PASS" in x.stdout
    assert "kernel_swap=BLOCKED" in x.stdout
print("V085_DRM_OWNERS_FIXTURE_PASS dedup=YES"
      " pci_filter=YES alias_not_double_counted=YES"
      " no_privileged_access=YES kernel_swap=BLOCKED")
