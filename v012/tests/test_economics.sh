#!/usr/bin/env bash
# Same logical workload: plain anonymous CPU pages vs bounded UFFD backing.
# No swap activation, no NBD. The baseline does not need userfaultfd.
set -euo pipefail
LAB="${1:?executable expected}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
BEFORE="$(awk 'NR>1 {print $1,$2,$3,$5}' /proc/swaps)"
COMMON=(--stress --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 \
        --operations 600 --seed 20261027 --hot-pages 16 --hot-percent 80 --write-percent 40)
"$LAB" --baseline "${COMMON[@]}" >"$TMP/baseline.log" 2>&1
cat "$TMP/baseline.log"
grep -q 'BASELINE_PASS version=0.12.0-alpha.5' "$TMP/baseline.log"
grep -q 'MEMORY_SNAPSHOT phase=baseline_filled' "$TMP/baseline.log"
! grep -q 'UFFD_' "$TMP/baseline.log"
if "$LAB" --probe >"$TMP/probe.log" 2>&1; then
    "$LAB" "${COMMON[@]}" >"$TMP/gpu.log" 2>&1
    cat "$TMP/gpu.log"
    grep -q 'STRESS_PASS version=0.12.0-alpha.5 backend=mock' "$TMP/gpu.log"
    grep -q 'MEMORY_SNAPSHOT phase=gpu_store_ready' "$TMP/gpu.log"
    grep -q 'MEMORY_SNAPSHOT phase=uffd_ready' "$TMP/gpu.log"
    grep -q 'MEMORY_SNAPSHOT phase=gpu_after_workload' "$TMP/gpu.log"
    grep -q 'verified_final_pages=128' "$TMP/gpu.log"
else
    echo 'ECONOMICS_UFFD_SKIPPED runner policy blocks UFFD; separate CI enforces it'
fi
AFTER="$(awk 'NR>1 {print $1,$2,$3,$5}' /proc/swaps)"
[[ "$BEFORE" == "$AFTER" ]] || { echo 'FAIL: /proc/swaps modified' >&2; exit 1; }
echo 'ECONOMICS_TEST_PASS baseline_and_mock_separate_processes=YES no_global_swap=YES'