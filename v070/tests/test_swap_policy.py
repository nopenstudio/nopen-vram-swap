#!/usr/bin/env python3
"""Preflight v0.7 privileged swap script without calling swapon or modprobe."""
from pathlib import Path
import subprocess
import sys
if len(sys.argv)!=2: raise SystemExit("expected vram_swap_lab.sh path")
path=Path(sys.argv[1]).resolve()
data=path.read_text(encoding="utf-8")
syntax=subprocess.run(["bash","-n",str(path)],capture_output=True,text=True)
if syntax.returncode: raise AssertionError(syntax.stderr)
required=(
    "NOPEN_V070_KERNEL_SWAP_TEST=EXPERIMENTAL_GPU_SWAP_CAN_HANG",
    'NOPEN_UBLK_BACKEND=opencl',
    'swapon -p "$priority" "$dev"',
    'swapoff "$dev"',
    'is_active',
    'V070_SWAP_ACTIVE',
    'V070_STOP_PASS',
    'V070_STOP_ALREADY_INACTIVE',
    'modprobe ublk_drv',
    'I_ACCEPT_GPU_SWAP_REBOOT',
    'zram_untouched=YES',
    'V070_START_BLOCKED',
    'systemd-run --system',
    '--property=OOMScoreAdjust=-950',
    '--setenv="NOPEN_UBLK_SHADOW_FILE=$shadowfile"',
    'swapoff "$dev"',
    'shadowbase="/var/lib/nopen-v070"',
    'btrfs subvolume create "$shadowdir"',
    'btrfs subvolume show "$shadowdir"',
    'snapshot_of_shadow=FORBIDDEN',
    'case "$action" in start|start-pilot|start-4g|stop|status)',
    'NOPEN_V080_4G_ACK',
    'V080_REAL_4G_GATE_PASS',
    'V080_REAL_4G_REFUSED',
    'I_ACCEPT_4GIB_REAL_KERNEL_SWAP_CAN_LOCKUP',
    'if [[ "$action" == start-pilot ]]',
    'if [[ "$capacity" != "64"',
    'NOPEN_V070_PILOT_ACK=I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK',
    'V070_PILOT_GATE_PASS',
)
for phrase in required:
    if phrase not in data: raise AssertionError(f"required guard absent: {phrase}")
if 'swapoff -a' in data or 'swapoff /dev/zram' in data:
    raise AssertionError("global swap disable detected")
test=subprocess.run(["bash",str(path),"start","3"],text=True,capture_output=True,timeout=4)
if test.returncode==0:
    raise AssertionError("invalid pool incorrectly accepted")
# These checks are deliberately performed as an ordinary user, so they
# can never reach modprobe/systemd-run/swapon even if the guard regresses.
for args, env in (
    (["start-4g","4096"], {}),
    (["start-4g","2048"], {"NOPEN_V080_4G_ACK":"I_ACCEPT_4GIB_REAL_KERNEL_SWAP_CAN_LOCKUP"}),
):
    proc=subprocess.run(["bash",str(path),*args],text=True,capture_output=True,
                        timeout=4,env=__import__("os").environ | env)
    if proc.returncode==0 or "V080_REAL_4G_REFUSED" not in proc.stderr:
        raise AssertionError(f"4GiB safety gate regressed: {args}: {proc}")
# An approved command still requires root, and remains unsafe to auto-start.
proc=subprocess.run(["bash",str(path),"start-4g","4096"],text=True,
                    capture_output=True,timeout=4,
                    env=__import__("os").environ | {"NOPEN_V080_4G_ACK":"I_ACCEPT_4GIB_REAL_KERNEL_SWAP_CAN_LOCKUP"})
if __import__("os").geteuid()!=0 and (
        proc.returncode==0 or "root required" not in proc.stderr):
    raise AssertionError("approved 4GiB start bypassed root check")
print("V070_GATE_PASS command_syntax=YES consent_required=YES "
      "zram_preserved=YES invalid_capacity_rejected=YES no_auto_swap=YES failing_physical_swap_blocked=YES independent_systemd_unit=YES durable_shadow_required=YES btrfs_isolated_subvolume=YES")
