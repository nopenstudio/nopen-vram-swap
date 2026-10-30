#!/usr/bin/env bash
# v0.8 alpha.4: 4GiB GPU-only integrity pilot, Bannerlord MUST be closed.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
if [[ "$EUID" == 0 ]]; then
  echo "V080_ALPHA4_REFUSED do not run as root" >&2; exit 2
fi
if grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps; then
  echo "V080_ALPHA4_REFUSED GPU/NBD kernel SWAP is active" >&2; exit 2
fi
if ps -eo rss,comm --no-headers | awk '$2=="Starter" && $1>524288{found=1}END{exit !found}'; then
  echo "V080_ALPHA4_REFUSED close Bannerlord before the 4GiB GPU-only test" >&2; exit 2
fi
if pgrep -afi 'Bannerlord\.(Native|exe)' >/dev/null 2>&1; then
  echo "V080_ALPHA4_REFUSED Bannerlord is running" >&2; exit 2
fi
echo "V080_ALPHA4_BEGIN capacity_mib=4096 kernel_swap=NO game=OFF"
python3 v080/tools/capacity_plan.py --stage-mib 4096
build="$repo/build-v080a4"
cmake -S v080 -B "$build" -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
out="$(mktemp -d "$HOME/nopen-v080-alpha4-XXXXXXXX")"
echo "V080_ALPHA4_REPORT_DIR=$out"
{
  printf 'git_revision=%s\n' "$(git rev-parse HEAD)"
  date --iso-8601=seconds
  uname -r
  swapon --show
  grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree):' /proc/meminfo
} > "$out/metadata.txt"
python3 v080/tools/capacity_plan.py --stage-mib 4096 > "$out/capacity.log"
timeout --signal=TERM --kill-after=5s 600s \
  "$build/nopen-v080-gpu-bench" --backend opencl \
  --pool-mib 4096 --mode direct --full-passes 2 --operations 256 \
  --ack I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP \
  2>&1 | tee "$out/gpu-direct.log"
grep -Eq 'V080_GPU_FULL_ARENA_VERIFIED .*capacity_mib=4096 .*verified_pass=2 ' "$out/gpu-direct.log"
grep -Eq 'V080_GPU_BENCH_PASS .*capacity_mib=4096 .*full_passes_completed=2 ' "$out/gpu-direct.log"
echo "V080_ALPHA4_LOCAL_PASS real_gpu=YES capacity_mib=4096 full_passes=2 kernel_swap=NO"
echo "V080_ALPHA4_REPORT_FILES=$out/gpu-direct.log $out/metadata.txt $out/capacity.log"
echo "V080_ALPHA4_NOTE Standalone test ONLY. Game comparison is a separate stage."
