#!/usr/bin/env bash
# Real UFFD missing-page faults, with CPU-only simulated store.
# No system swap or NBD is modified by this test.
set -euo pipefail
LAB="${1:?expected nopen-vram-demand path}"
if ! "$LAB" --probe; then
  echo 'SKIP: userfaultfd blocked on this runner; CI must require successful --probe.'
  exit 0
fi
BEFORE="$(awk 'NR>1 {print $1,$2,$3,$5}' /proc/swaps)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for config in \
  '4000 17 8 85 35' \
  '4000 2026 128 0 50' \
  '2000 294967 32 95 90'; do
  read -r OPS SEED HOT HOTP WRITEP <<< "$config"
  timeout --signal=TERM --kill-after=2s 20s "$LAB" \
    --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 \
    --stress --operations "$OPS" --seed "$SEED" \
    --hot-pages "$HOT" --hot-percent "$HOTP" --write-percent "$WRITEP" \
    > "$TMP/run.log" 2>&1 || { cat "$TMP/run.log"; exit 1; }
  grep -q 'STRESS_PASS version=0.12.0-alpha.4 backend=mock' "$TMP/run.log"
  grep -q "seed=$SEED operations=$OPS" "$TMP/run.log"
  grep -q 'verified_final_pages=128' "$TMP/run.log"
  grep -q 'peak_ram_pages=8' "$TMP/run.log"
  grep -q 'no_global_swap=YES' "$TMP/run.log"
  grep -Eq 'pageins=[1-9][0-9]*' "$TMP/run.log"
  echo "STRESS_ISOLATION_PASS seed=$SEED operations=$OPS"
done
for invalid in \
  '--operations 0' '--hot-pages 129' '--hot-percent 101' \
  '--write-percent 101' '--seed 0' '--operations 2000001' '--pool-mib 512'; do
  read -ra ARGS <<< "$invalid"
  set +e
  "$LAB" --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 \
    --stress "${ARGS[@]}" >"$TMP/invalid.log" 2>&1
  RC=$?
  set -e
  if [[ "$RC" != 2 ]]; then
    echo "FAIL: unsafe argument was not rejected: $invalid (exit=$RC)" >&2
    cat "$TMP/invalid.log" >&2
    exit 1
  fi
done
AFTER="$(awk 'NR>1 {print $1,$2,$3,$5}' /proc/swaps)"
if [[ "$BEFORE" != "$AFTER" ]]; then
  echo 'FAIL: system swap config changed' >&2
  exit 1
fi
echo 'STRESS_SUITE_PASS multiple seeds/hot-cold/write ratios + reject invalid inputs; no global swap changed'