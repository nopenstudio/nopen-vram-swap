#!/usr/bin/env bash
# Diagnostic only: two separate processes with exactly the same workload args.
# Usage: bash v012/tests/compare_memory.sh ./build/nopen-vram-demand --backend opencl ...
# Does not enable swap or make a net RAM savings claim.
set -euo pipefail
LAB="${1:?pass path to nopen-vram-demand}"; shift
if (( EUID == 0 )); then echo 'REFUSED: do not run research experiments as root' >&2; exit 2; fi
if awk 'NR>1 && $1 ~ /\/nbd[0-9]+/ {found=1} END{exit !found}' /proc/swaps; then
  echo 'REFUSED: active NBD swap detected' >&2; exit 2
fi
if (( $# < 2 )); then echo 'Pass --stress --backend opencl and numeric workload options.' >&2; exit 2; fi
args=("$@")
for arg in "${args[@]}"; do
  if [[ "$arg" == '--baseline' || "$arg" == '--probe' ]]; then
    echo 'REFUSED: baseline/probe flag is controlled by this script' >&2; exit 2
  fi
done
D="$(mktemp -d)"
trap 'rm -rf "$D"' EXIT
set +e
"$LAB" --baseline "${args[@]}" >"$D/baseline" 2>&1
A=$?
"$LAB" "${args[@]}" >"$D/gpu" 2>&1
B=$?
set -e
cat "$D/baseline"
cat "$D/gpu"
if (( A != 0 || B != 0 )); then
  echo "COMPARISON_INVALID baseline_exit=$A paged_exit=$B; no savings claim" >&2; exit 5
fi
BASE="$(grep '^BASELINE_PASS ' "$D/baseline" | tail -1 || true)"
GPU="$(grep '^STRESS_PASS ' "$D/gpu" | tail -1 || true)"
if [[ -z "$BASE" || -z "$GPU" ]]; then echo 'COMPARISON_INVALID missing PASS markers' >&2; exit 5; fi
getfield() { grep -oE "(^| )${2}=[0-9]+" <<< "$1" | tail -1 | cut -d= -f2; }
R1="$(getfield "$BASE" process_maxrss_kib)"; R2="$(getfield "$GPU" process_maxrss_kib)"
if [[ ! "$R1" =~ ^[0-9]+$ || ! "$R2" =~ ^[0-9]+$ ]]; then
  echo 'COMPARISON_INVALID missing RSS' >&2; exit 5
fi
DELTA=$((R2 - R1))
echo "ECONOMICS_RESULT baseline_maxrss_kib=$R1 paged_maxrss_kib=$R2 paged_minus_baseline_kib=$DELTA scope=whole_process_not_system vram_excluded_from_rss=YES no_global_swap=YES"
echo 'NOTE: This compares two separate processes on one workload. GPU/driver shared memory, filesystem cache and kernel reservations need independent measurement. It does not demonstrate system-wide swap or automatic RAM savings.'