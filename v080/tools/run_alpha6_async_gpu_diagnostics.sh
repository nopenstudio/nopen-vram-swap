#!/usr/bin/env bash
# v0.8 alpha6: real Radeon+SSD latency comparison without kernel swap.
# CPU RAM is NOT enlarged. Keeps existing zram, no root/ublk/swapon.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
if [[ "${EUID}" == 0 ]]; then
  echo "V080_ASYNC_DIAG_REFUSED root not allowed" >&2; exit 2
fi
if grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps; then
  echo "V080_ASYNC_DIAG_REFUSED existing ublk/NBD SWAP active" >&2; exit 2
fi
if pgrep -afi 'Bannerlord\.(Native|exe)' >/dev/null 2>&1; then
  echo "V080_ASYNC_DIAG_REFUSED close Bannerlord first" >&2; exit 2
fi
python3 v080/tools/capacity_plan.py --stage-mib 4096
out="$(mktemp -d "$HOME/nopen-v080-alpha6-diagnostics-XXXXXXXX")"
echo "V080_ALPHA6_REPORT_DIR=$out"
git rev-parse HEAD > "$out/git-revision.txt"
cmake -S v080 -B build-v080a6 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v080a6 --parallel
ctest --test-dir build-v080a6 --output-on-failure
bench="$repo/build-v080a6/nopen-v080-async-shadow-bench"
worker="$repo/build-v080a6/v070/v060/nopen-isolated-worker"
for mode in sync async; do
  echo "V080_ALPHA6_256MIB_START mode=$mode"
  "$bench" --backend opencl --mode "$mode" --capacity-mib 256 \
    --operations 2000 --worker "$worker" \
    --shadow-file "$out/256-${mode}-shadow.bin" \
    2>&1 | tee "$out/256-${mode}.log"
done
echo "V080_ALPHA6_4GIB_START mode=async kernel_swap=NO"
"$bench" --backend opencl --mode async --capacity-mib 4096 \
  --operations 2000 --worker "$worker" \
  --shadow-file "$out/4096-async-shadow.bin" \
  2>&1 | tee "$out/4096-async.log"
echo "V080_ALPHA6_REAL_GPU_DIAG_PASS kernel_swap=NO report=$out"
echo "V080_ALPHA6_NOTE 4GiB GPU cache + persistent SSD verified; not kernel SWAP safety"
