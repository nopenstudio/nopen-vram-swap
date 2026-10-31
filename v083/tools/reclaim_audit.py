#!/usr/bin/env python3
"""Nopen v0.8.3 conservative, source-based kernel reclaim risk audit.

Does not run the worker, create ublk, touch /proc/swaps, load modules,
initialize OpenCL, call mlock or change any process settings.
A source audit cannot establish absence of reclaim deadlocks.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    "ipc": "v060/src/nopen_isolated_io.c",
    "worker": "v060/src/nopen_isolated_worker.c",
    "block": "v050/src/nopen_blk_backend.c",
    "ublk": "v070/experimental/ublk/nopen_ublk_swap_target.cpp",
}
# Named invariants checked against source. These are design observations,
# not proofs that the allocator or GPU driver can run during reclaim.
REQUIRED = {
    "ipc_deadline": ("ipc", ("io_wait(", "poll(&p,1,remaining)", "fail_io(", "SIGKILL")),
    "spawn_worker_before_IO": ("ipc", ("posix_spawn(", "nopen_isolated_open(", "recv_exact(")),
    "metadata_mlock_only": ("block", ("mlock(b->meta", "mlock(b->staging")),
    "worker_staging_mlock": ("worker", ("mlock(buffer,NOPEN_BLK_MAX_BYTES)",)),
    "OpenCL_runtime_not_mlocked": (
        "worker", ("OpenCL", "runtime/driver allocations are NOT mlocked")),
    "GPU_path_userspace_runtime": ("block", ("nopen_store_write_pages(", "nopen_store_read_pages(")),
    "volatile_GPU_backing": ("ublk", ("UBLK_ATTR_VOLATILE_CACHE", "NOPEN_UBLK_STORAGE_MODE")),
    "kernel_reclaim_unproven": ("ublk", ("kernel_reclaim_forward_progress=UNPROVEN",)),
}
BLOCKERS = [
    {
        "id": "GPU_RUNTIME_RECLAIM_DEPENDENCY",
        "severity": "BLOCKER",
        "detail": "OpenCL and GPU driver runtime allocations, page locks and scheduling are not fully prefaulted/mlocked or proven reclaim-independent.",
    },
    {
        "id": "UBLK_USERSPACE_FORWARD_PROGRESS",
        "severity": "BLOCKER",
        "detail": "Linux may require ublk request completion while Nopen userspace worker or driver requires memory/scheduler progress; IPC timeout cannot prove kernel forward progress.",
    },
    {
        "id": "VOLATILE_PAGE_LOSS",
        "severity": "BLOCKER",
        "detail": "VRAM-only backing is lost on GPU reset/reboot, with no durable recovery of pages acknowledged to Linux.",
    },
    {
        "id": "PRESSURE_AND_LOCKUP_REPRO_NOT_ISOLATED",
        "severity": "BLOCKER",
        "detail": "Earlier 4GiB live kernel SWAP + Bannerlord full-host freeze remains unexplained; raw ublk pass is not a reclaim test.",
    },
]


def audit_texts(texts: dict[str, str]) -> dict:
    observations = {}
    for name, (src, needles) in REQUIRED.items():
        observations[name] = all(needle in texts.get(src, "") for needle in needles)
    missing = [name for name, present in observations.items() if not present]
    return {
        "schema": "NOPEN_V083_RECLAIM_FORWARD_PROGRESS_AUDIT",
        "version": "0.8.3",
        "evidence_type": "STATIC_SOURCE_REVIEW_ONLY",
        "code_evidence_intact": not missing,
        "missing_required_evidence": missing,
        "observations": observations,
        "blocking_risks": BLOCKERS,
        "physical_kernel_swap_authorized": False,
        "reclaim_forward_progress_proven": False,
        "real_GPU_context_created": False,
        "kernel_device_created": False,
        "host_disks_modified": False,
        "zram_modified": False,
        "gate": "KERNEL_SWAP_BLOCKED",
    }


def audit(root: Path = ROOT) -> dict:
    return audit_texts({
        key: (root / rel).read_text(encoding="utf-8")
        for key, rel in SOURCES.items()
    })


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci", action="store_true",
                        help="short CI result, never starts GPU/SWAP")
    args = parser.parse_args()
    try:
        result = audit()
    except (OSError, UnicodeError) as exc:
        print(f"V083_RECLAIM_AUDIT_FAILED source_unavailable={exc}", file=sys.stderr)
        return 2
    if not args.ci:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    if not result["code_evidence_intact"]:
        print("V083_RECLAIM_AUDIT_FAILED missing=" +
              ",".join(result["missing_required_evidence"]), file=sys.stderr)
        return 2
    print("V083_RECLAIM_AUDIT_PASS source_checks=8"
          " kernel_swap_gate=BLOCKED physical_swap=NO"
          " gpu=NO userspace_worker_reclaim_safety=UNPROVEN")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
