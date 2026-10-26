#!/usr/bin/env bash
# Explicit, opt-in 8 GiB NBD/VRAM swap research profile. NOT production-safe.
# Never force-kills an active swap backend. Never creates SSD/NVMe swap.
set -Eeuo pipefail
ACTION="${1:-help}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd -- "$HERE/../.." && pwd)"
ARCHIVE="$REPO/legacy/archives/Nopen_VRAM_NBD_v0.9.3_NBD_Stall_Diagnostics.zip"
WORK="${NOPEN_8G_WORK:-$HOME/nopen-vram-experiment/nbd-8g-experiment}"
SRC="$WORK/backend-8g"
LOGS="$WORK/reports"
DEVICE=/dev/nbd15

only_zram() {
  if ! awk 'NR==1 {next} $1!="/dev/zram0" {bad=1} $1=="/dev/zram0" && $5==100 {ok=1} END {exit !(ok && !bad)}' /proc/swaps; then
    echo 'REFUSED: active swaps must be /dev/zram0 priority 100 only.' >&2
    swapon --show >&2; return 1
  fi
}
precheck() {
  only_zram
  [[ -b "$DEVICE" ]] || { echo 'REFUSED: /dev/nbd15 is not a block device; modprobe nbd first.' >&2; return 1; }
  [[ "$(cat /sys/block/nbd15/size 2>/dev/null || echo 1)" == 0 ]] || { echo 'REFUSED: NBD15 is connected.' >&2; return 1; }
  for u in nopen-vram-v091 nopen-vram-v092 nopen-vram-v093; do
    if systemctl -q is-active "$u.service"; then echo "REFUSED: $u already active" >&2; return 1; fi
  done
}
mem_avail() { awk '/^MemAvailable:/ {printf "%.0f",$2/1024}' /proc/meminfo; }
check_prepared() {
  [[ -x "$SRC/nopen-vram-nbd" && -f "$SRC/NOPEN_8G_PROFILE.txt" ]] || {
    echo 'Run: real-swap-8g.sh prepare (8 GiB profile missing)' >&2; return 1;
  }
}
check_active() {
  [[ "$(cat /sys/block/nbd15/size 2>/dev/null || echo 0)" == 16777216 ]] || {
    echo 'REFUSED: NBD15 is not 8 GiB.' >&2; return 1;
  }
  systemctl -q is-active nopen-vram-v093.service || { echo 'REFUSED: backend not active.' >&2; return 1; }
  if ! grep -Eq '^/dev/nbd15[[:space:]]' /proc/swaps; then
    echo 'REFUSED: real NBD swap not active.' >&2; return 1
  fi
}
case "$ACTION" in
  prepare)
    only_zram
    for command in python3 unzip make cc sudo; do command -v "$command" >/dev/null || { echo "Missing: $command" >&2; exit 2; }; done
    sudo modprobe nbd nbds_max=16 max_part=0
    precheck
    mkdir -p "$WORK" "$LOGS"
    if [[ ! -e "$SRC" ]]; then
      python3 "$HERE/build-profile.py" --archive "$ARCHIVE" --output "$SRC"
    fi
    [[ -f "$SRC/NOPEN_8G_PROFILE.txt" ]] || { echo 'Bad profile; refuse.' >&2; exit 2; }
    # Heal profiles created by the initial build script, which lost ZIP execute bits.
    for script in start-v093.sh enable-v093.sh disable-v093.sh monitor-v093.sh \
                  doctor-v093.sh focus-probe-v093.sh run-isolated-cycle.sh \
                  collect-after-reboot.sh; do
      [[ -f "$SRC/$script" ]] || { echo "Missing backend script: $script" >&2; exit 2; }
      chmod 755 "$SRC/$script"
    done
    (cd "$SRC" && make clean && make -j2 && make test)
    echo 'PREPARED: 8 GiB code compiled and tests passed. NO real swap enabled.'
    ;;
  start)
    precheck
    check_prepared
    (( $(mem_avail) >= 3500 )) || { echo 'REFUSED: RAM availability <3500 MiB.' >&2; exit 2; }
    # Do not let a low-memory game compete for half of GPU VRAM at start.
    if pgrep -af '[Bb]annerlord|TaleWorlds' | grep -v 'real-swap-8g.sh' >/dev/null; then
      echo 'REFUSED: Bannerlord already running; reserve VRAM before launching the game.' >&2; exit 2
    fi
    echo 'HIGH RISK: reserves 8 GiB of RX 9060 XT VRAM and activates GLOBAL Linux swap.'
    echo 'If NBD fails, even unrelated desktop programs may SIGBUS/freeze; save work first.'
    echo 'Do not attempt to fill all 8 GiB in this 8 GiB RAM desktop.'
    (cd "$SRC" && ./start-v093.sh && ./enable-v093.sh)
    check_active
    echo 'ACTIVE: /dev/nbd15 8 GiB SWAP. Backend MUST remain running until swapoff.'
    swapon --show
    ;;
  test|test-8g)
    check_prepared
    check_active
    (( $(mem_avail) >= 3500 )) || { echo 'REFUSED: RAM availability <3500 MiB.' >&2; exit 2; }
    if [[ -n "$(awk 'NR>1 && $1!="/dev/nbd15" && $1!="/dev/zram0" {print $1}' /proc/swaps)" ]]; then
      echo 'REFUSED: unexpected disk swap active.' >&2; exit 2
    fi
    mkdir -p "$LOGS"
    stamp="$(date +%Y%m%d-%H%M%S)"
    cycles=1
    if [[ "$ACTION" == test-8g ]]; then cycles=16; fi
    echo "REAL SWAP: 8 GiB NBD capacity; 512 MiB pages per isolated iteration; $cycles iterations (cumulative $((cycles*512)) MiB)."
    echo 'This is NOT an 8 GiB simultaneously resident memory-pressure test.'
    overall="$LOGS/${stamp}-${ACTION}-summary.log"
    printf 'action=%s capacity_mib=8192 cycle_mib=512 iterations=%d\n' "$ACTION" "$cycles" | tee "$overall"
    for ((iteration=1; iteration<=cycles; iteration++)); do
      if ! systemctl -q is-active nopen-vram-v093.service || ! grep -Eq '^/dev/nbd15[[:space:]]' /proc/swaps; then
        echo "CRITICAL: backend/swap became inactive at iteration $iteration; aborting." | tee -a "$overall" >&2
        exit 4
      fi
      if (( $(mem_avail) < 2500 )); then
        echo "REFUSED: MemAvailable below 2500 MiB at iteration $iteration." | tee -a "$overall" >&2
        exit 4
      fi
      echo "=== ITERATION $iteration/$cycles $(date --iso-8601=seconds) ===" | tee -a "$overall"
      logfile="$LOGS/${stamp}-${ACTION}-${iteration}.log"
      set +e
      systemd-run --user --scope --collect \
        --property=MemoryAccounting=yes --property=MemoryMax=384M --property=MemorySwapMax=768M \
        /usr/bin/python3 -u "$SRC/test-real-swap-cycle.py" \
        --mib 512 --cycles 1 --chunk-mib 32 \
        --i-accept-crash-risk --require-isolated-memory \
        2>&1 | tee "$logfile"
      rc=${PIPESTATUS[0]}
      set -e
      if ((rc != 0)) || ! grep -q '^\[TEST_PASS\]' "$logfile"; then
        echo "FAILED iteration=$iteration rc=$rc; do not stop backend before swapoff." | tee -a "$overall" >&2
        exit 5
      fi
      printf 'PASS iteration=%d/%d, cumulative_verified_mib=%d\n' "$iteration" "$cycles" "$((iteration*512))" | tee -a "$overall"
      sleep 1
    done
    echo "TEST_COMPLETE total_verified_mib=$((cycles*512)) iterations=$cycles (separate page sets; not simultaneous)." | tee -a "$overall"
    ;;
  watch)
    check_active
    mkdir -p "$LOGS"
    stamp="$(date +%Y%m%d-%H%M%S)"
    out="$LOGS/bannerlord-8g-${stamp}.csv"
    echo 'timestamp,mem_avail_mib,psi_some10,psi_full10,zram_used_kib,nbd_used_kib,pswpin_pages,pswpout_pages,nbd_read_ios,nbd_write_ios,vram_used_mib,backend_active' > "$out"
    echo "WATCHING FOR 600s. Now launch Bannerlord (saved game -> world). Log: $out"
    for ((sec=0;sec<600;sec++)); do
      ma="$(mem_avail)"
      some="$(awk '/^some / {sub("avg10=","",$2);print $2}' /proc/pressure/memory)"
      full="$(awk '/^full / {sub("avg10=","",$2);print $2}' /proc/pressure/memory)"
      zr="$(awk 'NR>1 && $1=="/dev/zram0" {print $4}' /proc/swaps)"
      ns="$(awk 'NR>1 && $1=="/dev/nbd15" {print $4}' /proc/swaps)"
      pin="$(awk '$1=="pswpin" {print $2}' /proc/vmstat)"
      pout="$(awk '$1=="pswpout" {print $2}' /proc/vmstat)"
      rd=0; wr=0
      if [[ -r /sys/block/nbd15/stat ]]; then read -r rd _ _ _ wr _ < /sys/block/nbd15/stat || true; fi
      vb=0
      for f in /sys/class/drm/card*/device/mem_info_vram_used; do
        [[ -r "$f" ]] || continue
        vb="$(awk '{printf "%.0f", $1 / 1048576}' "$f")"; break
      done
      active=1
      if ! systemctl -q is-active nopen-vram-v093.service; then active=0; fi
      echo "$(date +%s),$ma,${some:-0},${full:-0},${zr:-0},${ns:-0},$pin,$pout,$rd,$wr,$vb,$active" >> "$out"
      if ((sec % 10 == 0)); then
        printf 't=%ss MemAvailable=%sMiB NBDswap=%sKiB VRAMused=%sMiB backend=%s\n' "$sec" "$ma" "${ns:-0}" "$vb" "$active"
      fi
      if (( active != 1 )); then
        echo 'CRITICAL: BACKEND INACTIVE. Do not restart or kill processes. Capture journal after reboot.' >&2
        break
      fi
      if [[ -z "$ns" ]]; then
        echo 'CRITICAL: NBD SWAP missing during watch. Stopping observation.' >&2
        break
      fi
      sleep 1
    done
    echo "Saved: $out; close Bannerlord and then safely stop NBD swap."
    ;;
  stop)
    check_prepared
    echo 'Safe stop: swapoff FIRST, then backend service shutdown.'
    (cd "$SRC" && ./disable-v093.sh)
    echo 'Verify only zram is active:'
    swapon --show
    ;;
  status)
    swapon --show
    free -h
    lsblk -o NAME,SIZE,TYPE,MOUNTPOINTS /dev/nbd15 2>/dev/null || true
    printf 'Backend: '; systemctl is-active nopen-vram-v093.service 2>/dev/null || true
    printf 'Available RAM MiB: '; mem_avail; echo
    ;;
  report)
    mkdir -p "$LOGS"
    stamp="$(date +%Y%m%d-%H%M%S)"
    out="$WORK/nopen-bannerlord-8g-report-${stamp}.tar.gz"
    sudo journalctl -u nopen-vram-v093.service -b --no-pager > "$LOGS/backend-journal.txt" || true
    sudo journalctl -k -b --no-pager > "$LOGS/kernel-journal.txt" || true
    sudo cp /var/log/nopen-vram-nbd-v093.log "$LOGS/backend-events.log" 2>/dev/null || true
    sudo cp /var/log/nopen-vram-nbd-v093-telemetry.log "$LOGS/telemetry.log" 2>/dev/null || true
    sudo chown "$(id -u):$(id -g)" "$LOGS"/* 2>/dev/null || true
    (cd "$WORK" && tar -czf "$out" reports)
    echo "REPORT: $out"
    ;;
  help|--help|-h)
    cat <<'USAGE'
Usage: bash real-swap-8g.sh prepare|start|test|test-8g|watch|stop|status|report
  prepare  Build an independent v0.9.3-based 8 GiB profile (NO SWAP)
  start    Explicitly start OpenCL + enable GLOBAL 8 GiB NBD SWAP (HIGH RISK)
  test     512 MiB REAL SWAP-IN/OUT while 8 GiB capacity is active
  test-8g 16 x 512 MiB = 8 GiB cumulative VERIFIED real swap
  watch    Observe Bannerlord for <=600s without modifying active swap
  stop     SWAPOFF FIRST, then stop NBD daemon
  status   Read-only status
  report   Collect kernel, backend and telemetry logs
Do not run on unsaved work. 8 GiB swap is NOT an extra 8 GiB of RAM.
USAGE
    ;;
  *) echo 'Unknown action' >&2; exit 2;;
esac