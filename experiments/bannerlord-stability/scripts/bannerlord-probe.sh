#!/usr/bin/env bash
# Nopen: deliberately NO SWAP/NBD enable. Bannerlord A/B diagnostic runner.
set -Eeuo pipefail
MODE="${1:-}"
SECONDS_TO_RUN="${2:-300}"
if [[ ! "$MODE" =~ ^(baseline|opencl|nbd-raw)$ ]] || ! [[ "$SECONDS_TO_RUN" =~ ^[0-9]+$ ]] || (( SECONDS_TO_RUN < 5 || SECONDS_TO_RUN > 1800 )); then
  echo 'Usage: ./bannerlord-probe.sh baseline|opencl|nbd-raw [seconds: 5..1800]' >&2
  exit 2
fi
BASE="${NOPEN_TEST_DIR:-$HOME/nopen-vram-experiment/bannerlord-safe-tests}"
mkdir -p "$BASE"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="$BASE/${STAMP}-${MODE}"
mkdir -p "$OUT"
START="$(date --iso-8601=seconds)"
printf '%s\n' "$START" > "$OUT/start-time.txt"
{ date --iso-8601=seconds; uname -a; printf 'Mode=%s Duration=%ss\n' "$MODE" "$SECONDS_TO_RUN"; printf 'BOOT_ID='; cat /proc/sys/kernel/random/boot_id; printf 'PCI='; lspci -s 03:00.0 2>/dev/null || true; swapon --show; free -h; } > "$OUT/preflight.txt" 2>&1

# A healthy baseline needs the same swap policy as a healthy OpenCL trial.
# Refuse rather than silently testing with NBD, SSD or unexpected swap.
check_swap() {
  local unsafe
  unsafe="$(awk 'NR>1 && $1 !~ /^\/dev\/zram[0-9]+$/ {print $1}' /proc/swaps)"
  if [[ -n "$unsafe" ]]; then
    echo "REFUSED: non-ZRAM swap active: $unsafe" >&2
    echo 'Do not kill an NBD backend while its swap remains active. Perform a safe swapoff first.' >&2
    return 1
  fi
}
check_swap || exit 3
for u in nopen-vram-v091 nopen-vram-v092 nopen-vram-v093; do
  if systemctl -q is-active "$u.service"; then
    if [[ "$MODE" != nbd-raw || "$u" != nopen-vram-v093 ]]; then
      echo "REFUSED: existing Nopen unit active: $u. Do not mix backends." >&2
      exit 3
    fi
  fi
done

BENCH="$HOME/nopen-opencl-test/build-opencl/nopen-opencl-bench"
RAW="$HOME/nopen-vram-experiment/nbd-beta-093/Nopen_VRAM_NBD_v0.9.3/test-random4k-direct.py"
if [[ "$MODE" == opencl && ! -x "$BENCH" ]]; then
  echo "Missing $BENCH. Build the existing OpenCL benchmark first." >&2; exit 2
fi
if [[ "$MODE" == nbd-raw ]]; then
  if [[ ! -f "$RAW" ]]; then echo "Missing $RAW. Refusing raw IO." >&2; exit 2; fi
  if ! systemctl -q is-active nopen-vram-v093.service; then echo 'v0.9.3 raw-only backend must be running; do not enable its swap.' >&2; exit 2; fi
  if [[ ! -b /dev/nbd15 ]] || [[ "$(cat /sys/block/nbd15/size 2>/dev/null || echo 0)" != '4194304' ]]; then
    echo 'NBD15 is not a connected 2 GiB block device. Refusing.' >&2; exit 2
  fi
  if [[ -n "$(lsblk -n -o MOUNTPOINTS /dev/nbd15 2>/dev/null | tr -d '[:space:]')" ]]; then
    echo 'NBD15 appears mounted. Refusing destructive raw test.' >&2; exit 2
  fi
  if ! sudo -v; then echo 'Need sudo for raw O_DIRECT tests.' >&2; exit 2; fi
fi

KLOG_PID=''; WORK_PID=''; CLEANED=0
cleanup() {
  local rc=$?
  if (( CLEANED )); then return; fi
  CLEANED=1
  trap - EXIT INT TERM
  if [[ -n "$WORK_PID" ]]; then
    pkill -TERM -P "$WORK_PID" 2>/dev/null || true
    kill "$WORK_PID" 2>/dev/null || true
    wait "$WORK_PID" 2>/dev/null || true
  fi
  if [[ -n "$KLOG_PID" ]]; then
    kill "$KLOG_PID" 2>/dev/null || true
    wait "$KLOG_PID" 2>/dev/null || true
  fi
  { date --iso-8601=seconds; swapon --show; free -h; } > "$OUT/postflight.txt" 2>&1 || true
  journalctl -b -k --since "$START" --no-pager -o short-iso > "$OUT/kernel-since-start.log" 2>&1 || true
  journalctl -b --since "$START" -u nopen-vram-v093.service --no-pager -o short-iso > "$OUT/nopen-unit-since-start.log" 2>&1 || true
  echo "exit_code=$rc" > "$OUT/result.txt"
  tar -czf "$OUT.tar.gz" -C "$BASE" "$(basename "$OUT")" 2>/dev/null || true
  printf '\nTEST COMPLETE mode=%s exit_code=%s\nLOG FOLDER: %s\nARCHIVE: %s.tar.gz\n' "$MODE" "$rc" "$OUT" "$OUT"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Journald is persistent after a power cycle on systems where persistent logging is enabled.
# Streaming is a convenience and not a substitute for the after-reboot collection.
if journalctl -b -k -n 0 -o short-iso >/dev/null 2>&1; then
  journalctl -b -k -f -n 0 -o short-iso > "$OUT/kernel-live.log" 2>&1 &
  KLOG_PID=$!
else
  echo 'NOTE: kernel journal not accessible as current user. Run collect-after-reboot.sh with sudo if needed.' | tee -a "$OUT/notes.log"
fi

if [[ "$MODE" == opencl ]]; then
  (
    n=0
    while :; do
      n=$((n+1))
      echo "===== OpenCL iteration $n $(date --iso-8601=seconds) ====="
      # Intentionally small allocation, short-run benchmark, NO SWAP.
      if ! timeout --signal=TERM --kill-after=3s 25s "$BENCH" \
          --mib 256 --test-mib 64 --chunk-mib 8 --seg-mib 256 \
          --pages 128 --host pageable; then
        echo "OPENCL_BENCH_FAILED iteration=$n" >&2
        break
      fi
      sleep 8
    done
  ) > "$OUT/workload.log" 2>&1 &
  WORK_PID=$!
elif [[ "$MODE" == nbd-raw ]]; then
  (
    n=0
    while :; do
      n=$((n+1))
      if ! check_swap || ! systemctl -q is-active nopen-vram-v093.service; then
        echo 'RAW TEST STOPPED: unsafe swap state or disconnected daemon.' >&2; break
      fi
      echo "===== RAW NBD iteration $n $(date --iso-8601=seconds) ====="
      if ! timeout --signal=TERM --kill-after=3s 30s sudo -n python3 "$RAW" --count 128; then
        echo "RAW_NBD_FAILED iteration=$n" >&2; break
      fi
      sleep 10
    done
  ) > "$OUT/workload.log" 2>&1 &
  WORK_PID=$!
fi

printf 't_epoch,mem_available_kib,psi_some_avg10,psi_full_avg10,pswpin_pages,pswpout_pages,zram_used_kib,nbd_used_kib,gpu_vram_used_bytes,nbd_read_ios,nbd_write_ios\n' > "$OUT/metrics.csv"
printf 'TEST %s | %s seconds\n' "$MODE" "$SECONDS_TO_RUN"
printf 'Now launch Bannerlord, continue the saved game, enter the world and change workspaces a few times.\n'
printf 'This runner never enables swap, changes swap priority, or disconnects an NBD backend.\n'
printf 'Progress logs: %s\n\n' "$OUT"
for ((i=0;i<SECONDS_TO_RUN;i++)); do
  if ! check_swap; then echo 'Unsafe swap became active: diagnostic stopping.' | tee -a "$OUT/notes.log"; exit 3; fi
  t="$(date +%s)"
  avail="$(awk '/^MemAvailable:/ {print $2}' /proc/meminfo)"
  psis="$(awk '/^some / {sub("avg10=","",$2);print $2}' /proc/pressure/memory 2>/dev/null || echo n/a)"
  psif="$(awk '/^full / {sub("avg10=","",$2);print $2}' /proc/pressure/memory 2>/dev/null || echo n/a)"
  pin="$(awk '$1=="pswpin" {print $2}' /proc/vmstat)"
  pout="$(awk '$1=="pswpout" {print $2}' /proc/vmstat)"
  zram="$(awk 'NR>1 && $1=="/dev/zram0" {print $4}' /proc/swaps)"
  nbd="$(awk 'NR>1 && $1=="/dev/nbd15" {print $4}' /proc/swaps)"
  vram=0
  for f in /sys/class/drm/card*/device/mem_info_vram_used; do
    if [[ -r "$f" ]]; then vram="$(cat "$f")"; break; fi
  done
  nioread=0; niowrite=0
  if [[ -r /sys/block/nbd15/stat ]]; then
    read -r nioread _ _ _ niowrite _ < /sys/block/nbd15/stat || true
  fi
  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' "$t" "${avail:-0}" "${psis:-0}" "${psif:-0}" "${pin:-0}" "${pout:-0}" "${zram:-0}" "${nbd:-0}" "$vram" "$nioread" "$niowrite" >> "$OUT/metrics.csv"
  if (( i%30==0 )); then
    printf '%s elapsed=%ss available=%sKiB psi_full=%s nbd_swap=%sKiB\n' "$(date +%T)" "$i" "$avail" "$psif" "${nbd:-0}"
    { date --iso-8601=seconds; pgrep -af 'Bannerlord|bannerlord|TaleWorlds' || true; } >> "$OUT/game-processes.log"
  fi
  if [[ "$MODE" == nbd-raw ]] && ! systemctl -q is-active nopen-vram-v093.service; then
    echo 'NBD server stopped: ending diagnostic.' | tee -a "$OUT/notes.log"; exit 4
  fi
  sleep 1
done
printf 'SUCCESS: diagnostic window finished. Inspect whether the world loaded without freezes.\n'