#!/usr/bin/env python3
"""IE-03: deterministic design CONTRACT tests (no actual disk/GPU/swap)."""
from __future__ import annotations

from dataclasses import replace
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "ie03_page_contract", ROOT / "tools/ie03_ack_model.py"
)
assert spec is not None and spec.loader is not None
import sys
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)

PageContractModel = module.PageContractModel
ContractError = module.ContractError
CorruptAuthority = module.CorruptAuthority
PAGE = module.PAGE_BYTES


def rejects(fn, error=ContractError):
    try:
        fn()
    except error:
        return
    raise AssertionError("unsafe operation unexpectedly accepted")


def payload(ch: bytes, pages: int = 1) -> bytes:
    return ch * PAGE * pages


p = PageContractModel(64, "test-boot-1")
assert p.read(0) == bytes(PAGE)
assert p.begin_write("first", 0, payload(b"A")) == "PREPARED"
rejects(lambda: p.ack_write("first"))
rejects(lambda: p.commit_manifest("first"))
assert p.read(0) == bytes(PAGE)
p.persist_cow_data("first")
rejects(lambda: p.ack_write("first"))
assert p.read(0) == bytes(PAGE)
assert p.simulate_same_boot_restart().read(0) == bytes(PAGE)
record1 = p.commit_manifest("first")
assert record1.generations == (1,)
assert p.ack_write("first") is True
assert p.read(0) == payload(b"A")
assert p.begin_write("first", 0, payload(b"A")) == "ALREADY_COMMITTED"
rejects(lambda: p.begin_write("first", 0, payload(b"Z")))
assert p.simulate_same_boot_restart().ack_write("first") is True
assert p.simulate_same_boot_restart().read(0) == payload(b"A")

# GPU read cache must never become authority, even if it acknowledges late.
epoch1 = p.gpu_epoch
assert p.put_gpu_cache(0, 1, epoch1, payload(b"A"))
assert p.read(0) == payload(b"A")
assert not p.put_gpu_cache(0, 0, epoch1, payload(b"A"))
assert not p.put_gpu_cache(0, 1, epoch1, payload(b"Z"))
p.gpu_reset()
assert p.read(0) == payload(b"A")
assert not p.put_gpu_cache(0, 1, epoch1, payload(b"A"))

# Partial COW data, incomplete metadata commit -> previous ACK generation.
p.begin_write("second", 0, payload(b"B"))
p.persist_cow_data("second")
assert p.read(0) == payload(b"A")
after_crash = p.simulate_same_boot_restart()
assert after_crash.read(0) == payload(b"A")
rejects(lambda: after_crash.ack_write("second"))
assert after_crash.begin_write("second", 0, payload(b"B")) == "PREPARED"
after_crash.persist_cow_data("second")
record2 = after_crash.commit_manifest("second")
assert record2.generations == (2,)
assert after_crash.ack_write("second")
assert after_crash.read(0) == payload(b"B")
assert not after_crash.put_gpu_cache(0, 1, after_crash.gpu_epoch, payload(b"A"))

# Durable COMMIT succeeded, ACK response lost -> replay must accept identical
# request without overwriting old or new data or advancing generation.
retry = after_crash.simulate_same_boot_restart()
assert retry.begin_write("second", 0, payload(b"B")) == "ALREADY_COMMITTED"
assert retry.ack_write("second")
assert retry.authority[0][0] == 2
assert len(retry.journal) == 2

# Multi-page all-or-nothing simulated metadata commit (up to 64KiB).
multi = payload(b"C", 16)
assert retry.begin_write("multi", 16, multi) == "PREPARED"
rejects(lambda: retry.begin_write("overlap", 31, payload(b"X")))
assert retry.begin_write("disjoint", 32, payload(b"D")) == "PREPARED"
rejects(lambda: retry.commit_manifest("multi"))
retry.persist_cow_data("multi")
retry.commit_manifest("multi")
assert retry.ack_write("multi")
assert retry.read(16, 16) == multi
assert retry.simulate_same_boot_restart().read(16, 16) == multi
assert retry.read(32) == bytes(PAGE)  # pending disjoint write never ACKed
rejects(lambda: retry.begin_write("too_large", 32, payload(b"E", 17)))
rejects(lambda: retry.begin_write("unaligned", 32, b"E" * 3))
rejects(lambda: retry.begin_write("out_of_range", 64, payload(b"E")))

# Deliberate corruption: do not touch caller's output on error.
destination = bytearray(b"?" * (PAGE * 2))
retry.simulate_authority_corruption(16)
rejects(lambda: retry.read_into(16, 2, destination), CorruptAuthority)
assert destination == b"?" * (PAGE * 2)

# Corrupt journal or forged generation must be rejected on restore.
clean = after_crash.simulate_same_boot_restart()
last = clean.journal[-1]
bad_digest = replace(last, page_digests=("bad!",))
rejects(lambda: PageContractModel(64, "test-boot-1",
                                  (*clean.journal[:-1], bad_digest)),
        CorruptAuthority)
wrong_gen = replace(last, generations=(100,))
rejects(lambda: PageContractModel(64, "test-boot-1",
                                  (*clean.journal[:-1], wrong_gen)),
        CorruptAuthority)
rejects(lambda: PageContractModel(64, "different-boot",
                                  tuple(clean.journal)))
rejects(lambda: PageContractModel(64, "test-boot-1",
                                  (*clean.journal, last)))

# A newer commit must never be replaced by an older GPU generation.
clean.put_gpu_cache(0, 2, clean.gpu_epoch, payload(b"B"))
clean.gpu_reset()
assert clean.read(0) == payload(b"B")

# DESIGN ONLY: assert no host-mutating, subprocess, GPU or block-device API.
source = (ROOT / "tools/ie03_ack_model.py").read_text(encoding="utf-8")
for forbidden in ("subprocess.", "os.system(", "ctypes", "pyopencl",
                  "open(", "os.open(", "socket(", "swapon(",
                  "swapoff(", "mkswap(", "pwrite(", "fsync("):
    assert forbidden not in source, forbidden

print("IE03_ACK_MODEL_TEST_PASS ack_after_manifest_only=YES"
      " torn_write_keeps_old_generation=YES"
      " identical_replay_idempotent=YES multi_page_atomic_model=YES"
      " stale_gpu_rejected=YES gpu_loss_preserves_authority=YES"
      " output_unchanged_on_corruption=YES kernel_swap=BLOCKED"
      " production_durable_engine=NOT_IMPLEMENTED")
