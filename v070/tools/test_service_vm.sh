#!/usr/bin/env bash
# Real kernel ublk service-lifecycle regression on disposable GitHub VM only.
# Mock backend; no swapon. The test daemon must survive user-session teardown
# because its cgroup belongs to system.slice and not user@1000.service.
set -euo pipefail
[[ $# -eq 3 ]] || exit 2
target="$(realpath "$1")"
ctl="$(realpath "$2")"
worker="$(realpath "$3")"
[[ "$EUID" == 0 && "${GITHUB_ACTIONS:-}" == true &&
   "${NOPEN_ISOLATED_VM_TEST:-}" == I_ACKNOWLEDGE_DISPOSABLE_VM ]] ||
   { echo "SYSTEM_UNIT_VM_REFUSED disposable GitHub VM only" >&2; exit 2; }
[[ -x "$target" && -x "$ctl" && -x "$worker" ]] || exit 2
[[ -c /dev/ublk-control && -d /run/systemd/system ]] ||
   { echo "SYSTEM_UNIT_VM_UNAVAILABLE no systemd/ublk support"; exit 77; }
id=25
dev="/dev/ublkb25"
unit="nopen-v070-vm-service.service"
[[ ! -e "$dev" ]] || exit 2
if systemctl is-active --quiet "$unit"; then exit 2; fi
d="$(mktemp -d)"
created=0
cleanup() {
  if (( created )); then timeout 12s "$ctl" del -n "$id" || true; fi
  systemctl stop "$unit" >/dev/null 2>&1 || true
  rm -rf "$d"
}
trap cleanup EXIT
systemd-run --system --quiet --unit=nopen-v070-vm-service \
    --service-type=exec --collect \
    --property=Restart=no --property=KillMode=control-group \
    --property=OOMPolicy=continue --property=OOMScoreAdjust=-950 \
    --setenv=NOPEN_UBLK_ISOLATED_LAB=EXPERIMENTAL_BLOCK_IO_NOT_SWAP \
    --setenv=NOPEN_UBLK_BACKEND=mock \
    --setenv=NOPEN_V070_CAPACITY_MIB=64 \
    --setenv="NOPEN_UBLK_WORKER_BIN=$worker" \
    "$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
    --max_io_buf_bytes 65536
created=1
for _ in $(seq 1 100); do
  [[ -b "$dev" ]] && break
  systemctl is-active --quiet "$unit" || break
  sleep .1
done
if [[ ! -b "$dev" ]]; then
  journalctl -u "$unit" --no-pager -n 80 >&2 || true
  echo "SYSTEM_UNIT_VM_FAILED kernel block device absent" >&2
  exit 3
fi
group="$(systemctl show "$unit" --property=ControlGroup --value)"
[[ "$group" == /system.slice/* ]] ||
  { echo "SYSTEM_UNIT_VM_FAILED invalid cgroup=$group" >&2; exit 4; }
mainpid="$(systemctl show "$unit" --property=MainPID --value)"
[[ "$mainpid" =~ ^[0-9]+$ && "$mainpid" -gt 1 ]] || exit 4
dd if=/dev/urandom of="$d/expected" bs=4096 count=8 status=none
timeout 10s dd if="$d/expected" of="$dev" bs=4096 count=8 oflag=direct status=none
timeout 10s dd if="$dev" of="$d/actual" bs=4096 count=8 iflag=direct status=none
cmp "$d/expected" "$d/actual"
timeout 15s "$ctl" del -n "$id"
created=0
# libublksrv may exit and --collect unload the transient unit after del.
systemctl stop "$unit" >/dev/null 2>&1 || true
if systemctl is-active --quiet "$unit"; then exit 5; fi
echo "SYSTEM_UNIT_VM_PASS cgroup_system_slice=YES user_session_independent=YES live_kernel_io=YES worker_mock=YES no_global_swap=YES"
