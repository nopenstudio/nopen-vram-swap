#!/usr/bin/env bash
set -euo pipefail
LAB="${1:?binary required}"
if ! "$LAB" --probe; then
  echo 'SKIP: kernel/seccomp blocks unprivileged userfaultfd; requires host-side integration test.'
  exit 0
fi
BEFORE="$(awk 'NR>1 {print $1,$2,$3,$5}' /proc/swaps)"
"$LAB" --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 --rounds 2
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
for pair in 'read:73' 'write:73' 'corruption:74'; do
  injection="${pair%%:*}"
  expected="${pair##*:}"
  set +e
  "$LAB" --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 \
    --rounds 2 --inject-"$injection" >"$TMP/$injection.out" 2>&1
  got="$?"
  set -e
  if [[ "$got" != "$expected" ]]; then
    echo "FAIL: $injection expected exit=$expected got=$got" >&2
    cat "$TMP/$injection.out" >&2
    exit 1
  fi
  grep -q 'NOPEN_ISOLATED_FATAL' "$TMP/$injection.out"
  if grep -q 'DEMAND_PASS' "$TMP/$injection.out"; then
    echo "FAIL: $injection falsely reported successful demand paging" >&2
    exit 1
  fi
  echo "ISOLATION_PASS mode=$injection exit=$got no_global_swap=YES"
done
AFTER="$(awk 'NR>1 {print $1,$2,$3,$5}' /proc/swaps)"
if [[ "$BEFORE" != "$AFTER" ]]; then
  echo 'FAIL: system swap configuration was modified' >&2
  exit 1
fi
echo 'DEMAND_ISOLATION_PASS all injected faults isolated, global swap unchanged'