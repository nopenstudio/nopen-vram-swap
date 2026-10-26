#!/usr/bin/env bash
# Verifies that a missing-page storage failure exits THIS test process.
# Never sets system swap, sysctls, NBD or privileged kernel state.
set -euo pipefail
APP="${1:?binary path}"
if ! "$APP" --probe >/dev/null 2>&1; then
  echo 'SKIP UFFD fault isolation: kernel/seccomp does not permit userfaultfd.'
  exit 0
fi
set +e
out="$(timeout --signal=TERM --kill-after=2s 10s "$APP" --demo --backend mock --mib 2 --pages 16 --rounds 1 --inject-on-pagein 2>&1)"
status=$?
set -e
printf '%s\n' "$out"
if [[ "$status" -ne 143 ]]; then
  echo "FAIL: expected local SIGTERM (143), saw $status" >&2
  exit 1
fi
if ! grep -q 'FATAL: isolated application pager' <<<"$out"; then
  echo 'FAIL: missing pager failure evidence' >&2
  exit 1
fi
echo 'PASS: failed page-in terminated only the UFFD test process; no global swap involved.'