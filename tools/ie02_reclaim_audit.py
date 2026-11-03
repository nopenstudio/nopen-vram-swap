#!/usr/bin/env python3
"""IE-02: read-only, evidence-addressed reclaim dependency audit.

No process execution, kernel devices, GPU context, /proc writes, root, swap
commands, systemd operations or memory-pressure injection. This is NOT a
physical deadlock detector; unobservable kernel/driver edges remain hypotheses.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = "NOPEN_IE02_RECLAIM_DEPENDENCY_AUDIT_V1"

# A line fragment uniquely identifying a structural dependency or a startup
# mitigation. Strict observations: stale source should break the audit.
# phase=init means before first I/O (NOT a guarantee for later requests).
EVIDENCE = (
    ("KERNEL_GATE", "v070/experimental/ublk/nopen_ublk_swap_target.cpp",
     "const bool physical_lab=", "init", "host-safety",
     "Physical-host ublk path exists in source but is fail-closed."),
    ("UBLK_CALLBACK", "v070/experimental/ublk/nopen_ublk_swap_target.cpp",
     "int on_io(const ublksrv_queue* q,const ublk_io_data* io)", "hot",
     "kernel/userspace",
     "ublk request enters a userspace callback."),
    ("UBLK_AWAITS_BACKING", "v070/experimental/ublk/nopen_ublk_swap_target.cpp",
     "state->shadow?nopen_shadow_read(", "hot", "blocking",
     "Read dispatch calls the selected backing before completion."),
    ("UBLK_COMPLETION", "v070/experimental/ublk/nopen_ublk_swap_target.cpp",
     "ublksrv_complete_io(q,io->tag,rc==0?static_cast<int>(bytes):rc)",
     "hot", "kernel/userspace",
     "The request is completed only after backing returns."),
    ("UBLK_LIB_API", "v070/experimental/ublk/nopen_ublk_swap_target.cpp",
     ".handle_io_async=on_io,", "init", "blocking",
     "Async API registration does not make this callback nonblocking."),
    ("IPC_TRANSFER_TIMER_START", "v060/src/nopen_isolated_io.c",
     "const uint64_t begin_ms=monotonic_ms();", "hot", "mutex",
     "Callers enter a serialized IPC transfer; admission policy is checked below."),
    ("IPC_TIMER", "v060/src/nopen_isolated_io.c",
     "uint64_t deadline=begin_ms+r->timeout_ms;", "hot", "timer",
     "Starts dispatched operation deadline after a bounded or legacy admission stage."),
    ("IPC_POLL", "v060/src/nopen_isolated_io.c",
     "int rc=poll(&p,1,remaining);", "hot", "socket-wait",
     "Bounds parent userspace IPC polling, not the GPU driver's kernel wait."),
    ("IPC_FATAL_WORKER_KILL", "v060/src/nopen_isolated_io.c",
     "if(kill(r->pid,SIGKILL)<0 && errno!=ESRCH){}", "error", "lifecycle",
     "SIGKILL cannot guarantee exit from uninterruptible D-state."),
    ("IPC_PARENT_STAGING", "v060/src/nopen_isolated_io.c",
     "r->staging=malloc(NOPEN_BLK_MAX_BYTES);", "init", "userspace-RAM",
     "Parent staging is prefaulted with optional strict mlock; driver allocations remain unprotected."),
    ("WORKER_READY", "v060/src/nopen_isolated_worker.c",
     "nopen_ipc_msg msg={NOPEN_IPC_MAGIC,NOPEN_IPC_READY,0,0,",
     "init", "lifecycle",
     "Worker READY only follows successful staging allocation and prefault."),
    ("WORKER_STAGING_PRE_READY", "v060/src/nopen_isolated_worker.c",
     "buffer=malloc(NOPEN_BLK_MAX_BYTES);", "init",
     "userspace-RAM",
     "Worker staging is allocated before READY."),
    ("WORKER_STAGING_MLOCK_PRE_READY", "v060/src/nopen_isolated_worker.c",
     "if(mlock(buffer,NOPEN_BLK_MAX_BYTES)==0)", "init", "mlock",
     "Worker buffer is prefaulted and locked (strictly required in strict mode) before READY."),
    ("WORKER_OOM_ADJ_BEST_EFFORT", "v060/src/nopen_isolated_worker.c",
     "FILE *score=fopen(\"/proc/self/oom_score_adj\",\"w\");", "init",
     "OOM-policy", "OOM score adjustment is best effort, not I/O liveness."),
    ("BLOCK_ALLOC_META", "v050/src/nopen_blk_backend.c",
     "b->meta=calloc(b->pages,sizeof(*b->meta));", "init",
     "userspace-RAM", "Page metadata is allocated at startup."),
    ("BLOCK_OPEN_GPU_AFTER_MLOCK", "v050/src/nopen_blk_backend.c",
     "if(nopen_store_open(&b->store,backend,mib)!=0)", "init",
     "driver-memory", "Strict metadata is locked before OpenCL store is opened."),
    ("BLOCK_META_MLOCK", "v050/src/nopen_blk_backend.c",
     "if(mlock(b->meta,b->pages*sizeof(*b->meta))!=0", "init",
     "mlock", "Only selected metadata/staging buffers are locked."),
    ("BLOCK_IO_MUTEX", "v050/src/nopen_blk_backend.c",
     "pthread_mutex_lock(&b->lock);", "hot", "mutex",
     "Block backend holds a mutex around blocking store I/O."),
    ("BLOCK_TO_OPENCL", "v050/src/nopen_blk_backend.c",
     "if(nopen_store_write_pages(b->store,first,src,count)!=0)",
     "hot", "driver-call", "Synchronous call into the OpenCL store."),
    ("OPENCL_CONTEXT", "v012/src/nopen_store.c",
     "s->ctx=clCreateContext(", "init", "driver-memory",
     "GPU context is created before kernel SWAP activation."),
    ("OPENCL_RESIDENCY_NOT_PIN", "v012/src/nopen_store.c",
     "pinned_physical_vram=UNPROVEN", "init", "GPU-residency",
     "OpenCL prefill is not a physical VRAM residency guarantee."),
    ("OPENCL_READ_BLOCKS", "v012/src/nopen_store.c",
     "clEnqueueReadBuffer(s->queue,seg,CL_TRUE,", "hot",
     "driver-call", "A blocking OpenCL read occurs in the worker hot path."),
    ("OPENCL_WRITE_BLOCKS", "v012/src/nopen_store.c",
     "clEnqueueWriteBuffer(s->queue,seg,CL_TRUE,", "hot",
     "driver-call", "A blocking OpenCL write occurs in the worker hot path."),
    ("OPENCL_PINNED_BRANCH_FINISH", "v012/src/nopen_store.c",
     "if(rc==CL_SUCCESS)rc=clFinish(s->queue);", "hot",
     "driver-call", "Pinned-host optional path can also wait in clFinish."),
    ("SHADOW_DISK_AUTHORITY", "v070/src/nopen_shadow.c",
     "O_CREAT|O_EXCL|O_RDWR|O_DIRECT|O_DSYNC|O_CLOEXEC",
     "init", "filesystem", "Independent file authority, not an in-memory mirror."),
    ("SHADOW_PREALLOC", "v070/src/nopen_shadow.c",
     "posix_fallocate(s->fd,0,(off_t)(mib*MIB));", "init",
     "filesystem", "File is preallocated before use, reducing allocation risk."),
    ("SHADOW_DISK_MUTEX", "v070/src/nopen_shadow.c",
     "pthread_mutex_lock(&s->disk_lock);", "hot", "mutex",
     "Serializes disk access and staging buffer."),
    ("SHADOW_SYNC_WRITE", "v070/src/nopen_shadow.c",
     "const int io_rc=full_pwrite(s->fd,s->aligned,len,(off_t)off);",
     "hot", "filesystem",
     "O_DSYNC pwrite can block in VFS/filesystem/block layers."),
    ("SHADOW_SYNC_READ", "v070/src/nopen_shadow.c",
     "const int read_rc=full_pread(s->fd,s->aligned,len,(off_t)off);",
     "hot", "filesystem",
     "SSD fallback still waits on filesystem/block I/O."),
    ("SHADOW_GPU_READ_UNDER_METADATA", "v070/src/nopen_shadow.c",
     "const int gpu_rc=nopen_isolated_read(s->gpu,off,s->aligned,len);",
     "hot", "mutex",
     "GPU read is invoked while shadow metadata/disk locks are held."),
    ("SHADOW_UPLOADER_JOIN", "v070/src/nopen_shadow.c",
     "pthread_join(s->uploader,NULL);", "teardown", "lifecycle",
     "Unbounded uploader join during close can block teardown."),
    ("SYSTEM_SERVICE_INDEPENDENT", "v070/tools/vram_swap_lab.sh",
     "systemd-run --system --quiet --unit=nopen-v070-ublk",
     "init", "lifecycle",
     "Root system service mitigates old GUI-session lifecycle hazard."),
    ("SINGLE_UBLK_QUEUE", "v070/tools/vram_swap_lab.sh",
     "-q 1 -d 16", "init", "queue",
     "One queue and finite depth; not an I/O progress guarantee."),
)

# SOURCE-OBSERVED edges are real call ordering, NOT proof of kernel/driver
# scheduling. Assumed edges are clearly separated in model_scenarios().
GPU_SOURCE_PATH = (
    ("ublk_callback", "isolated_ipc"),
    ("isolated_ipc", "worker_block_backend"),
    ("worker_block_backend", "opencl_blocking_call"),
)
SHADOW_SOURCE_PATH = (
    ("ublk_callback", "shadow_disk_io"),
    ("shadow_disk_io", "vfs_block_layer"),
)

def _cycle(graph: dict[str, tuple[str, ...]]) -> list[str]:
    """Reuse the existing v084 graph algorithm; do not introduce a new one."""
    ref = ROOT / "v084/tools/reclaim_cycle.py"
    spec = importlib.util.spec_from_file_location("ie02_v084_graph", ref)
    if spec is None or spec.loader is None:
        raise RuntimeError("v084 graph algorithm unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.cycle_in_wait_graph(graph)

def model_scenarios() -> list[dict]:
    scenarios = (
        ("gpu_only_conditional_host_allocation", {
            "kernel_reclaim": ("ublk_completion",),
            "ublk_completion": ("ublk_callback",),
            "ublk_callback": ("isolated_ipc",),
            "isolated_ipc": ("worker_block_backend",),
            "worker_block_backend": ("opencl_blocking_call",),
            "opencl_blocking_call": ("driver_host_allocation",),
            # This final edge is a HYPOTHESIS. AMDGPU/driver stacks not captured.
            "driver_host_allocation": ("kernel_reclaim",),
        }, ("driver_host_allocation -> kernel_reclaim",)),
        ("shadow_conditional_filesystem_reclaim", {
            "kernel_reclaim": ("ublk_completion",),
            "ublk_completion": ("ublk_callback",),
            "ublk_callback": ("shadow_disk_io",),
            "shadow_disk_io": ("vfs_block_layer",),
            # An allocation/reclaim dependency in VFS/block under pressure
            # is a HYPOTHESIS, not shown by the userspace source.
            "vfs_block_layer": ("kernel_reclaim",),
        }, ("vfs_block_layer -> kernel_reclaim",)),
        ("idealized_nonallocating_service_not_implemented", {
            "kernel_reclaim": ("ublk_completion",),
            "ublk_completion": ("ublk_callback",),
            "ublk_callback": ("isolated_ipc",),
            "isolated_ipc": ("independent_preallocated_backend",),
            "independent_preallocated_backend": (),
        }, ()),
    )
    return [{
        "name": name,
        "evidence": "CONDITIONAL_WAIT_FOR_MODEL_NOT_PHYSICAL_TRACE",
        "cycle_path": _cycle(graph),
        "conditional_edges": list(conditional),
        "kernel_stacks_captured": False,
        "real_forward_progress_proven": False,
    } for name, graph, conditional in scenarios]

def audit_texts(texts: dict[str, str]) -> dict:
    evidence = []
    missing = []
    for ident, path, needle, phase, resource, finding in EVIDENCE:
        lines = texts.get(path, "").splitlines()
        locations = [i + 1 for i, line in enumerate(lines) if needle in line]
        if not locations:
            missing.append(ident)
        evidence.append({
            "id": ident, "file": path, "line": locations[0] if locations else None,
            "phase": phase, "resource": resource, "finding": finding,
            "anchor_present": bool(locations),
        })
    ipc_lines = texts.get("v060/src/nopen_isolated_io.c", "").splitlines()
    worker_lines = texts.get("v060/src/nopen_isolated_worker.c", "").splitlines()
    block_lines = texts.get("v050/src/nopen_blk_backend.c", "").splitlines()
    def first(ls: list[str], mark: str) -> int:
        return next((i for i, line in enumerate(ls, 1) if mark in line), 0)
    # IE-01 is an independent PR. Support either the baseline's legacy
    # unbounded mutex admission or its bounded trylock policy once merged.
    # This audit records which source mode was examined; a refactor must
    # never silently make source audit tests stale.
    ipc = "\n".join(ipc_lines)
    transfer = ipc.split("static int transfer(", 1)[-1].split(
        "int nopen_isolated_read(", 1)[0]
    legacy = ("pthread_mutex_lock(&r->lock);" in transfer
              and transfer.find("pthread_mutex_lock(&r->lock);") <
                  transfer.find("uint64_t deadline=begin_ms+r->timeout_ms;"))
    bounded = ("const int admitted=admit(r);" in transfer
               and "if(admitted)return admitted;" in transfer
               and "pthread_mutex_trylock(&r->lock)" in ipc
               and "deadline=start+r->admission_timeout_ms" in ipc
               and transfer.find("const int admitted=admit(r);") <
                   transfer.find("uint64_t deadline=begin_ms+r->timeout_ms;"))
    admission_mode = ("legacy_unbounded_mutex" if legacy and not bounded else
                      "bounded_trylock_policy" if bounded and not legacy else
                      "UNRECOGNIZED")
    ordered = {
        "ipc_admission_precedes_request_deadline":
          admission_mode != "UNRECOGNIZED",
        "worker_staging_precedes_ready":
          0 < first(worker_lines, "buffer=malloc(NOPEN_BLK_MAX_BYTES);")
            < first(worker_lines, "if(mlock(buffer,NOPEN_BLK_MAX_BYTES)==0)")
            < first(worker_lines, "NOPEN_IPC_READY,0,0,"),
        "gpu_metadata_mlock_precedes_store_open":
          0 < first(block_lines, "if(mlock(b->meta,")
            < first(block_lines, "nopen_store_open(&b->store"),
    }
    missing.extend(key for key, ok in ordered.items() if not ok)
    physical_script = texts.get("v080/tools/v081_direct4g_kernel_lab.sh", "")
    direct4g_start_is_routable = "start) :;;" in physical_script
    return {
        "schema": SCHEMA, "scope": "IE01/02 runtime patched, mock/static only",
        "source_contract_intact": not missing,
        "missing_or_reordered": missing,
        "evidence": evidence,
        "ordering_checks": ordered,
        "ipc_admission_mode": admission_mode,
        "model_scenarios": model_scenarios(),
        "live_direct4g_launcher_present_on_baseline": direct4g_start_is_routable,
        "unverified_kernel_or_driver_edges": [
            "Kernel swap fault path needing this exact ublk completion under pressure",
            "AMDGPU/OpenCL host allocation requiring the blocked reclaim progress",
            "VFS/filesystem/blk-mq forward progress and GFP behavior under memory pressure",
            "GPU driver D-state / GPU reset / scheduler liveness and full context residency",
            "Fault-handler stacks, kernel lockdep, PSI and independently preserved incident logs",
            "End-to-end ublk completion time incl. queue admission and storage backend",
        ],
        "observed_root_cause_of_issue_10": "UNKNOWN",
        "reclaim_independence_proven": False,
        "data_recovery_proven": False,
        "physical_gpu_test_run": False,
        "kernel_swap_test_run": False,
        "host_changed": False,
        "release_version": "NOT_RELEASED",
        "gate": "KERNEL_SWAP_BLOCKED",
    }

def audit(root: Path = ROOT) -> dict:
    paths = {e[1] for e in EVIDENCE}
    paths.add("v080/tools/v081_direct4g_kernel_lab.sh")
    return audit_texts({
        path: (root / path).read_text(encoding="utf-8") for path in sorted(paths)
    })

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci", action="store_true", help="compact read-only CI")
    args = parser.parse_args()
    try:
        result = audit()
    except (OSError, UnicodeError, RuntimeError) as error:
        print(f"IE02_AUDIT_FAIL unable_to_read_source={error}", file=sys.stderr)
        return 2
    if not args.ci:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    if not result["source_contract_intact"]:
        print("IE02_AUDIT_FAIL missing_or_reordered=" +
              ",".join(result["missing_or_reordered"]), file=sys.stderr)
        return 1
    print(f"IE02_SOURCE_AUDIT_PASS anchors={len(EVIDENCE)}"
          " actual_reclaim_test=NO gpu_driver_proof=NO"
          " kernel_swap=BLOCKED issue10_root_cause=UNKNOWN")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
