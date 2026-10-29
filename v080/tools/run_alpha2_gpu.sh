#!/usr/bin/env bash
# Hardware-only, non-SWAP alpha.2 1GiB AMD OpenCL direct/pinned A/B.
# Compatible when invoked from fish with: bash v080/tools/run_alpha2_gpu.sh
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
build="$repo/build-v080a2"
if [[ "$EUID" == 0 ]]; then
  echo "V080_ALPHA2_REFUSED do not run GPU test with sudo/root" >&2
  exit 2
fi
if swapon --noheadings --raw --show=NAME 2>/dev/null |
   grep -Eq '^/dev/(ublkb|nbd)'; then
  echo "V080_ALPHA2_REFUSED active ublk/NBD kernel swap. Stop safely first." >&2
  exit 2
fi
# A Co-op/Proton game currently competing for VRAM makes measurements noisy.
# This is an intentionally narrow condition based on this user's actual
# Wine 'Starter' process; do not kill or modify the game.
if ps -eo rss,comm --no-headers | awk '$2=="Starter" && $1>524288{found=1}END{exit !found}'; then
  echo "V080_ALPHA2_REFUSED Bannerlord-like Starter using >512MiB RSS." >&2
  echo "Save and close Bannerlord before measuring GPU-only performance." >&2
  exit 2
fi
echo "V080_ALPHA2_BEGIN kernel_swap=NO pool_mib=1024"
echo "V080_ALPHA2_NEVER run a 1GiB physical start-pilot; v0.7 cap=64MiB"
echo "V080_ALPHA2_SYSFS_LINK_NOTE PCIe current link may downclock while idle"
for dev in /sys/bus/pci/devices/*; do
  [[ -r "$dev/vendor" && -r "$dev/class" ]] || continue
  [[ "$(cat "$dev/vendor")" == "0x1002" ]] || continue
  [[ "$(cat "$dev/class")" == 0x03* ]] || continue
  if [[ -r "$dev/current_link_speed" && -r "$dev/current_link_width" ]]; then
    echo "V080_ALPHA2_PCIE_LINK pci=${dev##*/} current_speed=$(cat "$dev/current_link_speed") current_width=$(cat "$dev/current_link_width")"
    if [[ -r "$dev/max_link_speed" && -r "$dev/max_link_width" ]]; then
      echo "V080_ALPHA2_PCIE_MAX pci=${dev##*/} max_speed=$(cat "$dev/max_link_speed") max_width=$(cat "$dev/max_link_width")"
    fi
  fi
done

cmake -S v080 -B "$build" -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure

# Unique directory: never overwrite measurements from earlier attempts.
out="$(mktemp -d "$HOME/nopen-v080-alpha2-XXXXXXXX")"
echo "V080_ALPHA2_REPORT_DIR=$out"
printf '%s\n' "Git revision: $(git rev-parse HEAD)" > "$out/metadata.txt"
{
  date --iso-8601=seconds
  uname -r
  swapon --show
  grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree):' /proc/meminfo
} >> "$out/metadata.txt"
echo "V080_ALPHA2_TEST_DIRECT_START no_swap=YES"
timeout --signal=TERM --kill-after=5s 360s \
  "$build/nopen-v080-gpu-bench" --backend opencl --pool-mib 1024 \
  --mode direct --operations 128 \
  --ack I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP \
  2>&1 | tee "$out/direct.log"

# A pinned path failure must not be hidden behind a silent direct fallback.
echo "V080_ALPHA2_TEST_PINNED_START experimental=YES"
timeout --signal=TERM --kill-after=5s 360s \
  "$build/nopen-v080-gpu-bench" --backend opencl --pool-mib 1024 \
  --mode pinned --operations 128 \
  --ack I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP \
  2>&1 | tee "$out/pinned.log"

python3 v080/tools/compare_gpu_paths.py \
  --direct "$out/direct.log" --pinned "$out/pinned.log" \
  --json "$out/comparison.json" | tee "$out/summary.txt"

echo "V080_ALPHA2_LOCAL_PASS report_dir=$out"
echo "Submit direct.log, pinned.log, comparison.json, and metadata.txt."
echo "No kernel swap activated; no Bannerlord benchmark run by this script."
