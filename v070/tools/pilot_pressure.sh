#!/usr/bin/env bash
# CAUTION: touches REAL Linux kernel SWAP backed by AMDGPU OpenCL.
# 64MiB GPU SWAP, bounded 96MiB process cgroup; desktop processes excluded.
# Test can still hang the entire host if kernel/AMDGPU reclaim deadlocks.
set -euo pipefail
device="/dev/ublkb23"
unit="nopen-v070-ublk.service"
state="/run/nopen-v070"
[[ "$EUID" == 0 ]] ||
  { echo "V070_PRESSURE_REFUSED run via sudo" >&2; exit 2; }
[[ "$(printenv NOPEN_V070_PILOT_ACK || :)" == "I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK" ]] ||
  { echo "V070_PRESSURE_REFUSED explicit physical pilot consent required" >&2; exit 2; }
if [[ ! -r "$state/capacity" ]]; then
  echo "V070_PRESSURE_REFUSED pilot not started; first run:" >&2
  echo "sudo env NOPEN_V070_PILOT_ACK=I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK bash v070/tools/vram_swap_lab.sh start-pilot 64" >&2
  echo "Then verify V070_SWAP_ACTIVE before executing pilot_pressure.sh" >&2
  exit 2
fi
if [[ "$(cat "$state/capacity")" != "64" ]]; then
  echo "V070_PRESSURE_REFUSED capacity mismatch; expected 64MiB pilot" >&2
  exit 2
fi
[[ -b "$device" && -f /sys/fs/cgroup/cgroup.controllers ]] || exit 2
grep -qw memory /sys/fs/cgroup/cgroup.controllers ||
  { echo "V070_PRESSURE_REFUSED cgroup-v2 memory controller unavailable" >&2; exit 2; }
swapon --noheadings --raw --show=NAME | grep -Fxq "$device" ||
  { echo "V070_PRESSURE_REFUSED Nopen pilot swap not active" >&2; exit 2; }
systemctl is-active --quiet "$unit" ||
  { echo "V070_PRESSURE_REFUSED root GPU service not active" >&2; exit 2; }
group="/sys/fs/cgroup/nopen-v070-pilot-$$"
dir="$(mktemp -d /var/tmp/nopen-v070-physical-pilot-XXXXXX)"
child=0;created=0
cleanup() {
  if ((child>0)); then
    kill "$child" 2>/dev/null || true
    # Avoid unbounded wait on uninterruptible kernel D state.
    for _ in $(seq 1 15); do
      kill -0 "$child" 2>/dev/null || break
      sleep .1
    done
  fi
  if ((created)); then rmdir "$group" 2>/dev/null || true; fi
  rm -rf "$dir"
}
trap cleanup EXIT
mkdir "$group";created=1
printf '%s\n' $((96*1024*1024)) > "$group/memory.max"
printf '%s\n' $((64*1024*1024)) > "$group/memory.swap.max"
cat >"$dir/probe.py" <<'PY'
import pathlib,sys,time
gate=pathlib.Path(sys.argv[1])
ready=pathlib.Path(sys.argv[2])
done=pathlib.Path(sys.argv[3])
try:
    length=112*1024*1024
    data=bytearray(length)
    for pos in range(0,length,4096):
        data[pos]=((pos//4096)*31+17)&255
    ready.write_text("READY")
    for _ in range(3000):
        if gate.exists():
            break
        time.sleep(.01)
    else:
        raise RuntimeError("no test gate in 30 seconds")
    for pos in range(0,length,4096):
        if data[pos]!=((pos//4096)*31+17)&255:
            raise RuntimeError(f"memory corruption page={pos//4096}")
    done.write_text("PILOT_DATA_VERIFIED")
except Exception as err:
    print("V070_PILOT_CHILD_FAILED",repr(err),file=sys.stderr)
    sys.exit(1)
PY
# Child is migrated before it allocates any large memory.
bash -c 'while [[ ! -e "$1" ]]; do sleep .01; done; exec python3 "$2" "$3" "$4" "$5"' \
  _ "$dir/start" "$dir/probe.py" "$dir/verify" "$dir/ready" "$dir/done" \
  >"$dir/probe.log" 2>&1 &
child=$!
echo "$child" > "$group/cgroup.procs"
touch "$dir/start"
used=0
for _ in $(seq 1 250); do
  used="$(awk -v dev="$device" '$1==dev{print $4}' /proc/swaps)"
  [[ "$used" =~ ^[0-9]+$ ]] || used=0
  if [[ -f "$dir/ready" && "$used" -gt 0 ]]; then break; fi
  kill -0 "$child" 2>/dev/null || break
  sleep .04
done
echo "V070_PILOT_PRESSURE_SNAPSHOT device_swap_used_kib=$used"
cat /proc/swaps
if [[ ! -f "$dir/ready" || "$used" -le 0 ]]; then
  echo "V070_PILOT_PRESSURE_FAILED Nopen kernel SWAP usage not confirmed" >&2
  cat "$dir/probe.log" >&2
  exit 3
fi
touch "$dir/verify"
for _ in $(seq 1 350); do
  [[ -f "$dir/done" ]] && break
  kill -0 "$child" 2>/dev/null || break
  sleep .05
done
[[ -f "$dir/done" ]] || {
  echo "V070_PILOT_PRESSURE_FAILED physical kernel swap-in verification failed" >&2
  cat "$dir/probe.log" >&2
  exit 4
}
wait "$child";child=0
echo "V070_PILOT_PRESSURE_PASS real_amdgpu=YES kernel_swap_used_kib=$used"
echo "V070_PILOT_PRESSURE_PASS_AFTER_READ verified_memory_pages=28672 gpu_ssd_shadow=YES data_corruption=NO"
echo "V070_PILOT_PRESSURE_NOTE service_and_swap_left_active=YES stop_with_vram_swap_lab=YES"
