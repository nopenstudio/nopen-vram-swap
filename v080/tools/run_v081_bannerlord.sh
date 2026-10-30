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
# Perform lightweight preflight before compiling or touching GPU memory.
python3 -m py_compile v080/tools/bannerlord_v081_probe.py
cmake -S v080 -B build-v081 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v081 --parallel
ctest --test-dir build-v081 --output-on-failure
exec python3 v080/tools/bannerlord_v081_probe.py \
  --mode "$mode" --capacity-mib "$capacity" --rounds 3 \
  --bench "$repo/build-v081/nopen-v080-vram-direct-bench" \
  --worker "$repo/build-v081/v070/v060/nopen-isolated-worker"
