#!/usr/bin/env bash
# Compare 4KiB durable SSD-only vs real AMD GPU cache on the same filesystem.
# NO kernel SWAP, NO root, zram untouched. Outputs comparable p50/p95.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
if (( EUID==0 )); then
  echo "V080_ALPHA9_REFUSED use ordinary user, no sudo" >&2; exit 2
fi
if grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps; then
  echo "V080_ALPHA9_REFUSED ublk/NBD swap is active" >&2; exit 2
fi
if pgrep -afi 'Bannerlord\.(Native|exe)' >/dev/null 2>&1; then
  echo "V080_ALPHA9_REFUSED close Bannerlord before comparing" >&2; exit 2
fi
cmake -S v080 -B build-v080a9 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v080a9 --parallel
ctest --test-dir build-v080a9 --output-on-failure
out="$(mktemp -d "$HOME/nopen-v080-a9-compare-XXXXXXXX")"
echo "V080_ALPHA9_REPORT_DIR=$out"
bench="$repo/build-v080a9/nopen-v080-async-shadow-bench"
worker="$repo/build-v080a9/v070/v060/nopen-isolated-worker"
for mode in disk-only async; do
  echo "V080_ALPHA9_START mode=$mode capacity_mib=256"
  "$bench" --backend opencl --mode "$mode" --capacity-mib 256 \
      --operations 2000 --worker "$worker" \
      --shadow-file "$out/${mode}-shadow.bin" \
      2>&1 | tee "$out/${mode}.log"
done
echo "V080_ALPHA9_COMPARE_PASS kernel_swap=NO report=$out"
echo "Interpret: SSD baseline is expected to dominate every durable ACK."
