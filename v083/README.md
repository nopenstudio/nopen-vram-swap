# Nopen VRAM SWAP v0.8.3 | Kernel SWAP Reliability Diagnostic

**Status: development / mock-only validation. NOT a safe kernel SWAP release.**

This phase inherits the verified v0.8.1 GPU-only and Issue #10 raw ublk
diagnostics. It targets the unresolved full-host freeze observed when the
primary Garuda Linux system used **4GiB actual GPU-backed kernel SWAP while
running Bannerlord**. The raw-only 256MiB physical ublk run completed 165/165
requests with three independently verified 4KiB write/read/cmp checks and
no kernel SWAP. None of those tests proves reclaim forward progress.

## Implemented in v0.8.3

1. **Mock worker hard exit:** `NOPEN_IPC_EXIT_TEST_ONLY`. The mock backend
   exits without a response. The parent must fail the request, kill/reap
   the worker within a bounded interval and permanently reject subsequent I/O.
2. **Mock malformed reply:** `NOPEN_IPC_BAD_REPLY_TEST_ONLY`. The mock
   backend returns a response with invalid magic; the parent rejects it
   with a protocol error and permanently fails closed.
3. **Existing mock timeout regression:** deliberate 10-second worker hang
   with a short parent deadline, plus unchanged write/read/checksum.
4. **Reclaim risk audit:** `tools/reclaim_audit.py` checks source-level
   invariants (worker staging/metadata mlock, bounded parent IPC, OpenCL
   userspace dependency, volatile GPU backing). Its **gate is always
   KERNEL_SWAP_BLOCKED**: an audit cannot establish physical safety.
5. Dedicated CTest/CMake build and GitHub Actions mock-only job. It uses no
   `/dev/ublk*`, no OpenCL context and no privileged SWAP commands.

## Safe workstation test (no sudo)

On Garuda Linux with a working C/CMake toolchain:

```sh
git fetch origin
git switch --track origin/develop/v0.8.3
cmake -S v083 -B build-v083-mock -DCMAKE_BUILD_TYPE=Debug -DNOPEN_OPENCL=OFF
cmake --build build-v083-mock --parallel 2
ctest --test-dir build-v083-mock -R '^v083_' --output-on-failure
ctest --test-dir build-v083-mock -R 'v060_issue10_trace_mock_only' --output-on-failure
python3 v083/tools/reclaim_audit.py
```

If `develop/v0.8.3` is already checked out, use `git pull --ff-only`
instead of `git switch --track`. The CMake build is userspace only.
Successful tests **do not permit** `mkswap`, `swapon`, `swapoff` or
the previous 4GiB kernel-SWAP lab on the daily-use computer.

## What PASS establishes

- IPC mock worker crashes, timeout and corrupted headers do not silently
  produce successful reads/writes or leak overwritten output data.
- The child is marked unavailable and there is no transparent background
  recovery or implicit fallback to other storage.
- The test executable never opens an OpenCL context or creates a kernel
  block device.
- Source evidence for current reclaim dependencies was inspected.

## Still UNPROVEN (release blockers)

- GPU runtime and kernel driver **forward progress** while the kernel is
  waiting for swap-in or swap-out under hard memory pressure.
- All allocations, page faults and scheduler dependencies of OpenCL being
  prefaulted, pinned, reclaim-independent or GPU-reset safe.
- Volatile VRAM page durability through GPU reset/reboot.
- Correctness when a userspace GPU worker is uninterruptible in kernel
  D-state or after the driver has stopped servicing GPU work.
- Long-duration performance and coexistence under reclaim; FPS and RAM
  extension have **not** been demonstrated with live GPU swap.

**No automatic upgrade to kernel SWAP.** Next work is an isolated,
recoverable forward-progress experiment with telemetry and a recovery plan,
or an architectural alternative that does not create an unbounded reclaim
cycle through the GPU runtime. Do not launch a 4GiB swap + Bannerlord repeat
on the primary workstation.
