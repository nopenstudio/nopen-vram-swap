#!/usr/bin/env bash
# Nopen v0.7 experimental LIVE GPU->Linux kernel swap. May HANG/reboot PC.
# No installation, no autostart, no zram disable. Operates only /dev/ublkb23.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
action="${1:-status}"
capacity="${2:-}"
build="$repo/build-v070"
state="/run/nopen-v070"
dev="/dev/ublkb23"
id=23
priority=200
target="$build/nopen-v070-ublk"
controller="$build/ublksrv-upstream/ublk"
worker="$build/v060/nopen-isolated-worker"
usage() {
  echo "Usage: sudo bash v070/tools/vram_swap_lab.sh start {64|256|1024|2048|4096}" >&2
  echo "       sudo bash v070/tools/vram_swap_lab.sh stop" >&2
  echo "       bash v070/tools/vram_swap_lab.sh status" >&2
}
is_active() { swapon --noheadings --raw --show=NAME 2>/dev/null | grep -Fxq "$dev"; }
case "$action" in start|stop|status) : ;; *) usage; exit 2 ;; esac
if [[ "$action" == status ]]; then
  echo "V070_STATUS device=$dev exists=$([[ -b "$dev" ]] && echo yes || echo no)"
  swapon --show
  [[ ! -r "$state/pid" ]] || { echo "NOPEN_SERVER_PID=$(cat "$state/pid")"; }
  [[ ! -r "$state/log" ]] || tail -n 12 "$state/log"
  exit 0
fi
[[ "$EUID" == 0 ]] || { echo "V070_REFUSED root required for swapon/ublk" >&2; exit 2; }
[[ -d /sys/fs/cgroup ]] || exit 2
command -v swapon >/dev/null
command -v swapoff >/dev/null
if [[ "$action" == stop ]]; then
  [[ -r "$state/device" && "$(cat "$state/device")" == "$dev" ]] ||
      { echo "V070_STOP_REFUSED state file absent/mismatch; do not remove unknown block devices" >&2; exit 2; }
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
  if [[ -r "$state/pid" ]]; then
    pid="$(cat "$state/pid")"
    if [[ "$pid" =~ ^[0-9]+$ ]]; then kill "$pid" 2>/dev/null || true; fi
  fi
  echo "V070_STOP_PASS device_removed=YES zram_untouched=YES"
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 0
fi
[[ "$capacity" =~ ^(64|256|1024|2048|4096)$ ]] || { usage; exit 2; }
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
printf '%s\n' "$dev" > "$state/device"
printf '%s\n' "$capacity" > "$state/capacity"
swapon --show > "$state/swapon-before.txt"
echo "V070_START pool_mib=$capacity device=$dev priority=$priority zram_preserved=YES" | tee "$state/start.log"
NOPEN_V070_KERNEL_SWAP_TEST=EXPERIMENTAL_GPU_SWAP_CAN_HANG \
NOPEN_UBLK_BACKEND=opencl \
NOPEN_V070_CAPACITY_MIB="$capacity" \
NOPEN_UBLK_WORKER_BIN="$worker" \
"$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
  --max_io_buf_bytes 65536 > "$state/log" 2>&1 &
server_pid=$!
echo "$server_pid" > "$state/pid"
ready=0
for _ in $(seq 1 150); do
  if [[ -b "$dev" ]]; then ready=1; break; fi
  if ! kill -0 "$server_pid" 2>/dev/null; then break; fi
  sleep 0.1
done
if (( ! ready )); then
  echo "V070_START_FAILED kernel ublk device was not created" >&2
  tail -n 60 "$state/log" >&2
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
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 5
fi
mkswap -f "$dev" | tee "$state/mkswap.log"
if ! swapon -p "$priority" "$dev"; then
  echo "V070_START_FAILED swapon rejected GPU device; leaving zram intact" >&2
  timeout 8s "$controller" del -n "$id" || true
  rm -f "$state/pid" "$state/device" "$state/capacity"
  exit 6
fi
swapon --show | tee "$state/swapon-active.txt"
if ! is_active; then
  echo "V070_START_FAILED no active kernel swap after swapon" >&2
  exit 7
fi
echo "V070_SWAP_ACTIVE device=$dev pool_mib=$capacity priority=$priority real_gpu=YES direct_io_verified=YES zram_untouched=YES"
echo "To stop: sudo bash $repo/v070/tools/vram_swap_lab.sh stop"
