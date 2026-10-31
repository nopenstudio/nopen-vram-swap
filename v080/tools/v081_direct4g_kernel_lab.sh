#!/usr/bin/env bash
# HIGH-RISK PHYSICAL EXPERIMENT: real 4GiB Linux kernel swap in GPU VRAM.
# ONLY Nopen's SWAP data path changes; host disk, partitions and zram untouched.
# No SSD shadow; any GPU failure can destroy live swapped pages.
# Do not run on a system with unsaved work or without verified backups.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
action="${1:-preflight}"
dev=/dev/ublkb23
id=23
service=nopen-v070-ublk.service
state=/run/nopen-v070
logdir=/var/log/nopen-v070
target="$repo/build-v070/nopen-v070-ublk"
controller="$repo/build-v070/ublksrv-upstream/ublk"
worker="$repo/build-v070/v060/nopen-isolated-worker"
ACK=I_ACCEPT_VOLATILE_GPU_ONLY_SWAP_CAN_LOSE_PAGES_AND_LOCKUP
START_ACK=I_ACCEPT_REAL_4G_GPU_SWAP_MAY_FREEZE_AND_LOSE_UNSAVED_DATA
priority=100
is_active() {
  swapon --noheadings --raw --show=NAME 2>/dev/null | grep -Fxq "$dev"
}
case "$action" in
  status)
    bash "$repo/v070/tools/vram_swap_lab.sh" status
    exit 0;;
  stop)
    if (( EUID!=0 ));then
      echo "V081_DIRECT4G_REFUSED stop needs sudo";exit 2
    fi
    exec bash "$repo/v070/tools/vram_swap_lab.sh" stop;;
  preflight)
    python3 "$repo/v080/tools/v081_direct4g_preflight.py"
    echo "V081_DIRECT4G_PREFLIGHT_NOTE physical host swapon NOT authorized by this check"
    exit 0;;
  start) :;;
  *)
    echo "Usage: bash v080/tools/v081_direct4g_kernel_lab.sh preflight|status"
    echo "       sudo env NOPEN_V081_DIRECT4G_ACK=$START_ACK bash v080/tools/v081_direct4g_kernel_lab.sh start"
    echo "       sudo bash v080/tools/v081_direct4g_kernel_lab.sh stop"
    exit 2;;
esac
if (( EUID!=0 ));then
  echo "V081_DIRECT4G_REFUSED start requires sudo";exit 2
fi
if [[ "${NOPEN_V081_DIRECT4G_ACK:-}" != "$START_ACK" ]];then
  echo "V081_DIRECT4G_REFUSED exact high-risk acknowledgment required";exit 9
fi
echo "WARNING: This is VOLATILE REAL 4GiB KERNEL SWAP ON YOUR MAIN AMD GPU."
echo "Past ublk/NBD experiments caused WRITE timeouts, SWAP read failures and kernel panic."
echo "A reboot is NOT the worst possible outcome; unsaved or in-flight data can be lost."
echo "The GPU / OpenCL userspace service may deadlock under memory reclaim."
echo "Nopen will not use an SSD shadow; ALL acknowledged swap pages depend on VRAM."
echo "Do not start without verified backups and a saved game."
echo "Type exactly EVET to continue, anything else cancels."
read -r approval </dev/tty ||
  { echo "V081_DIRECT4G_REFUSED interactive console required";exit 9; }
[[ "$approval" == EVET ]] ||
  { echo "V081_DIRECT4G_REFUSED confirmation missing";exit 9; }
python3 "$repo/v080/tools/v081_direct4g_preflight.py"
[[ ! -e "$dev" && ! -e /dev/ublkc23 ]] ||
  { echo "V081_DIRECT4G_REFUSED ublk device 23 already allocated";exit 2; }
[[ ! -e "$state/device" && ! -e "$state/shadow-file" ]] ||
  { echo "V081_DIRECT4G_REFUSED old Nopen state; inspect first";exit 2; }
if systemctl is-active --quiet "$service";then
  echo "V081_DIRECT4G_REFUSED existing ublk daemon";exit 2
fi
[[ -x "$target" && -x "$controller" && -x "$worker" ]] ||
  { echo "V081_DIRECT4G_REFUSED compile with bash v070/tools/build_ublk.sh first";exit 2; }
command -v swapon >/dev/null
command -v swapoff >/dev/null
command -v systemd-run >/dev/null
modprobe ublk_drv
[[ -c /dev/ublk-control ]] ||
  { echo "V081_DIRECT4G_REFUSED missing ublk-control";exit 2; }
install -d -m 0700 "$state"
install -d -m 0750 "$logdir"
logfile="$logdir/direct4g-$(date +%Y%m%d-%H%M%S)-${BASHPID}.log"
install -m 0640 /dev/null "$logfile"
ln -sfn "$logfile" "$state/log"
printf '%s\n' "$dev" > "$state/device"
printf '%s\n' "4096" > "$state/capacity"
printf '%s\n' "vram-direct" > "$state/storage-mode"
swapon --show > "$state/swapon-before.txt"

rollback_if_inactive() {
  # A SWAP-backed daemon must NEVER be stopped while it holds live swap.
  if is_active; then
    echo "V081_DIRECT4G_SWAP_ACTIVE_DURING_FAILURE leaving device and daemon intact" >&2
    return
  fi
  if [[ -e "$dev" ]];then
    timeout 8s "$controller" del -n "$id" || true
  fi
  systemctl stop "$service" >/dev/null 2>&1 || true
  rm -f "$state/device" "$state/capacity" "$state/storage-mode"
}
trap 'rollback_if_inactive' ERR
echo "V081_DIRECT4G_START storage=VRAM_ONLY ssd_shadow=NO zram=UNCHANGED"
systemd-run --system --quiet --unit=nopen-v070-ublk \
  --service-type=exec --collect \
  --property=Restart=no \
  --property=KillMode=control-group \
  --property=OOMPolicy=continue \
  --property=OOMScoreAdjust=-950 \
  --property=LimitMEMLOCK=infinity \
  --property="StandardOutput=append:$logfile" \
  --property="StandardError=append:$logfile" \
  --setenv=NOPEN_V070_KERNEL_SWAP_TEST=EXPERIMENTAL_GPU_SWAP_CAN_HANG \
  --setenv=NOPEN_UBLK_BACKEND=opencl \
  --setenv=NOPEN_UBLK_STORAGE_MODE=vram-direct \
  --setenv="NOPEN_VRAM_DIRECT_ACK=$ACK" \
  --setenv=NOPEN_VRAM_DIRECT_PREFILL=1 \
  --setenv=NOPEN_V070_CAPACITY_MIB=4096 \
  --setenv="NOPEN_UBLK_WORKER_BIN=$worker" \
  --setenv=MALLOC_ARENA_MAX=1 \
  "$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
    --max_io_buf_bytes 65536
printf '%s\n' "$(systemctl show "$service" --value --property=MainPID)" > "$state/pid"
ready=0
for (( i=0;i<200;i++ ));do
  if [[ -b "$dev" ]];then ready=1;break;fi
  if ! systemctl is-active --quiet "$service";then break;fi
  sleep 0.1
done
if (( ! ready ));then
  echo "V081_DIRECT4G_REFUSED ublk GPU device startup failed" >&2
  tail -n 80 "$logfile" >&2
  exit 5
fi
for stamp in NOPEN_VRAM_DIRECT_PHYSICAL_LAB NOPEN_VRAM_DIRECT_PREFILL_PASS \
  NOPEN_DIRECT_METADATA_MLOCK_PASS NOPEN_DIRECT_WORKER_STAGING_MLOCK_PASS;do
  if ! grep -Fq "$stamp" "$logfile";then
    echo "V081_DIRECT4G_REFUSED missing before-swap safety log: $stamp" >&2
    tail -n 80 "$logfile" >&2
    exit 5
  fi
done
# Raw 16KiB integrity test affects only our newly created GPU-backed block.
dd if=/dev/urandom of="$state/direct4g-raw" bs=4096 count=4 status=none
timeout 12s dd if="$state/direct4g-raw" of="$dev" bs=4096 count=4 \
  oflag=direct conv=notrunc status=none
timeout 12s dd if="$dev" of="$state/direct4g-readback" bs=4096 count=4 \
  iflag=direct status=none
cmp "$state/direct4g-raw" "$state/direct4g-readback"
echo "V081_DIRECT4G_RAW_INTEGRITY_PASS before_kernel_swapon=YES"
mkswap -f "$dev"
swapon -p "$priority" "$dev"
if ! is_active;then
  echo "V081_DIRECT4G_FAILED kernel swap activation unconfirmed" >&2
  exit 6
fi
trap - ERR
swapon --show | tee "$state/swapon-active.txt"
echo "V081_DIRECT4G_REAL_SWAP_ACTIVE device=$dev capacity_mib=4096 priority=$priority"
echo "V081_DIRECT4G_BACKING GPU_VRAM_ONLY=YES SSD_SHADOW=NO ZRAM_PRESERVED=YES"
echo "V081_DIRECT4G_NOT_CERTIFIED gpu_reset_recovery=NO forward_progress=UNPROVEN"
echo "V081_DIRECT4G_LOG=$logfile"
echo "Stop only when system is responsive: sudo bash $repo/v080/tools/v081_direct4g_kernel_lab.sh stop"
