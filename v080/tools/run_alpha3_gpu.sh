#!/usr/bin/env bash
# Alpha.3: real AMD 2GiB GPU-only endurance experiment; NEVER kernel SWAP.
# Invoke from fish as: bash v080/tools/run_alpha3_gpu.sh
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
if [[ "$EUID" == 0 ]]; then
  echo "V080_ALPHA3_REFUSED do not run with sudo/root" >&2
  exit 2
fi
if grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps; then
  echo "V080_ALPHA3_REFUSED active ublk/NBD kernel SWAP" >&2
  exit 2
fi
# The game must NOT be running during this GPU-only allocation pilot.
if ps -eo rss,comm --no-headers | awk '$2=="Starter" && $1>524288{found=1}END{exit !found}'; then
  echo "V080_ALPHA3_REFUSED large Wine Starter detected; close Bannerlord" >&2
  exit 2
fi
if pgrep -afi 'Bannerlord\\.(Native|exe)' >/dev/null 2>&1; then
  echo "V080_ALPHA3_REFUSED Bannerlord game detected; close it" >&2
  exit 2
fi
echo "V080_ALPHA3_BEGIN capacity_mib=2048 kernel_swap=NO game=OFF"
python3 v080/tools/capacity_plan.py --stage-mib 2048
build="$repo/build-v080a3"
cmake -S v080 -B "$build" -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
out="$(mktemp -d "$HOME/nopen-v080-alpha3-XXXXXXXX")"
echo "V080_ALPHA3_REPORT_DIR=$out"
{
  printf 'git_revision=%s\n' "$(git rev-parse HEAD)"
  date --iso-8601=seconds
  uname -r
  swapon --show
  grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree):' /proc/meminfo
} > "$out/metadata.txt"
python3 v080/tools/capacity_plan.py --stage-mib 2048 > "$out/capacity.log"
timeout --signal=TERM --kill-after=5s 600s \
  "$build/nopen-v080-gpu-bench" --backend opencl \
  --pool-mib 2048 --mode direct --full-passes 2 --operations 256 \
  --ack I_ACCEPT_2GIB_GPU_ONLY_NO_SWAP \
  2>&1 | tee "$out/gpu-direct.log"
grep -Eq 'V080_GPU_FULL_ARENA_VERIFIED .*capacity_mib=2048 .*verified_pass=2 ' "$out/gpu-direct.log"
grep -Eq 'V080_GPU_BENCH_PASS .*capacity_mib=2048 .*full_passes_completed=2 ' "$out/gpu-direct.log"
echo "V080_ALPHA3_LOCAL_PASS real_gpu=YES capacity_mib=2048 full_passes=2 kernel_swap=NO"
echo "V080_ALPHA3_REPORT_FILES=$out/{gpu-direct.log,metadata.txt,capacity.log}"
echo "V080_ALPHA3_NOTE No kernel SWAP activated. Bannerlord game test is NOT part of alpha.3."
