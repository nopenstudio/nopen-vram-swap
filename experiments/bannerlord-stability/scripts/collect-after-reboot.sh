#!/usr/bin/env bash
# Read-only journal collection. Does not modify devices, swap, GPU, or services.
set -Eeuo pipefail
BASE="${NOPEN_TEST_DIR:-$HOME/nopen-vram-experiment/bannerlord-safe-tests}"
STAMP="$(date +%Y%m%d-%H%M%S)"
DIR="$BASE/after-reboot-$STAMP"
mkdir -p "$DIR"
{ date --iso-8601=seconds; uname -a; journalctl --list-boots --no-pager; swapon --show; free -h; } > "$DIR/system.txt" 2>&1 || true
jread() {
  if journalctl "$@" 2>/dev/null; then return 0; fi
  if command -v sudo >/dev/null 2>&1; then sudo -n journalctl "$@"; else return 1; fi
}
for BOOT in -1 0; do
  jread -b "$BOOT" -k -o short-iso --no-pager > "$DIR/kernel-boot${BOOT}.log" 2>&1 || true
  jread -b "$BOOT" --no-pager -o short-iso \
    -u nopen-vram-v093.service -u nopen-vram-v093-monitor.service \
    -u nopen-vram-v092.service -u nopen-vram-v092-monitor.service \
    > "$DIR/nopen-boot${BOOT}.log" 2>&1 || true
  jread -b "$BOOT" --no-pager -o short-iso \
    --grep='SIGBUS|segfault|coredump|nbd15|GPU reset|amdgpu|oom|swap-device|Sway|sway' \
    > "$DIR/events-boot${BOOT}.log" 2>&1 || true
done
for f in /var/log/nopen-vram-nbd-v092.log /var/log/nopen-vram-nbd-v093.log; do
  if [[ -r "$f" ]]; then cp "$f" "$DIR/$(basename "$f")"; fi
done
for f in /var/log/nopen-vram-nbd-v092-telemetry.log /var/log/nopen-vram-nbd-v093-telemetry.log; do
  if [[ -r "$f" ]]; then cp "$f" "$DIR/$(basename "$f")"; fi
done
(cd "$BASE" && tar -czf "after-reboot-$STAMP.tar.gz" "after-reboot-$STAMP")
printf 'Saved: %s/after-reboot-%s.tar.gz\n' "$BASE" "$STAMP"
printf 'Logs may include user/process names, memory addresses and private metadata. Review before publishing.\n'