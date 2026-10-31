#!/usr/bin/env python3
"""Static guard: v0.8.5 is budgeting and observing, not kernel memory partitioning."""
from pathlib import Path
root = Path(__file__).resolve().parents[2]
read = lambda p: (root/p).read_text(encoding="utf-8")
p=read("v085/tools/reserved_vram_preflight.py")
o=read("v085/tools/reserved_vram_observe.py")
ci=read(".github/workflows/v085-reserved-vram-mock.yml")
for marker in (
    '"application_vram_capacity_modified": False',
    '"driver_vram_partition_created": False',
    '"physical_vram_residency_guaranteed": False',
    '"gpu_memory_allocated": False',
    '"kernel_swap_authorized": False',
    '"safety_gate": "KERNEL_SWAP_BLOCKED"',
):
    assert marker in p, marker
for marker in (
    '"physical_residency_or_pin_proven": False',
    '"changes_attributable_to_nopen": False',
    '"kernel_swap_authorized": False',
    '"safety_gate": "KERNEL_SWAP_BLOCKED"',
):
    assert marker in o, marker
for source in (p,o):
    for forbidden in (
        "subprocess.run(", "subprocess.Popen(", "os.system(",
        "clCreateBuffer(", "clEnqueue", "swapon(", "swapoff(",
        "mkswap ", "modprobe ", "amdgpu.vramlimit=", "systemctl "):
        assert forbidden not in source, forbidden
assert "NOPEN_OPENCL=OFF" in ci
assert "ctest --test-dir build-v085-mock -R '^v085_'" in ci
print("V085_RESERVED_SAFETY_PASS sysfs_readonly=YES app_vram_cap=UNCHANGED"
      " physical_gpu_pin=UNPROVEN kernel_swap=BLOCKED")
