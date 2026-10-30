#!/usr/bin/env bash
# Alpha.4 optional VRAM-only game coexistence. NEVER enable kernel SWAP.
# Run ONLY after the standalone 4GiB alpha.4 pilot passed.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
if [[ "$EUID" == 0 ]] || grep -Eq '^/dev/(ublkb|nbd)' /proc/swaps; then
  echo "V080_ALPHA4_GAME_REFUSED root or physical GPU/NBD swap" >&2; exit 2
fi
if ps -eo rss,comm --no-headers | awk '$2=="Starter" && $1>524288{found=1}END{exit !found}'; then
  echo "V080_ALPHA4_GAME_REFUSED first close Bannerlord" >&2; exit 2
fi
if pgrep -afi 'Bannerlord\.(Native|exe)' >/dev/null 2>&1; then
  echo "V080_ALPHA4_GAME_REFUSED first close Bannerlord" >&2; exit 2
fi
revision="$(git rev-parse HEAD)"
proven=NO
for log in "$HOME"/nopen-v080-alpha4-*/gpu-direct.log; do
  [[ -f "$log" ]] || continue
  meta="${log%/gpu-direct.log}/metadata.txt"
  [[ -f "$meta" ]] || continue
  if grep -Fq "git_revision=$revision" "$meta" &&
     grep -Eq '^V080_GPU_BENCH_PASS .*capacity_mib=4096 .*kernel_swap=NO .*full_passes_completed=2 ' "$log"; then
    proven=YES
    break
  fi
done
if [[ "$proven" != YES ]]; then
  echo "V080_ALPHA4_GAME_REFUSED no verified 4GiB standalone pilot at this exact git revision" >&2
  exit 2
fi
python3 v080/tools/capacity_plan.py --stage-mib 4096
out="$(mktemp -d "$HOME/nopen-v080-alpha4-game-XXXXXXXX")"
echo "V080_ALPHA4_GAME_REPORT_DIR=$out"
echo "V080_ALPHA4_GAME_INSTRUCTION After V080_GAME_HOLD_READY, launch Bannerlord in the same scene and run read-only telemetry in another terminal."
echo "V080_ALPHA4_GAME_INSTRUCTION Close the game before the 300s hold finishes if practical."
{
 printf 'git_revision=%s\n' "$revision"
 date --iso-8601=seconds
 uname -r
 swapon --show
} > "$out/metadata.txt"
timeout --signal=TERM --kill-after=5s 420s \
  "$repo/build-v080a4/nopen-v080-gpu-bench" --backend opencl \
  --pool-mib 4096 --mode direct --full-passes 2 --operations 256 \
  --hold-seconds 300 --ack I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP \
  2>&1 | tee "$out/hold.log"
grep -Eq '^V080_GAME_HOLD_PASS backend=opencl capacity_mib=4096 ' "$out/hold.log"
echo "V080_ALPHA4_GAME_HOLD_PASS kernel_swap=NO report=$out/hold.log"
echo "V080_ALPHA4_GAME_NOTE This is GPU VRAM occupancy, NOT CPU RAM swap acceleration."
