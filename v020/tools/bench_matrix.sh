#!/usr/bin/env bash
# Read-only performance characterization: bounded process-local GPU allocations.
# Does not change kernel parameters, NBD, global swap, or drivers.
set -euo pipefail
if (( EUID == 0 )); then echo 'REFUSED: do not benchmark as root' >&2; exit 2; fi
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LAB="${1:-$ROOT/build-v020/nopen-link-bench}"
DEST="${2:-$HOME/nopen-v020-results}"
if [[ ! -x "$LAB" ]]; then echo "Executable not found: $LAB" >&2;exit 2;fi
mkdir -p "$DEST"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
LOG="$DEST/matrix-$STAMP.log"
{
  echo "MATRIX_START $(date -u +%FT%TZ)"
  echo "git_revision=$(git -C "$ROOT" rev-parse HEAD)"
  bash "$ROOT/v020/tools/pcie_topology.sh"
} | tee "$LOG"
for KIB in 4 16 64 256 1024 4096; do
  echo "MATRIX_RUN mode=queued chunk_kib=$KIB" | tee -a "$LOG"
  timeout --signal=TERM --kill-after=5s 120s "$LAB" --backend opencl \
    --pool-mib 64 --chunk-kib "$KIB" --passes 3 | tee -a "$LOG"
done
for KIB in 4 256 4096; do
  echo "MATRIX_RUN mode=blocking chunk_kib=$KIB" | tee -a "$LOG"
  timeout --signal=TERM --kill-after=5s 120s "$LAB" --backend opencl \
    --pool-mib 64 --chunk-kib "$KIB" --passes 3 --blocking | tee -a "$LOG"
done
echo "MATRIX_PASS all_opencl_runs_complete=YES result=$LOG" | tee -a "$LOG"
