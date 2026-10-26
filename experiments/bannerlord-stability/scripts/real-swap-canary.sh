#!/usr/bin/env bash
# Nopen v0.9.3: deliberately opt-in REAL swap experiment on /dev/nbd15.
# The only swap-related writes are performed by the audited v0.9.3 enable script.
# WARNING: cgroup isolation does not isolate the kernel swap device from the host.
set -Eeuo pipefail
MODE="${1:-}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$HERE/../../.." && pwd)"
ARCHIVE="$ROOT/legacy/archives/Nopen_VRAM_NBD_v0.9.3_NBD_Stall_Diagnostics.zip"
WORK="$HOME/nopen-vram-experiment/nbd-beta-093-canary"
DIR="$WORK/Nopen_VRAM_NBD_v0.9.3"
LOGS="$WORK/canary-logs"

usage() {
  cat <<'HELP'
Usage: ./real-swap-canary.sh prepare|start|test|stop|status|report
 prepare: build archived v0.9.3 backend, unit tests, NBD kernel module
 start: explicitly activate REAL VRAM swap /dev/nbd15 (HIGH RISK)
 test:  512 MiB / one real Linux page-out/page-in cycle in memory cgroup
 stop:  swapoff FIRST, then stop backend (can block if kernel is wedged)
 status: show live device and memory stats (read-only)
 report: collect kernel/NBD diagnostics into tar.gz (read-only)
Do NOT run 'start' until unsaved work is closed. This can hang or crash the desktop.
HELP
}

only_zram() {
  if ! awk 'NR==1 {next} $1!="/dev/zram0" {bad=1} $1=="/dev/zram0" && $5==100 {seen=1} END {exit !(seen && !bad)}' /proc/swaps; then
    echo 'REFUSED: expected only /dev/zram0 priority=100 in active swap devices.' >&2
    swapon --show; return 1
  fi
}

mem_avail_mib() {
  awk '/^MemAvailable:/ {printf "%d", $2 / 1024}' /proc/meminfo
}

no_legacy_services() {
  local unit
  for unit in nopen-vram-v091.service nopen-vram-v092.service; do
    if systemctl -q is-active "$unit"; then
      echo "REFUSED: old Nopen backend active: $unit" >&2; return 1
    fi
  done
}

require_compiled() {
  if [[ ! -x "$DIR/nopen-vram-nbd" ]]; then
    echo 'Backend missing; run: ./real-swap-canary.sh prepare' >&2; return 1
  fi
}

case "$MODE" in
  prepare)
    only_zram
    no_legacy_services
    [[ -f "$ARCHIVE" ]] || { echo "Archive missing: $ARCHIVE" >&2; exit 1; }
    for exe in unzip make cc python3 sudo; do
      command -v "$exe" >/dev/null || { echo "Missing command: $exe" >&2; exit 1; }
    done
    mkdir -p "$WORK" "$LOGS"
    if [[ ! -f "$DIR/nopen-vram-nbd.c" ]]; then
      unzip -q "$ARCHIVE" -d "$WORK"
    fi
    cd "$DIR"
    make clean && make -j2 && make test
    sudo modprobe nbd nbds_max=16 max_part=0
    [[ -b /dev/nbd15 ]] || { echo 'No /dev/nbd15; refusing.' >&2; exit 1; }
    ./doctor-v093.sh
    echo "PREPARED: $DIR"
    echo 'No VRAM swap has been enabled.'
    ;;
  start)
    only_zram
    no_legacy_services
    require_compiled
    [[ "$(mem_avail_mib)" -ge 3500 ]] || { echo 'REFUSED: MemAvailable below 3500 MiB; close workloads.' >&2; exit 1; }
    [[ -b /dev/nbd15 ]] || { echo 'Missing /dev/nbd15, run prepare.' >&2; exit 1; }
    if [[ "$(cat /sys/block/nbd15/size 2>/dev/null || echo 1)" != 0 ]]; then
      echo 'REFUSED: NBD15 already connected; do not overwrite.' >&2; exit 1
    fi
    if systemctl -q is-active nopen-vram-v093.service; then
      echo 'REFUSED: Nopen v0.9.3 service already active.' >&2; exit 1
    fi
    mkdir -p "$LOGS"
    cd "$DIR"
    printf '\nHIGH RISK: this will activate 2 GiB REAL global Linux SWAP.\n'
    printf 'If NBD fails, Linux can lose swapped pages and crash other programs.\n'
    printf 'Save work; close Bannerlord and Steam for the first canary.\n'
    ./start-v093.sh
    ./enable-v093.sh   # requires literal interactive confirmation
    echo 'ACTIVE: do not kill/stop/disconnect Nopen while swap is enabled.'
    swapon --show
    ;;
  test)
    require_compiled
    no_legacy_services
    [[ -e /sys/fs/cgroup/cgroup.controllers ]] || { echo 'REFUSED: needs cgroup v2.' >&2; exit 1; }
    [[ "$(mem_avail_mib)" -ge 3500 ]] || { echo 'REFUSED: available RAM below 3500 MiB.' >&2; exit 1; }
    grep -Eq '^/dev/nbd15[[:space:]]' /proc/swaps || { echo 'REFUSED: NBD15 is NOT active SWAP. First start.' >&2; exit 1; }
    systemctl -q is-active nopen-vram-v093.service || { echo 'REFUSED: backend inactive.' >&2; exit 1; }
    if ! systemctl --user show-environment >/dev/null 2>&1; then
      echo 'REFUSED: user systemd manager required for isolated test.' >&2; exit 1
    fi
    mkdir -p "$LOGS"
    stamp="$(date +%Y%m%d-%H%M%S)"
    echo "Real pageout/pagein test starting; log=$LOGS/$stamp-cycle-512.log"
    echo 'This is a GLOBAL NBD swap device; the cgroup protects only test-process memory usage.'
    set +e
    systemd-run --user --scope --collect \
      --property=MemoryAccounting=yes \
      --property=MemoryMax=384M \
      --property=MemorySwapMax=768M \
      /usr/bin/python3 -u "$DIR/test-real-swap-cycle.py" \
      --mib 512 --cycles 1 --chunk-mib 32 \
      --i-accept-crash-risk --require-isolated-memory \
      2>&1 | tee "$LOGS/$stamp-cycle-512.log"
    status=${PIPESTATUS[0]}
    set -e
    echo "TEST_EXIT=$status" | tee -a "$LOGS/$stamp-cycle-512.log"
    swapon --show
    sudo journalctl -u nopen-vram-v093.service -n 100 --no-pager > "$LOGS/$stamp-backend.log" || true
    sudo journalctl -k -n 150 --no-pager > "$LOGS/$stamp-kernel.log" || true
    if (( status != 0 )); then
      echo 'Test did NOT pass; collect report. Do NOT kill backend while NBD swap is active.' >&2
      exit "$status"
    fi
    echo "PASS candidate: inspect test-runs/events.jsonl; then run ./real-swap-canary.sh stop"
    ;;
  stop)
    require_compiled
    echo 'Using official v0.9.3 disable; swapoff happens BEFORE service stop.'
    cd "$DIR"
    ./disable-v093.sh
    ;;
  status)
    echo '=== ACTIVE SWAP ==='; swapon --show
    echo '=== RAM ==='; free -h
    echo '=== NBD15 ==='; lsblk -o NAME,SIZE,TYPE,MOUNTPOINTS /dev/nbd15 2>/dev/null || true
    echo '=== BACKEND ==='; systemctl is-active nopen-vram-v093.service 2>/dev/null || true
    echo '=== MemAvailable MiB ==='; mem_avail_mib; echo
    ;;
  report)
    [[ -f "$DIR/collect-after-reboot.sh" ]] || { echo 'Run prepare first.' >&2; exit 1; }
    (cd "$DIR" && ./collect-after-reboot.sh)
    echo "Also attach logs from: $LOGS"
    ;;
  *) usage; [[ -z "$MODE" || "$MODE" == help || "$MODE" == --help ]] || exit 2 ;;
esac