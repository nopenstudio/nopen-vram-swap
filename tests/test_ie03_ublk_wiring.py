#!/usr/bin/env python3
"""IE-03: source-level contract for isolated VM raw ublk transactional adapter.

No ublk, GPU, kernel swap, root or device operation is launched in this test.
Runtime disk/journal semantics are tested by ie03_durable_store_fs_torn_ack_replay.
"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
adapter = (ROOT / "v070/experimental/ublk/nopen_ublk_swap_target.cpp").read_text()
builder = (ROOT / "v070/tools/build_ublk.sh").read_text()
header = (ROOT / "v070/include/nopen_durable_store.h").read_text()
source = (ROOT / "v070/src/nopen_durable_store.c").read_text()

assert '#include "nopen_durable_store.h"' in adapter
assert "transactional-ssd" in adapter
assert "if(!vm_lab || backend!=NOPEN_STORE_MOCK" in adapter
assert "nopen_durable_open(&state->durable" in adapter
assert "nopen_durable_read(state->durable" in adapter
assert "nopen_durable_write(state->durable" in adapter
assert "state->next_txid.fetch_add(1" in adapter
assert "nopen_durable_close(state->durable)" in adapter
assert "NOPEN_TRANSACTIONAL_RAW_REFUSED" in adapter
assert "if(physical_lab)" in adapter and "NOPEN_LIVE_KERNEL_SWAP_BLOCKED" in adapter
assert "libnopen_durable_store.a" in builder
assert "NOPEN_DURABLE_FAIL_AFTER_DATA_SYNC" in header
assert "NOPEN_DURABLE_FAIL_AFTER_MANIFEST_SYNC" in header
assert "fdatasync" in source
assert "flock(" in source
assert "O_CREAT|O_EXCL" in source
assert "sync_parent(" in source
assert "int nopen_durable_write(" in source
assert "int nopen_durable_read(" in source
assert "replay(s)" in source

print("IE03_UBLK_RAW_WIRING_PASS"
      " real_cow_store=YES vm_mock_only=YES"
      " physical_ublk=BLOCKED kernel_swap=BLOCKED"
      " driver_reclaim_safe=UNPROVEN")
