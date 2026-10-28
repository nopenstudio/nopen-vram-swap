#!/usr/bin/env bash
# REAL KERNEL ublk lifecycle smoke. ONLY in a disposable VM CI runner.
# Mock GPU data backend; no NBD, no swapon, no filesystem mounting.
set -euo pipefail
[[ $# -eq 2 ]] || { echo "usage: $0 /abs/path/nopen-ublk-recovery /abs/path/ublk" >&2; exit 2; }
target="$(realpath "$1")"
ublk="$(realpath "$2")"
[[ -x "$target" && -x "$ublk" ]] || exit 2
[[ "$EUID" == 0 ]] || { echo "UBLK_LIFECYCLE_REFUSED root only inside disposable VM" >&2; exit 2; }
[[ "$(printenv NOPEN_ISOLATED_VM_TEST || :)" == "I_ACKNOWLEDGE_DISPOSABLE_VM" ]] || { echo "UBLK_LIFECYCLE_REFUSED missing disposable VM gate" >&2; exit 2; }
[[ "$(printenv GITHUB_ACTIONS || :)" == "true" ]] || { echo "UBLK_LIFECYCLE_REFUSED not in GitHub ephemeral VM" >&2; exit 2; }
vendor="$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || true)"
product="$(cat /sys/class/dmi/id/product_name 2>/dev/null || true)"
if ! printf '%s\n' "$vendor $product" | grep -Eiq 'QEMU|KVM|VMware|Hyper-V|Virtual Machine|Microsoft Corporation|Bochs|Amazon EC2'; then
  echo "UBLK_LIFECYCLE_REFUSED no recognized disposable virtual platform" >&2
  exit 2
fi
[[ -c /dev/ublk-control ]] || { echo "UBLK_LIFECYCLE_UNAVAILABLE control device not present" >&2; exit 77; }
if grep -Eq '^/dev/(nbd|ublkb)' /proc/swaps; then
  echo "UBLK_LIFECYCLE_REFUSED potentially dangerous active test swap" >&2; exit 2
fi
device_id=1791
device="/dev/ublkb1791"
[[ ! -e "$device" ]] || { echo "UBLK_LIFECYCLE_REFUSED device collision" >&2; exit 2; }
work="$(mktemp -d)"
created=0
child_pid=0
cleanup() {
  if (( created )); then
    timeout --signal=TERM --kill-after=3s 8s "$ublk" del -n "$device_id" >>"$work/cleanup.log" 2>&1 || true
  fi
  if (( child_pid>0 )); then
    kill "$child_pid" 2>/dev/null || true
    wait "$child_pid" 2>/dev/null || true
  fi
  rm -rf "$work"
}
trap cleanup EXIT
NOPEN_UBLK_ISOLATED_LAB=EXPERIMENTAL_BLOCK_IO_NOT_SWAP \
NOPEN_UBLK_BACKEND=mock \
NOPEN_UBLK_RECOVERY_MODE=full-cpu-mirror \
"$target" add -t nopen-recovery-lab -n "$device_id" -q 1 -d 16 \
  --max_io_buf_bytes 65536 >"$work/server.log" 2>&1 &
child_pid=$!
for _ in $(seq 1 100); do
  if [[ -b "$device" ]]; then created=1; break; fi
  if ! kill -0 "$child_pid" 2>/dev/null; then break; fi
  sleep 0.1
done
if (( ! created )); then
  echo "UBLK_LIFECYCLE_FAILED ublk device did not appear" >&2
  cat "$work/server.log" >&2
  exit 1
fi
dd if=/dev/urandom of="$work/input" bs=4096 count=16 status=none
timeout 12s dd if="$work/input" of="$device" bs=4096 count=16 oflag=direct conv=notrunc status=none
timeout 12s dd if="$device" of="$work/output" bs=4096 count=16 iflag=direct status=none
cmp "$work/input" "$work/output"
timeout 8s "$ublk" del -n "$device_id" >"$work/delete.log" 2>&1
created=0
for _ in $(seq 1 50); do
  [[ ! -e "$device" ]] && break
  sleep 0.1
done
[[ ! -e "$device" ]] || { echo "UBLK_LIFECYCLE_FAILED block device survived delete" >&2; exit 1; }
echo "UBLK_LIFECYCLE_PASS created=YES direct_4k_io=YES bytewise_verified=YES deleted=YES mock_only=YES no_global_swap=YES"
