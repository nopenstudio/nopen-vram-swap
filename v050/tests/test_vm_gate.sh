#!/usr/bin/env bash
set -euo pipefail
GATE="${1:?gate required}"
PAGER="${2:?pager required}"
CHILD="${3:?slow test child required}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$GATE" --pager "$PAGER" --backend mock --pool-mib 4 --logical-pages 128 \
  --ram-pages 8 --operations 1000 --timeout-seconds 15 > "$tmp/ok" 2>&1
grep -q "^STRESS_PASS" "$tmp/ok"
grep -q "^VM_GATE_PASS" "$tmp/ok"
set +e
NOPEN_TEST_FAIL_AFTER=2 "$GATE" --pager "$CHILD" --backend mock \
 --pool-mib 4 --logical-pages 128 --ram-pages 8 --operations 1000 \
 --timeout-seconds 8 > "$tmp/abort" 2>&1
code="$?"
set -e
[[ "$code" == 8 ]] || { cat "$tmp/abort"; echo "Expected pressure abort status=8, got=$code" >&2; exit 1; }
grep -q "VM_GATE_ABORT pressure_or_vram" "$tmp/abort"
grep -q "VM_GATE_STOP isolated_child_terminated=YES" "$tmp/abort"
echo "VM_GATE_TEST_PASS real_uffd_mock=YES synthetic_psi_abort=YES no_global_swap=YES"
