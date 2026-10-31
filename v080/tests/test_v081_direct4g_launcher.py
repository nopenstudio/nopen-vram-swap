#!/usr/bin/env python3
"""Static safety regression: no host SSD shadow in direct 4GiB launcher."""
from pathlib import Path

root=Path(__file__).resolve().parents[2]
launch=(root/"v080/tools/v081_direct4g_kernel_lab.sh").read_text()
target=(root/"v070/experimental/ublk/nopen_ublk_swap_target.cpp").read_text()
store=(root/"v012/src/nopen_store.c").read_text()
block=(root/"v050/src/nopen_blk_backend.c").read_text()
old=(root/"v070/tools/vram_swap_lab.sh").read_text()
checks={
    "separate_from_old_start4g": "NOPEN_UBLK_STORAGE_MODE=vram-direct" not in old,
    "direct_not_shadow": "--setenv=NOPEN_UBLK_STORAGE_MODE=vram-direct" in launch
                         and "--setenv=NOPEN_UBLK_SHADOW_FILE" not in launch
                         and 'shadowfile=' not in launch,
    "explicit_user_risk_gate": "NOPEN_V081_DIRECT4G_ACK" in launch
                          and "read -r approval </dev/tty" in launch,
    "no_system_swapoff": "exec swapoff" not in launch and
                         "swapoff -a" not in launch,
    "zram_remains": "swapoff /dev/zram0" not in launch and "zramctl" not in launch,
    "device_id_fixed": "dev=/dev/ublkb23" in launch,
    "prefill_required": "NOPEN_VRAM_DIRECT_PREFILL=1" in launch
                        and "NOPEN_VRAM_DIRECT_PREFILL_PASS" in launch
                        and "clEnqueueFillBuffer" in store,
    "critical_worker_metadata_pinned": "NOPEN_DIRECT_METADATA_MLOCK_PASS" in launch
                              and "mlock(b->meta" in block,
    "virtual_swap_ack_requires_optin": "NOPEN_VRAM_DIRECT_ACK" in launch and
                                        "I_ACCEPT_VOLATILE_GPU_ONLY_SWAP_CAN_LOSE_PAGES_AND_LOCKUP" in target,
    "only_nopen_swapon": 'swapon -p "$priority" "$dev"' in launch,
    "lowered_priority": "priority=100" in launch,
    "stop_reuses_safe_lifecycle": 'vram_swap_lab.sh" stop' in launch,
    "old_shadow_script_unchanged": "--setenv=\"NOPEN_UBLK_SHADOW_FILE=$shadowfile\"" in old,
}
for name,passed in checks.items():
    assert passed,f"V081_DIRECT4G_SAFETY_FAIL check={name}"
print(f"V081_DIRECT4G_SAFETY_PASS checks={len(checks)}"
      " kernel_swap=NOT_ACTIVATED disks_untouched=YES"
      " existing_zram_untouched=YES")
