#!/usr/bin/env bash
# Create disposable Btrfs loop filesystem, verify real NOCOW O_DIRECT shadow,
# simulate GPU-worker timeout and verify checked fallback. NEVER touch host disk.
set -euo pipefail
[[ $# -eq 2 ]] || { echo "usage: $0 test-shadow-bin worker-bin" >&2; exit 2; }
testbin="$(realpath "$1")"; worker="$(realpath "$2")"
[[ "$EUID" == 0 && "${GITHUB_ACTIONS:-}" == true &&
    "${NOPEN_ISOLATED_VM_TEST:-}" == I_ACKNOWLEDGE_DISPOSABLE_VM ]] ||
  { echo "V070_BTRFS_VM_REFUSED requires disposable GitHub VM" >&2; exit 2; }
[[ -x "$testbin" && -x "$worker" ]] || exit 2
vendor="$(cat /sys/class/dmi/id/sys_vendor 2>/dev/null || true)"
product="$(cat /sys/class/dmi/id/product_name 2>/dev/null || true)"
printf '%s\n' "$vendor $product" | grep -Eiq \
  'QEMU|KVM|VMware|Hyper-V|Virtual Machine|Microsoft Corporation|Bochs|Amazon EC2' ||
  { echo "V070_BTRFS_VM_REFUSED not a recognized VM" >&2; exit 2; }
command -v mkfs.btrfs >/dev/null
command -v btrfs >/dev/null
dir="$(mktemp -d /var/tmp/nopen-btrfs-v070-XXXXXX)"
image="$dir/fs.img";mnt="$dir/mount";mounted=0
mkdir -m 0700 "$mnt"
cleanup() {
  if (( mounted ));then umount "$mnt" || true;fi
  rm -rf -- "$dir"
}
trap cleanup EXIT
# Sparse file, loop device and mount are all scoped to the disposable VM.
truncate -s 512M "$image"
mkfs.btrfs -f -d single -m single "$image" >"$dir/mkfs.log"
mount -t btrfs -o loop "$image" "$mnt"
mounted=1
[[ "$(stat -f -c '%T' "$mnt")" == btrfs ]] || exit 3
btrfs subvolume create "$mnt/outer"
btrfs subvolume create "$mnt/outer/shadow-subvolume"
btrfs subvolume show "$mnt/outer/shadow-subvolume" >/dev/null
"$testbin" "$worker" "$mnt/outer/shadow-subvolume" >"$dir/test.log" 2>&1 || {
  cat "$dir/test.log" >&2; exit 4;
}
grep -q V070_BTRFS_NOCOW_FLAG_PASS "$dir/test.log" || {
  cat "$dir/test.log" >&2
  echo "V070_BTRFS_VM_FAILED missing runtime NOCOW verification" >&2
  exit 5
}
grep -q V070_SHADOW_TEST_PASS "$dir/test.log" || {
  cat "$dir/test.log" >&2; exit 5;
}
# A parent's regular subvolume snapshot must NOT contain nested child's data.
# This mitigates auto-snapshotting of the host root subvolume, not direct
# snapshots or reflinks deliberately made of the Nopen child subvolume.
printf 'isolated-storage\n' > "$mnt/outer/shadow-subvolume/marker"
btrfs subvolume snapshot -r "$mnt/outer" "$mnt/parent-snapshot"
if [[ -f "$mnt/parent-snapshot/shadow-subvolume/marker" ]]; then
  echo "V070_BTRFS_VM_FAILED child subvolume data included in parent snapshot" >&2
  exit 6
fi
rm "$mnt/outer/shadow-subvolume/marker"
btrfs subvolume delete "$mnt/parent-snapshot" >/dev/null
btrfs subvolume delete "$mnt/outer/shadow-subvolume" >/dev/null
btrfs subvolume delete "$mnt/outer" >/dev/null
echo "V070_BTRFS_VM_PASS real_btrfs_filesystem=YES nocow_flag_verified=YES"
echo "V070_BTRFS_VM_PASS_AFTER_RECOVERY checked_pages=1024 worker_timeout=YES"
echo "V070_BTRFS_VM_PASS_AFTER_SNAPSHOT parent_snapshot_excludes_shadow=YES"
echo "V070_BTRFS_VM_NOTE physical_amdgpu_reclaim_not_tested=YES"
