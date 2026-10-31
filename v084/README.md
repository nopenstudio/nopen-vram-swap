# Nopen VRAM SWAP v0.8.4: Reclaim circular-wait investigation

**STATUS: diagnostic DEVELOPMENT branch. Real kernel SWAP is BLOCKED.**

This version investigates a specific plausible explanation for the earlier
primary-host hard freeze observed with 4GiB GPU-backed `ublk` swap + Bannerlord:
**the kernel needs a userspace I/O completion to reclaim memory, but completing
that I/O may itself require memory or scheduling resources that reclaim has
not yet made available.**

**This is a hypothesis, not a forensic attribution of the previous freeze.**

## Evidence, not guesses

- **Physical evidence:** Issue #10's 256MiB *raw-only* `ublk` session
  successfully completed **165/165 8-stage requests** (162 READ, 3 WRITE,
  verified 4KiB roundtrips at first/middle/last block); no actual kernel
  swap, game or heavy reclaim was involved.
- **Code evidence:** the `v070` `ublk` `on_io()` callback waits for
  `nopen_isolated_read/write`, which synchronously exchanges requests with
  `v060`'s userspace worker. The worker calls `nopen_blk_read/write`;
  `v012/src/nopen_store.c` uses blocking `clEnqueueReadBuffer(..., CL_TRUE)`
  and `clEnqueueWriteBuffer(..., CL_TRUE)`, plus OpenCL driver queues and
  page-buffer resources.
- **Insufficient protection:** Nopen locks the worker's explicit 64KiB
  staging buffer and backend metadata/staging under the physical lab, and
  applies best-effort OOM-score protection; this **does not** prove the OpenCL
  runtime, GPU driver, `ublksrv` buffers and every kernel path are
  reclaim-independent. GPU prefill `clFinish` does **not** physically pin
  all GPU pages or guarantee progress under another workload.
- **Kernel documentation:** filesystem/I/O paths that allocate memory can
  recurse into direct reclaim. In kernel code there are special
  `memalloc_noio_save` scopes, but inserting such a call into userspace is
  not an available blanket solution:
  https://docs.kernel.org/core-api/memory-allocation.html
  https://kernel.org/doc/html/v6.6/core-api/gfp_mask-from-fs-io.html
- **ublk semantics:** every kernel block request requires a userspace server
  response; a user daemon dying is not equivalent to reclaim making progress:
  https://docs.kernel.org/block/ublk.html
- **Historical analogy:** even NBD swap over a networking stack has had
  documented circular-memory-dependency deadlocks and needed specialized
  kernel memory-reserve handling, which does not automatically apply to an
  OpenCL-backed `ublk` userspace stack:
  https://man.archlinux.org/man/nbd-client.8.en

## What is implemented

1. `tools/reclaim_cycle.py`: deterministic directed wait-for graphs.
   Scenario `cycle` models `kernel_reclaim → ublk_completion →
   nopen_ublk_server → gpu_worker → opencl_driver → host_allocation →
   kernel_reclaim`. It detects the back edge. Scenario
   `hypothetical-reserve` illustrates how *actually independent*,
   fully preallocated I/O resources could remove a graph edge; it is
   **not** a current Nopen implementation, and its swap gate stays BLOCKED.
2. `NOPEN_V084_MOCK_WAIT_ON_RECLAIM=read|write`: **mock backend only**,
   a normal read/write operation is accepted by a live worker, which
   deliberately waits on a *synthetic unavailable memory dependency*
   instead of responding. The parent must enforce its bounded deadline,
   fail closed, and leave the caller's READ buffer unchanged.
   There is **no actual memory pressure, page fault, VRAM use or kernel
   device** in this test.
3. `tests/test_mock_reclaim_stall.c`: normal unmodified roundtrip passes,
   then both stalled read and stalled write return `-ETIMEDOUT` with
   `stats.child_alive=0`, `io_errors=1`, `timeouts=1`; subsequent
   operations return `-EIO`. This proves only bounded **userspace
   mock IPC behavior**, not survival of kernel reclaim or a GPU D-state.
4. `tests/test_reclaim_cycle.py`: cycle, acyclic idealized model,
   self-loop and additional disconnected cycle cases; both models
   explicitly forbid live swap.

## Safe workstation build (no sudo)

From a CLEAN new worktree, with `develop/v0.8.4` fetched:

```sh
cmake -S v084 -B build-v084-mock -DCMAKE_BUILD_TYPE=Debug -DNOPEN_OPENCL=OFF
cmake --build build-v084-mock --parallel 2
ctest --test-dir build-v084-mock -R '^v084_' --output-on-failure
ctest --test-dir build-v084-mock -R '^v083_' --output-on-failure
python3 v084/tools/reclaim_cycle.py --scenario both
```

**Never use `swapon`, `swapoff`, `mkswap` or the physical
4GiB kernel-lab scripts for v0.8.4 verification on the user's desktop.**
Keep existing zram unchanged; Bannerlord is not required.

## Decision and next engineering gates

**`KERNEL_SWAP_BLOCKED`**, even if every test passes.

To progress toward a potentially releasable system, independent physical
reclaim progress must be demonstrated first, ideally using a disposable
host or an externally controllable machine with logs recorded outside the
target. Before that, a concrete independent-reserve architecture must
account for userspace/driver/kernel allocations; an error path alone is
insufficient because a failed swap-in might irrecoverably lose application
pages. The other open requirement is recoverability after GPU reset.
No working 4GiB live GPU swap or Bannerlord FPS/host-RAM benefit is
claimed in this version.
