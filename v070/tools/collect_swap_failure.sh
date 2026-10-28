#!/usr/bin/env bash
# Read-only Nopen v0.7 failure collector. Safe after a forced reboot.
# Never activates swap, loads kernel modules, resets GPU or changes sysctl.
set -u
stamp="$(date +%Y%m%d-%H%M%S)"
dest="${HOME}/nopen-v070-swap-failure-${stamp}.txt"
{
  printf 'NOPEN_V070_FAILURE_DIAGNOSTICS collected_at=%s\n' "$(date -Is)"
  printf '\n== system ==\n'
  uname -a
  printf '\n== recent kernel boots ==\n'
  journalctl --list-boots --no-pager 2>&1 | tail -n 20
  printf '\n== active swaps NOW (do not re-enable experimental swap) ==\n'
  swapon --show 2>&1
  printf '\n== kernel block devices NOW ==\n'
  lsblk -o NAME,MAJ:MIN,SIZE,TYPE,MOUNTPOINTS 2>&1
  printf '\n== previous-boot kernel errors and surrounding entries ==\n'
  journalctl -k -b -1 --no-pager -o short-precise 2>&1 |
    grep -Ei -C 6 'swap-device|amdgpu|ublk|nopen|GPU reset|timeout|page allocation failure|oom.kill|out of memory|I/O error|failed to (read|write)' |
    tail -n 1400 || true
  printf '\n== current boot kernel state (errors only) ==\n'
  journalctl -k -b 0 --no-pager -o short-precise 2>&1 |
    grep -Ei 'swap-device|amdgpu.*(err|fault|reset|timeout)|ublk.*(err|fault|fail)|out of memory|oom.kill' |
    tail -n 200 || true
  printf '\n== persistent Nopen logs if present ==\n'
  for f in /run/nopen-v070/log /var/log/nopen-v070/log; do
    if [[ -r "$f" ]]; then
      echo "=== $f ==="
      tail -n 400 "$f"
    else
      printf '%s not accessible (normal after reboot for /run)\n' "$f"
    fi
  done
  printf '\n== AMD PCI and kernel module ==\n'
  (lspci -nnk 2>&1 | grep -Ei -A 6 'VGA|Display controller|3D controller' | head -n 60) || true
  printf '\nEND diagnostics read_only=YES experiment_not_restarted=YES\n'
} > "$dest" 2>&1
printf 'NOPEN_DIAGNOSTICS_SAVED file=%s read_only=YES swap_untouched=YES\n' "$dest"
