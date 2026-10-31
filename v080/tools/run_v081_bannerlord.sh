#!/usr/bin/env bash
# Nopen v0.8.1, Bannerlord + GPU-only VRAM page benchmark.
# Runs NO swapon/swapoff, no ublk device and no Nopen SSD shadow.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
mode="${1:-game}"
capacity="${2:-256}"
if [[ "$mode" != game && "$mode" != baseline ]] ||
   [[ "$capacity" != 256 && "$capacity" != 4096 ]]; then
  echo "Usage: bash v080/tools/run_v081_bannerlord.sh [game|baseline] [256|4096]" >&2
  exit 2
fi
if (( EUID==0 ));then
  echo "V081_REFUSED no sudo/root";exit 2
fi
if grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps;then
  echo "V081_REFUSED active kernel ublk/NBD swap";exit 2
fi
# Never rerun the entire 60-test CTest suite or compile during a game.
# The v040 memory-economics tests intentionally refuse under real game RAM
# pressure, even though the GPU coexistence bench has not yet started.
# First run the baseline with the game closed to build and test everything.
bench="$repo/build-v081/nopen-v080-vram-direct-bench"
worker="$repo/build-v081/v070/v060/nopen-isolated-worker"
python3 -m py_compile v080/tools/bannerlord_v081_probe.py
if [[ "$mode" == baseline ]]; then
  cmake -S v080 -B build-v081 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
  cmake --build build-v081 --parallel
  ctest --test-dir build-v081 --output-on-failure
else
  if [[ ! -x "$bench" || ! -x "$worker" ]]; then
    echo "V081_GAME_REFUSED game mode needs baseline-built executables; run baseline with game closed first" >&2
    exit 2
  fi
  echo "V081_GAME_REUSE_PREBUILT ctest_under_game=NO compile_under_game=NO kernel_swap=NO"
fi
exec python3 v080/tools/bannerlord_v081_probe.py \
  --mode "$mode" --capacity-mib "$capacity" --rounds 3 \
  --bench "$bench" \
  --worker "$worker"
