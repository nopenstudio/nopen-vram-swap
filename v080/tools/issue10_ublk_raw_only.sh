#!/usr/bin/env bash
# Issue #10: isolated physical ublk RAW I/O pilot. NO filesystem, mkswap, swapon
# or kernel SWAP. NOT risk-free: OpenCL/ublk can still hang the GPU/host.
# Operates exclusively on a newly created /dev/ublkb23 with owner service.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
action="${1:-preflight}"
capacity="${2:-256}"
dev=/dev/ublkb23
id=23
service=nopen-issue10-ublk-raw.service
state=/run/nopen-v070
logdir=/var/log/nopen-v070
target="$repo/build-v070/nopen-v070-ublk"
controller="$repo/build-v070/ublksrv-upstream/ublk"
worker="$repo/build-v070/v060/nopen-isolated-worker"
RAW_ACK=I_ACCEPT_RAW_UBLK_GPU_IO_CAN_HANG
VRAM_ACK=I_ACCEPT_VOLATILE_GPU_ONLY_SWAP_CAN_LOSE_PAGES_AND_LOCKUP

# This is a physical ublk driver pilot, not a remedy for high host memory
# pressure. A previous baseline with ~3.75GiB zram used and 1.3GiB available
# is NO-GO. Read-only checks; fail closed on malformed/missing proc data.
raw_memory_gate() {
  local available_kib zram_used_kib available_mib zram_used_mib
  available_kib="$(awk '$1=="MemAvailable:" {print $2; exit}' /proc/meminfo)"
  zram_used_kib="$(awk 'NR>1 && $1=="/dev/zram0" {print $4; exit}' /proc/swaps)"
  if [[ ! "$available_kib" =~ ^[0-9]+$ || ! "$zram_used_kib" =~ ^[0-9]+$ ]];then
    echo "ISSUE10_RAW_REFUSED cannot read MemAvailable or active /dev/zram0" >&2
    return 2
  fi
  available_mib=$(( available_kib / 1024 ))
  zram_used_mib=$(( zram_used_kib / 1024 ))
  if (( available_mib < 3072 || zram_used_mib > 1024 ));then
    echo "ISSUE10_RAW_REFUSED host memory pressure: MemAvailable=${available_mib}MiB (min=3072) zram_used=${zram_used_mib}MiB (max=1024)" >&2
    return 2
  fi
  if python3 "$repo/v070/tools/find_bannerlord.py" --pid-only >/dev/null 2>&1;then
    echo "ISSUE10_RAW_REFUSED close Bannerlord before the physical raw ublk pilot" >&2
    return 2
  fi
  echo "ISSUE10_RAW_RAM_GUARD_PASS mem_available_mib=$available_mib zram_used_mib=$zram_used_mib"
}

case "$action" in
  preflight)
    if [[ "$capacity" != 256 ]];then
      echo "ISSUE10_RAW_REFUSED first diagnostic capacity is 256MiB ONLY" >&2;exit 2
    fi
    python3 "$repo/v080/tools/issue10_next_swap_readiness.py"
    raw_memory_gate
    python3 "$repo/v080/tools/v081_direct4g_preflight.py"
    echo "ISSUE10_RAW_PREFLIGHT_PASS no_gpu_alloc=YES no_kernel_swap=YES"
    exit 0;;
  run)
    [[ "$capacity" == 256 ]] || {
      echo "ISSUE10_RAW_REFUSED first diagnostic capacity is 256MiB ONLY" >&2;exit 2;
    };;
  *)
    echo "Usage: bash v080/tools/issue10_ublk_raw_only.sh preflight [256]" >&2
    echo "Experimental 256MiB raw test: sudo env NOPEN_ISSUE10_RAW_ACK=$RAW_ACK bash v080/tools/issue10_ublk_raw_only.sh run 256" >&2
    exit 2;;
esac

[[ $EUID == 0 ]] || { echo "ISSUE10_RAW_REFUSED root required"; exit 2; }
# Re-evaluate immediately before any privileged action, even when a prior
# preflight report passed hours earlier. Never bypass this live host check.
raw_memory_gate
[[ "${NOPEN_ISSUE10_RAW_ACK:-}" == "$RAW_ACK" ]] || {
  echo "ISSUE10_RAW_REFUSED explicit risk acknowledgment required";exit 9;
}
echo "WARNING: Real ublk kernel block I/O on the primary AMD GPU can still freeze the host."
echo "NO filesystem, NO mkswap, NO swapon, NO SSD shadow and NO zram changes."
echo "Close Bannerlord, save all work and verify backups before this experiment."
echo "Type EVET to create a volatile 256MiB raw block device; anything else cancels."
read -r approval </dev/tty || {
  echo "ISSUE10_RAW_REFUSED interactive terminal required";exit 9;
}
[[ "$approval" == EVET ]] || { echo "ISSUE10_RAW_REFUSED not confirmed";exit 9; }

# Physical host: use the original 4GiB budget guard conservatively even for
# a small 256MiB pilot, so weaker RAM/VRAM conditions cannot slip through.
python3 "$repo/v080/tools/v081_direct4g_preflight.py"
if python3 "$repo/v070/tools/find_bannerlord.py" --pid-only >/dev/null 2>&1; then
  echo "ISSUE10_RAW_REFUSED close Bannerlord for first raw block test" >&2;exit 2
fi
[[ ! -e "$dev" && ! -e /dev/ublkc23 ]] || {
  echo "ISSUE10_RAW_REFUSED device 23 already exists";exit 2;
}
[[ ! -e "$state/device" && ! -e "$state/shadow-file" ]] || {
  echo "ISSUE10_RAW_REFUSED prior Nopen state exists; inspect it first";exit 2;
}
for unit in "$service" nopen-v070-ublk.service nopen-v081-direct4g-monitor.service;do
  if systemctl is-active --quiet "$unit";then
    echo "ISSUE10_RAW_REFUSED an experimental Nopen service is already active: $unit" >&2
    exit 2
  fi
done
if awk 'NR>1 && ($1 ~ /^\/dev\/(ublkb|nbd)/) {bad=1} END {exit !bad}' /proc/swaps;then
  echo "ISSUE10_RAW_REFUSED experimental ublk/NBD swap already active";exit 2
fi
[[ -x "$target" && -x "$controller" && -x "$worker" ]] || {
  echo "ISSUE10_RAW_REFUSED compiled ublk/worker/controller required";exit 2;
}
for bin in systemd-run systemctl modprobe timeout dd cmp python3;do
  command -v "$bin" >/dev/null || { echo "ISSUE10_RAW_REFUSED missing $bin";exit 2; }
done

modprobe ublk_drv
[[ -c /dev/ublk-control ]] || { echo "ISSUE10_RAW_REFUSED no ublk-control";exit 2; }
umask 077
install -d -m 0750 "$logdir"
tmpdir="$(mktemp -d /run/nopen-issue10-raw.XXXXXXXX)"
logfile="$logdir/issue10-raw-$(date +%Y%m%d-%H%M%S)-$BASHPID.log"
install -m 0600 /dev/null "$logfile"
owned=0
cleanup() {
  local result=$1
  trap - EXIT
  if (( owned ));then
    if awk 'NR>1 && $1=="/dev/ublkb23" {active=1} END {exit !active}' /proc/swaps;then
      echo "ISSUE10_RAW_FATAL unexpected active Nopen SWAP: leaving service and device intact" >&2
      result=90
    else
      if [[ -b "$dev" ]];then
        if ! timeout --signal=TERM --kill-after=3s 12s "$controller" del -n "$id";then
          echo "ISSUE10_RAW_CLEANUP_INCOMPLETE ublk delete failed; service left running for inspection" >&2
          result=80
        fi
      fi
      if (( result != 80 )) &&
         { systemctl is-active --quiet "$service" ||
           systemctl is-failed --quiet "$service"; };then
        if ! timeout --signal=TERM --kill-after=3s 12s systemctl stop "$service";then
          echo "ISSUE10_RAW_CLEANUP_INCOMPLETE service stop failed" >&2
          result=81
        fi
      fi
    fi
  fi
  rm -rf -- "$tmpdir"
  if (( result == 0 ));then
    if [[ -e "$dev" || -e /dev/ublkc23 ]] ||
       systemctl is-active --quiet "$service";then
      echo "ISSUE10_RAW_CLEANUP_INCOMPLETE device or service still present" >&2
      result=82
    fi
  fi
  if (( result == 0 ));then
    echo "ISSUE10_RAW_DONE device_removed=YES service_stopped=YES kernel_swap=NO zram_untouched=YES log=$logfile"
  else
    echo "ISSUE10_RAW_FAILED rc=$result log=$logfile" >&2
  fi
  exit "$result"
}
trap 'cleanup $?' EXIT

echo "ISSUE10_RAW_BEGIN capacity_mib=256 device=$dev physical_ublk=YES kernel_swap=NO log=$logfile"
# If systemd-run partially creates a unit but exits with failure, EXIT cleanup
# still owns this (previously absent) device/unit and must inspect it.
owned=1
systemd-run --system --quiet --unit=nopen-issue10-ublk-raw \
  --service-type=exec --collect \
  --property=Restart=no \
  --property=KillMode=control-group \
  --property=OOMPolicy=continue \
  --property=OOMScoreAdjust=-950 \
  --property=LimitMEMLOCK=infinity \
  --property="StandardOutput=append:$logfile" \
  --property="StandardError=append:$logfile" \
  --setenv=NOPEN_DIAG_IO_TRACE=1 \
  --setenv=NOPEN_V070_KERNEL_SWAP_TEST=EXPERIMENTAL_GPU_SWAP_CAN_HANG \
  --setenv=NOPEN_UBLK_BACKEND=opencl \
  --setenv=NOPEN_UBLK_STORAGE_MODE=vram-direct \
  --setenv="NOPEN_VRAM_DIRECT_ACK=$VRAM_ACK" \
  --setenv=NOPEN_VRAM_DIRECT_PREFILL=1 \
  --setenv=NOPEN_V070_CAPACITY_MIB=256 \
  --setenv="NOPEN_UBLK_WORKER_BIN=$worker" \
  --setenv=MALLOC_ARENA_MAX=1 \
  "$target" add -t nopen-vram-swap-lab -n "$id" -q 1 -d 16 \
    --max_io_buf_bytes 65536
ready=0
for (( i=0;i<120;i++ ));do
  if [[ -b "$dev" ]];then ready=1;break;fi
  if ! systemctl is-active --quiet "$service";then break;fi
  sleep 0.1
done
(( ready == 1 )) || {
  echo "ISSUE10_RAW_REFUSED ublk device not ready" >&2;tail -n 50 "$logfile" >&2;exit 5;
}
for stamp in NOPEN_VRAM_DIRECT_PHYSICAL_LAB NOPEN_VRAM_DIRECT_PREFILL_PASS \
             NOPEN_DIRECT_METADATA_MLOCK_PASS NOPEN_DIRECT_WORKER_STAGING_MLOCK_PASS;do
  found=0
  for (( i=0;i<50;i++ ));do
    if grep -Fq "$stamp" "$logfile";then found=1;break;fi
    if ! systemctl is-active --quiet "$service";then break;fi
    sleep 0.1
  done
  (( found == 1 )) || { echo "ISSUE10_RAW_REFUSED missing $stamp" >&2;exit 5; }
done
# Unique 4KiB pattern per offset; use direct I/O to the *dedicated* device.
# Exercise start, middle, end of 256MiB address space without touching host disks.
for page in 0 32768 65535;do
  dd if=/dev/urandom of="$tmpdir/source" bs=4096 count=1 status=none
  timeout --signal=TERM --kill-after=3s 10s \
    dd if="$tmpdir/source" of="$dev" bs=4096 count=1 seek="$page" \
      oflag=direct conv=notrunc status=none
  timeout --signal=TERM --kill-after=3s 10s \
    dd if="$dev" of="$tmpdir/restore" bs=4096 count=1 skip="$page" \
      iflag=direct status=none
  cmp -- "$tmpdir/source" "$tmpdir/restore"
  echo "ISSUE10_RAW_PAGE_PASS page=$page bytes=4096"
done
if ! grep -Fq 'NOPEN_DIAG_UBLK phase=BEGIN' "$logfile" ||
   ! grep -Fq 'NOPEN_DIAG_UBLK phase=END' "$logfile" ||
   ! grep -Fq 'NOPEN_DIAG_IPC phase=BEGIN' "$logfile" ||
   ! grep -Fq 'NOPEN_DIAG_IPC phase=END' "$logfile" ||
   ! grep -Fq 'NOPEN_DIAG_WORKER phase=GPU_ENTER' "$logfile" ||
   ! grep -Fq 'NOPEN_DIAG_WORKER phase=GPU_RETURN' "$logfile";then
  echo "ISSUE10_RAW_REFUSED incomplete ublk/worker trace" >&2
  tail -n 100 "$logfile" >&2
  exit 5
fi
echo "ISSUE10_RAW_INTEGRITY_PASS pages=3 capacity_mib=256 kernel_swap=NO trace=YES"
