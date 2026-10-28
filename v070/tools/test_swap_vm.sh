#!/usr/bin/env bash
# REAL Linux kernel swap activation on a disposable CI VM, MOCK backing only.
# Never run on a normal host; never creates a GPU/OpenCL context.
set -euo pipefail
[[ $# -eq 3 ]] || { echo "Usage: test_swap_vm.sh target ublk-cli worker" >&2; exit 2; }
target="$(realpath "$1")"; ublk="$(realpath "$2")"; worker="$(realpath "$3")"
[[ "$EUID" == 0 && "$(printenv GITHUB_ACTIONS || :)" == true &&
   "$(printenv NOPEN_ISOLATED_VM_TEST || :)" == I_ACKNOWLEDGE_DISPOSABLE_VM ]] ||
    { echo "V070_VM_REFUSED disposable CI VM only" >&2; exit 2; }
[[ -x "$target" && -x "$ublk" && -x "$worker" ]] || exit 2
[[ -c /dev/ublk-control ]] || { echo "V070_VM_UNAVAILABLE ublk control device absent"; exit 77; }
if grep -Eq '^/dev/(nbd|ublkb)' /proc/swaps; then
  echo "V070_VM_REFUSED another experimental swap active" >&2
  exit 2
fi
id=24;device="/dev/ublkb24"
[[ ! -e "$device" ]] || exit 2
dir="$(mktemp -d)"
pid=0; created=0; swap_active=0; cgroup=""
cleanup() {
  if [[ -n "$cgroup" && -d "$cgroup" ]]; then
    rmdir "$cgroup" 2>/dev/null || true
  fi
  if (( swap_active )); then
    timeout 40s swapoff "$device" || true
  fi
  if (( created )); then
    timeout 15s "$ublk" del -n "$id" || true
  fi
  if (( pid>0 )); then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
  rm -rf "$dir"
}
trap cleanup EXIT
NOPEN_UBLK_ISOLATED_LAB=EXPERIMENTAL_BLOCK_IO_NOT_SWAP \
NOPEN_UBLK_BACKEND=mock NOPEN_V070_CAPACITY_MIB=64 \
NOPEN_UBLK_WORKER_BIN="$worker" \
"$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
  --max_io_buf_bytes 65536 >"$dir/ublk.log" 2>&1 &
pid=$!
created=1
for _ in $(seq 1 120); do
  [[ -b "$device" ]] && break
  if ! kill -0 "$pid" 2>/dev/null; then break; fi
  sleep .1
done
if [[ ! -b "$device" ]]; then cat "$dir/ublk.log" >&2; exit 3; fi
# Direct I/O proves the actual block device, before swap initialization.
dd if=/dev/urandom of="$dir/raw" bs=4096 count=16 status=none
timeout 12s dd if="$dir/raw" of="$device" bs=4096 count=16 oflag=direct status=none
timeout 12s dd if="$device" of="$dir/read" bs=4096 count=16 iflag=direct status=none
cmp "$dir/raw" "$dir/read"
mkswap -f "$device" >"$dir/mkswap.log"
swapon -p 200 "$device"
swap_active=1
swapon --noheadings --raw --show=NAME | grep -Fxq "$device"
echo "V070_VM_SWAP_ACTIVE real_kernel_swap=YES mock_backend=YES"
# Best effort: force swap-ins/outs using disposable 64MiB cgroup,
# but do not misreport success if runner refuses cgroup changes.
proven=NO
if [[ -f /sys/fs/cgroup/cgroup.controllers ]] &&
   grep -qw memory /sys/fs/cgroup/cgroup.controllers &&
   mkdir "/sys/fs/cgroup/nopen-v070-ci-$$" 2>/dev/null; then
  cgroup="/sys/fs/cgroup/nopen-v070-ci-$$"
  if printf '%s\n' $((64*1024*1024)) >"$cgroup/memory.max" 2>/dev/null &&
     printf '%s\n' $((48*1024*1024)) >"$cgroup/memory.swap.max" 2>/dev/null; then
    marker="$dir/go"
    # The child waits until cgroup membership is applied before allocation.
    bash -c 'while [[ ! -e "$1" ]]; do sleep .01; done; exec python3 -c '"'"'
import time
b=bytearray(96*1024*1024)
for k in range(0,len(b),4096): b[k]=k%251
time.sleep(5)
'"'"' ' _ "$marker" >"$dir/pressure.log" 2>&1 &
    pressure=$!
    echo "$pressure" >"$cgroup/cgroup.procs" || true
    touch "$marker"
    for _ in $(seq 1 35); do
      current="$(cat "$cgroup/memory.swap.current" 2>/dev/null || echo 0)"
      if [[ "$current" =~ ^[0-9]+$ ]] && (( current > 0 )); then
        proven=YES
        echo "V070_VM_PAGING_PASS cgroup_swap_bytes=$current" 
        break
      fi
      sleep .2
    done
    kill "$pressure" 2>/dev/null || true
    wait "$pressure" 2>/dev/null || true
  fi
fi
echo "V070_VM_PAGING_STATUS swap_usage_observed=$proven"
timeout 40s swapoff "$device"
swap_active=0
timeout 15s "$ublk" del -n "$id"
created=0
[[ ! -e "$device" ]] || exit 4
echo "V070_VM_SWAP_PASS kernel_swapon=YES kernel_swapoff=YES block_verified=YES mock_backend=YES paging_usage_observed=$proven zram_untouched=YES"
