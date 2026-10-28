#!/usr/bin/env bash
# Actual ublk block device: persistent disk shadow must survive GPU worker SIGKILL.
# Runs ONLY on disposable CI VM, explicitly selected mock GPU worker.
set -euo pipefail
[[ $# -eq 3 ]] || exit 2
target="$(realpath "$1")"; ctl="$(realpath "$2")"; worker="$(realpath "$3")"
[[ "$EUID" == 0 && "${GITHUB_ACTIONS:-}" == true &&
   "${NOPEN_ISOLATED_VM_TEST:-}" == I_ACKNOWLEDGE_DISPOSABLE_VM ]] ||
 { echo "V070_SHADOW_VM_REFUSED CI VM only" >&2; exit 2; }
[[ -c /dev/ublk-control && -x "$target" && -x "$ctl" && -x "$worker" ]] || exit 77
id=26;dev="/dev/ublkb26"
[[ ! -e "$dev" ]] || exit 2
dir="$(mktemp -d /var/tmp/nopen-v070-shadow-XXXXXX)"
shadow="$dir/persistent-shadow.bin"
created=0;daemon=0
cleanup() {
 if ((created));then timeout 12s "$ctl" del -n "$id" || true;fi
 if ((daemon>0));then kill "$daemon" 2>/dev/null || true;wait "$daemon" 2>/dev/null || true;fi
 rm -rf "$dir"
}
trap cleanup EXIT
NOPEN_UBLK_ISOLATED_LAB=EXPERIMENTAL_BLOCK_IO_NOT_SWAP \
NOPEN_UBLK_BACKEND=mock NOPEN_V070_CAPACITY_MIB=64 \
NOPEN_UBLK_WORKER_BIN="$worker" NOPEN_UBLK_SHADOW_FILE="$shadow" \
"$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
 --max_io_buf_bytes 65536 >"$dir/ublk.log" 2>&1 &
daemon=$!;created=1
for _ in $(seq 1 120); do
 [[ -b "$dev" ]] && break
 kill -0 "$daemon" 2>/dev/null || break
 sleep .1
done
if [[ ! -b "$dev" ]]; then cat "$dir/ublk.log" >&2;exit 3;fi
[[ -s "$shadow" ]] || exit 3
dd if=/dev/urandom of="$dir/expected" bs=4096 count=8 status=none
timeout 12s dd if="$dir/expected" of="$dev" bs=4096 count=8 oflag=direct status=none
child="$(pgrep -P "$daemon" -f "nopen-isolated-worker" | head -n 1 || true)"
if [[ ! "$child" =~ ^[0-9]+$ ]]; then
 echo "V070_SHADOW_VM_FAILED independent GPU worker not found" >&2
 cat "$dir/ublk.log" >&2;exit 4
fi
kill -9 "$child"
# The first I/O after SIGKILL must trigger shadow fallback, not kernel EIO.
timeout 12s dd if="$dev" of="$dir/actual" bs=4096 count=8 iflag=direct status=none
cmp "$dir/expected" "$dir/actual"
dd if=/dev/urandom of="$dir/expected2" bs=4096 count=8 status=none
timeout 12s dd if="$dir/expected2" of="$dev" bs=4096 count=8 seek=8 oflag=direct status=none
timeout 12s dd if="$dev" of="$dir/actual2" bs=4096 count=8 skip=8 iflag=direct status=none
cmp "$dir/expected2" "$dir/actual2"
grep -q NOPEN_SHADOW_FAILOVER "$dir/ublk.log" ||
 { echo "V070_SHADOW_VM_FAILED no fallback evidence" >&2;cat "$dir/ublk.log" >&2;exit 5; }
timeout 15s "$ctl" del -n "$id";created=0
echo "V070_SHADOW_VM_PASS live_ublk_device=YES GPU_worker_SIGKILL=YES"
echo "V070_SHADOW_VM_PASS_AFTER_READ persisted_8_pages=YES fallback_write=YES kernel_swap_NOT_ENABLED=YES"
