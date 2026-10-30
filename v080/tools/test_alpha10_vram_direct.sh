#!/usr/bin/env bash
# ONLY Nopen VRAM SWAP experimental GPU page-store code is exercised.
# Does not activate /dev/ublkb23, does not call swapon/swapoff,
# does not create SSD shadow files or disable /dev/zram0.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
capacity="${1:-4096}"
case "$capacity" in 256|4096) ;; *)
  echo "NOPEN_DIRECT_REFUSED capacity must be 256 or 4096 MiB" >&2; exit 2;;
esac
if (( EUID==0 )); then
  echo "NOPEN_DIRECT_REFUSED never run benchmark as root" >&2;exit 2
fi
if grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps; then
  echo "NOPEN_DIRECT_REFUSED existing kernel GPU/NBD SWAP active" >&2;exit 2
fi
if pgrep -afi 'Bannerlord\.(Native|exe)' >/dev/null 2>&1; then
  echo "NOPEN_DIRECT_REFUSED close Bannerlord for uncontended GPU measurement" >&2;exit 2
fi
cmake -S v080 -B build-v080a10 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v080a10 --parallel
ctest --test-dir build-v080a10 --output-on-failure
bench="$repo/build-v080a10/nopen-v080-vram-direct-bench"
worker="$repo/build-v080a10/v070/v060/nopen-isolated-worker"
echo "NOPEN_VRAM_DIRECT_ONLY_BEGIN capacity_mib=$capacity shadow=NONE disk_io=NONE kernel_swap=NO"
for kib in 4 64; do
  "$bench" --backend opencl --capacity-mib "$capacity" --io-kib "$kib" \
    --operations 2000 --worker "$worker"
done
echo "NOPEN_VRAM_DIRECT_ONLY_DONE physical_ssd_untouched=YES zram_untouched=YES kernel_swap=NO"
