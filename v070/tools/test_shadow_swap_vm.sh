#!/usr/bin/env bash
# REAL kernel SWAP-in after GPU worker SIGKILL with independent SSD shadow.
# Mock backend, disposable GitHub VM only. NEVER run on a desktop.
set -euo pipefail
[[ $# -eq 3 ]] || { echo "Usage: $0 target ublk-cli worker" >&2; exit 2; }
target="$(realpath "$1")"; ctl="$(realpath "$2")"; worker="$(realpath "$3")"
[[ "$EUID" == 0 && "$(printenv GITHUB_ACTIONS || :)" == true &&
   "$(printenv NOPEN_ISOLATED_VM_TEST || :)" == I_ACKNOWLEDGE_DISPOSABLE_VM ]] ||
  { echo "V070_PAGING_FAILOVER_REFUSED disposable CI VM gate absent" >&2; exit 2; }
vendor="$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || true)"
product="$(cat /sys/class/dmi/id/product_name 2>/dev/null || true)"
printf '%s\n' "$vendor $product" | grep -Eiq \
  'QEMU|KVM|VMware|Hyper-V|Virtual Machine|Microsoft Corporation|Bochs|Amazon EC2' ||
  { echo "V070_PAGING_FAILOVER_REFUSED not a recognized virtual machine" >&2; exit 2; }
[[ -x "$target" && -x "$ctl" && -x "$worker" && -c /dev/ublk-control ]] || exit 77
[[ -f /sys/fs/cgroup/cgroup.controllers ]] || {
  echo "V070_PAGING_FAILOVER_UNVERIFIED cgroup-v2 not available"; exit 77;
}
grep -qw memory /sys/fs/cgroup/cgroup.controllers || {
  echo "V070_PAGING_FAILOVER_UNVERIFIED memory controller unavailable"; exit 77;
}
if grep -Eq '^/dev/(nbd|ublkb)' /proc/swaps; then
  echo "V070_PAGING_FAILOVER_REFUSED other experimental kernel swap active" >&2
  exit 2
fi
id=27;dev="/dev/ublkb27"; cgroup="/sys/fs/cgroup/nopen-v070-failover-$$"
[[ ! -e "$dev" && ! -e "$cgroup" ]] || exit 2
dir="$(mktemp -d /var/tmp/nopen-v070-swapped-shadow-XXXXXX)"
shadow="$dir/verified-persistent-shadow.bin"
daemon=0;pressure=0;created=0;swap_active=0;group_created=0
cleanup() {
  if (( pressure>0 )); then
    kill "$pressure" 2>/dev/null || true
    wait "$pressure" 2>/dev/null || true
  fi
  if (( group_created )); then rmdir "$cgroup" 2>/dev/null || true; fi
  if (( swap_active )); then
    timeout --signal=TERM --kill-after=4s 35s swapoff "$dev" || true
  fi
  if (( created )); then
    timeout --signal=TERM --kill-after=3s 15s "$ctl" del -n "$id" || true
  fi
  if (( daemon>0 )); then
    kill "$daemon" 2>/dev/null || true
    wait "$daemon" 2>/dev/null || true
  fi
  rm -rf "$dir"
}
trap cleanup EXIT
NOPEN_UBLK_ISOLATED_LAB=EXPERIMENTAL_BLOCK_IO_NOT_SWAP \
NOPEN_UBLK_BACKEND=mock NOPEN_V070_CAPACITY_MIB=64 \
NOPEN_UBLK_WORKER_BIN="$worker" NOPEN_UBLK_SHADOW_FILE="$shadow" \
"$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
  --max_io_buf_bytes 65536 >"$dir/ublk.log" 2>&1 &
daemon=$!
created=1
for _ in $(seq 1 150); do
  [[ -b "$dev" ]] && break
  if ! kill -0 "$daemon" 2>/dev/null; then break; fi
  sleep .1
done
if [[ ! -b "$dev" ]]; then
  echo "V070_PAGING_FAILOVER_FAILED ublk block device did not appear" >&2
  cat "$dir/ublk.log" >&2
  exit 3
fi
[[ -s "$shadow" ]] || { echo "V070_PAGING_FAILOVER_FAILED empty disk shadow" >&2; exit 3; }
mkswap -f "$dev" >"$dir/mkswap.log"
swapon -p 200 "$dev"
swap_active=1
swapon --noheadings --raw --show=NAME | grep -Fxq "$dev"
mkdir "$cgroup"
group_created=1
printf '%s\n' $((64*1024*1024)) > "$cgroup/memory.max"
printf '%s\n' $((64*1024*1024)) > "$cgroup/memory.swap.max"
cat >"$dir/pressure.py" <<'PY'
import pathlib,sys,time
gate=pathlib.Path(sys.argv[1])
ready=pathlib.Path(sys.argv[2])
done=pathlib.Path(sys.argv[3])
N=96*1024*1024
try:
    page=bytearray(N)
    for offset in range(0,N,4096):
        page[offset]=((offset//4096)*29+7)%256
    ready.write_text("READY\n")
    for _ in range(3000):
        if gate.exists():
            break
        time.sleep(.01)
    else:
        raise RuntimeError("gate wait exceeded 30 seconds")
    for offset in range(0,N,4096):
        if page[offset] != ((offset//4096)*29+7)%256:
            raise RuntimeError(f"swap-in data mismatch at offset {offset}")
    done.write_text("VERIFIED_ALL_24576_PAGES\n")
except Exception as exc:
    print("V070_PAGING_FAILOVER_CHILD_FAILED",repr(exc),file=sys.stderr)
    sys.exit(1)
PY
bash -c 'while [[ ! -e "$1" ]]; do sleep 0.01; done; exec python3 "$2" "$3" "$4" "$5"' \
  _ "$dir/begin" "$dir/pressure.py" "$dir/verify-now" "$dir/ready" "$dir/verified" \
  >"$dir/pressure.log" 2>&1 &
pressure=$!
echo "$pressure" > "$cgroup/cgroup.procs"
touch "$dir/begin"
for _ in $(seq 1 250); do
  [[ -f "$dir/ready" ]] && break
  kill -0 "$pressure" 2>/dev/null || break
  sleep .04
done
[[ -f "$dir/ready" ]] || {
  echo "V070_PAGING_FAILOVER_FAILED pressure child did not allocate" >&2
  cat "$dir/pressure.log" >&2; exit 4;
}
used=0
for _ in $(seq 1 250); do
  used="$(awk -v d="$dev" '$1==d{print $4}' /proc/swaps)"
  [[ "$used" =~ ^[0-9]+$ ]] || used=0
  if ((used>0)); then break; fi
  kill -0 "$pressure" 2>/dev/null || break
  sleep .04
done
if ((used==0)); then
  echo "V070_PAGING_FAILOVER_FAILED no Nopen swap pages observed" >&2
  cat "$dir/pressure.log" >&2; exit 5
fi
echo "V070_PAGING_FAILOVER_SWAPIN_READY kernel_swap_used_kib=$used real_kernel_swap=YES"
child="$(pgrep -P "$daemon" -f "nopen-isolated-worker" | head -n 1 || true)"
[[ "$child" =~ ^[0-9]+$ ]] || {
  echo "V070_PAGING_FAILOVER_FAILED GPU worker not found" >&2; exit 5;
}
kill -9 "$child"
echo "V070_PAGING_FAILOVER_INJECT worker_sigkill=YES kernel_swap_still_active=YES"
touch "$dir/verify-now"
for _ in $(seq 1 400); do
  [[ -f "$dir/verified" ]] && break
  kill -0 "$pressure" 2>/dev/null || break
  sleep .05
done
if [[ ! -f "$dir/verified" ]]; then
  echo "V070_PAGING_FAILOVER_FAILED swap-in verification did not finish" >&2
  cat "$dir/pressure.log" >&2
  tail -n 120 "$dir/ublk.log" >&2
  exit 6
fi
grep -Fxq VERIFIED_ALL_24576_PAGES "$dir/verified"
wait "$pressure"
pressure=0
grep -q NOPEN_SHADOW_FAILOVER "$dir/ublk.log" || {
  echo "V070_PAGING_FAILOVER_FAILED expected SSD fallback missing" >&2; exit 7;
}
timeout 35s swapoff "$dev"
swap_active=0
timeout 15s "$ctl" del -n "$id"
created=0
[[ ! -e "$dev" ]] || exit 8
rmdir "$cgroup";group_created=0
echo "V070_PAGING_FAILOVER_PASS real_kernel_swap=YES swapout_observed=YES worker_sigkill=YES"
echo "V070_PAGING_FAILOVER_PASS_AFTER_READ checked_pages=24576 disk_shadow_fallback=YES swapoff=YES device_deleted=YES mock_gpu=YES"
