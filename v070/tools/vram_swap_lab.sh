#!/usr/bin/env bash
# Nopen v0.7 experimental LIVE GPU->Linux kernel swap. May HANG/reboot PC.
# No installation, no autostart, no zram disable. Operates only /dev/ublkb23.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
action="${1:-status}"
capacity="${2:-}"
build="$repo/build-v070"
state="/run/nopen-v070"
logdir="/var/log/nopen-v070"
shadowbase="/var/lib/nopen-v070"
shadowdir="$shadowbase"
dev="/dev/ublkb23"
id=23
priority=200
target="$build/nopen-v070-ublk"
controller="$build/ublksrv-upstream/ublk"
worker="$build/v060/nopen-isolated-worker"
service="nopen-v070-ublk.service"
usage() {
  echo "Usage: sudo bash v070/tools/vram_swap_lab.sh start-pilot 64 (explicit opt-in required)" >&2
  echo "       Legacy start {64|256|1024|2048|4096} remains BLOCKED" >&2
  echo "       sudo bash v070/tools/vram_swap_lab.sh stop" >&2
  echo "       bash v070/tools/vram_swap_lab.sh status" >&2
}
is_active() { swapon --noheadings --raw --show=NAME 2>/dev/null | grep -Fxq "$dev"; }
case "$action" in start|start-pilot|stop|status) : ;; *) usage; exit 2 ;; esac
if [[ "$action" == status ]]; then
  echo "V070_STATUS device=$dev exists=$([[ -b "$dev" ]] && echo yes || echo no)"
  swapon --show
  systemctl is-active "$service" 2>/dev/null || true
  systemctl show "$service" --property=MainPID --property=ControlGroup --no-pager 2>/dev/null || true
  [[ ! -r "$state/log" ]] || tail -n 12 "$state/log"
  exit 0
fi
[[ "$EUID" == 0 ]] || { echo "V070_REFUSED root required for swapon/ublk" >&2; exit 2; }
[[ -d /sys/fs/cgroup ]] || exit 2
command -v swapon >/dev/null
command -v swapoff >/dev/null
if [[ "$action" == stop ]]; then
  # Idempotent stop when no Nopen swap was ever started. NEVER claim cleanup
  # if the kernel device or its root service exists without matching state.
  if [[ ! -r "$state/device" ]]; then
    if [[ ! -e "$dev" ]] && ! is_active &&
       ! systemctl is-active --quiet "$service"; then
      echo "V070_STOP_ALREADY_INACTIVE nothing_to_stop=YES zram_untouched=YES"
      exit 0
    fi
    echo "V070_STOP_REFUSED state missing while device/service exists; manual investigation required" >&2
    exit 2
  fi
  if [[ "$(cat "$state/device")" != "$dev" ]]; then
    echo "V070_STOP_REFUSED state/device mismatch; do not remove unknown block devices" >&2
    exit 2
  fi
  if is_active; then
    echo "V070_STOP swapoff requested (can stall under memory pressure)"
    if ! timeout --signal=TERM --kill-after=5s 40s swapoff "$dev"; then
      echo "V070_STOP_FAILED swap remains active; LEAVING kernel block device and worker intact" >&2
      exit 5
    fi
  fi
  if [[ -b "$dev" ]]; then
    if ! timeout --signal=TERM --kill-after=3s 12s "$controller" del -n "$id"; then
      echo "V070_STOP_FAILED ublk controller could not delete device; leaving state for inspection" >&2
      exit 6
    fi
  fi
  if systemctl is-active --quiet "$service"; then
    systemctl stop "$service"
  fi
  if systemctl is-active --quiet "$service"; then
    echo "V070_STOP_FAILED root system service remains active" >&2
    exit 7
  fi
  echo "V070_STOP_PASS device_removed=YES zram_untouched=YES"
  if [[ -r "$state/shadow-file" ]]; then
    shadowfile="$(cat "$state/shadow-file")"
    # Delete only the exact dedicated persistent shadow AFTER successful
    # swapoff and kernel device teardown. Never delete active swap backing.
    if [[ "$shadowfile" == "$shadowbase/"* && "$shadowfile" == */shadow-*.bin && -f "$shadowfile" ]]; then
      rm -- "$shadowfile"
    fi
  fi
  rm -f "$state/pid" "$state/device" "$state/capacity" "$state/shadow-file"
  exit 0
fi
# Known blocking defect: actual RX 9060 XT live kernel swap-in failed with
# repeated "Read-error on swap-device" and apparent system lockup.
# Do not re-enable host SWAP until lossless recovery/forward progress is fixed.
# STOP and STATUS remain usable to recover a still-running experiment.
if [[ "$action" == start ]]; then
  echo "V070_START_BLOCKED known physical GPU SWAP page-in failure (GitHub issue #7)." >&2
  echo "Read-only diagnostics: bash v070/tools/collect_swap_failure.sh" >&2
  echo "Do not restart kernel SWAP until new implementation passes failure recovery gates." >&2
  exit 9
fi
# The only approved host kernel SWAP entrypoint is a deliberately tiny
# 64MiB pilot, guarded by a second exact acknowledgement. Legacy start still
# rejects all physical SWAP until Issue #7 has a final resolution.
if [[ "$action" == start-pilot ]]; then
  if [[ "$capacity" != "64" ||
        "$(printenv NOPEN_V070_PILOT_ACK || :)" != "I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK" ]]; then
    echo "V070_PILOT_REFUSED require exactly 64MiB and NOPEN_V070_PILOT_ACK=I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK" >&2
    exit 9
  fi
  echo "V070_PILOT_GATE_PASS max_capacity_mib=64 full_swap_release=NO"
fi
[[ "$capacity" =~ ^(64|256|1024|2048|4096)$ ]] || { usage; exit 2; }
command -v systemd-run >/dev/null ||
  { echo "V070_REFUSED root system service manager unavailable" >&2; exit 2; }
if systemctl is-active --quiet "$service"; then
  echo "V070_REFUSED system service already active; inspect/stop previous Nopen swap" >&2
  exit 2
fi
[[ -x "$target" && -x "$controller" && -x "$worker" ]] ||
  { echo "V070_REFUSED run bash v070/tools/build_ublk.sh first" >&2; exit 2; }
[[ ! -e "$dev" && ! -e "/dev/ublkc23" ]] ||
  { echo "V070_REFUSED ublk device ID 23 already allocated" >&2; exit 2; }
[[ ! -e "$state/device" ]] ||
  { echo "V070_REFUSED earlier test state exists; inspect status/stop" >&2; exit 2; }
if swapon --noheadings --raw --show=NAME | grep -Eq '^/dev/nbd'; then
  echo "V070_REFUSED old NBD swap is still active" >&2
  exit 2
fi
modprobe ublk_drv
[[ -c /dev/ublk-control ]] || { echo "V070_REFUSED kernel has no ublk control device" >&2; exit 2; }
if [[ "$(printenv NOPEN_V070_ACCEPT_RISK || :)" != "I_ACCEPT_GPU_SWAP_REBOOT" ]]; then
  echo "This is REAL Linux kernel SWAP backed by Radeon GPU/OpenCL."
  echo "It can deadlock Linux/AMDGPU, force a hard power cycle, or damage unsaved data."
  echo "zram will stay enabled; Nopen gets priority $priority. Type 'EVET' to start."
  read -r approval </dev/tty || { echo "V070_REFUSED no terminal"; exit 2; }
  [[ "$approval" == EVET ]] || { echo "V070_REFUSED consent not received"; exit 2; }
fi
install -d -m 0700 "$state"
install -d -m 0700 "$shadowbase"
shadowtype="$(stat -f -c '%T' "$shadowbase")"
case "$shadowtype" in
  ext2/ext3|xfs)
    echo "V070_SHADOW_FILESYSTEM fs=$shadowtype mode=direct_disk" ;;
  btrfs)
    # The root/home Btrfs subvolume may be snapshotted by Snapper.
    # A nested child subvolume is NOT included as copied data in a parent
    # snapshot. This shadow MUST NOT itself be snapshotted/reflinked while
    # the active swap device depends on its physical extents.
    command -v btrfs >/dev/null ||
      { echo "V070_REFUSED btrfs-progs not installed" >&2; exit 2; }
    shadowdir="$shadowbase/shadow-subvolume"
    if [[ -e "$shadowdir" ]]; then
      if ! btrfs subvolume show "$shadowdir" >/dev/null 2>&1; then
        echo "V070_REFUSED shadow path exists but is not a Btrfs subvolume" >&2
        exit 2
      fi
    else
      btrfs subvolume create "$shadowdir"
    fi
    chmod 0700 "$shadowdir"
    [[ "$(stat -f -c '%T' "$shadowdir")" == btrfs ]] &&
      btrfs subvolume show "$shadowdir" >/dev/null ||
      { echo "V070_REFUSED cannot verify isolated Btrfs shadow subvolume" >&2; exit 2; }
    echo "V070_SHADOW_FILESYSTEM fs=btrfs mode=isolated_nocow_subvolume snapshot_of_shadow=FORBIDDEN"
    ;;
  *)
    echo "V070_REFUSED SSD shadow filesystem $shadowtype not approved" >&2
    exit 2 ;;
esac
# Require enough real block storage for the full write-through backing file
# and reserve; do not rely on GPU VRAM or a RAM-backed tmpfs.
free_kib="$(df -Pk "$shadowdir" | awk 'NR==2{print $4}')"
if [[ ! "$free_kib" =~ ^[0-9]+$ ]] ||
   (( free_kib < capacity * 1024 + 262144 )); then
  echo "V070_PILOT_REFUSED insufficient independent SSD capacity" >&2
  exit 2
fi
shadowfile="$shadowdir/shadow-$(date +%Y%m%d-%H%M%S)-${BASHPID}.bin"
[[ ! -e "$shadowfile" ]] || { echo "V070_REFUSED shadow file exists" >&2; exit 2; }
printf '%s\n' "$shadowfile" > "$state/shadow-file"
install -d -m 0750 -g "$(id -gn "${SUDO_USER:-root}")" "$logdir"
logfile="$logdir/server-$(date +%Y%m%d-%H%M%S).log"
install -m 0640 -g "$(id -gn "${SUDO_USER:-root}")" /dev/null "$logfile"
ln -sfn "$logfile" "$state/log"
printf '%s\n' "$dev" > "$state/device"
printf '%s\n' "$capacity" > "$state/capacity"
swapon --show > "$state/swapon-before.txt"
echo "V070_START mode=$action pool_mib=$capacity device=$dev priority=$priority zram_preserved=YES durable_ssd_shadow=REQUIRED" | tee "$state/start.log"
echo "V070_PERSISTENT_LOG file=$logfile realtime_log=YES"
echo "V070_SHADOW_PREPARE path=$shadowfile durability=O_DIRECT_O_DSYNC"
# The ublk target runs in the foreground as a SYSTEM service; the service is
# independent of user@1000.service/KDE. This corrects the observed session
# teardown problem, but does not fix the earlier kernel WRITE timeout.
if ! systemd-run --system --quiet --unit=nopen-v070-ublk \
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
    --setenv="NOPEN_V070_CAPACITY_MIB=$capacity" \
    --setenv="NOPEN_UBLK_WORKER_BIN=$worker" \
    --setenv="NOPEN_UBLK_SHADOW_FILE=$shadowfile" \
    "$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
      --max_io_buf_bytes 65536; then
  echo "V070_START_FAILED could not launch independent root system service" >&2
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 4
fi
server_pid="$(systemctl show "$service" --value --property=MainPID)"
echo "$server_pid" > "$state/pid"
ready=0
for _ in $(seq 1 150); do
  if [[ -b "$dev" ]]; then ready=1; break; fi
  if ! systemctl is-active --quiet "$service"; then break; fi
  sleep 0.1
done
if (( ! ready )); then
  echo "V070_START_FAILED kernel ublk device was not created" >&2
  tail -n 60 "$state/log" >&2
  systemctl stop "$service" || true
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 4
fi
# Byte-verify the real GPU device before writing swap metadata.
dd if=/dev/urandom of="$state/raw-test" bs=4096 count=4 status=none
if ! timeout 10s dd if="$state/raw-test" of="$dev" bs=4096 count=4 \
       oflag=direct conv=notrunc status=none ||
   ! timeout 10s dd if="$dev" of="$state/readback" bs=4096 count=4 \
       iflag=direct status=none ||
   ! cmp "$state/raw-test" "$state/readback"; then
  echo "V070_START_FAILED GPU direct-I/O integrity failed; swap NOT enabled" >&2
  timeout 8s "$controller" del -n "$id" || true
  systemctl stop "$service" || true
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 5
fi
mkswap -f "$dev" | tee "$state/mkswap.log"
if ! swapon -p "$priority" "$dev"; then
  echo "V070_START_FAILED swapon rejected GPU device; leaving zram intact" >&2
  timeout 8s "$controller" del -n "$id" || true
  systemctl stop "$service" || true
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 6
fi
swapon --show | tee "$state/swapon-active.txt"
if ! is_active; then
  echo "V070_START_FAILED no active kernel swap after swapon" >&2
  exit 7
fi
echo "V070_SWAP_ACTIVE mode=$action device=$dev pool_mib=$capacity priority=$priority service=$service root_system_service=YES real_gpu=YES durable_ssd_shadow=YES direct_io_verified=YES zram_untouched=YES"
echo "To stop: sudo bash $repo/v070/tools/vram_swap_lab.sh stop"
