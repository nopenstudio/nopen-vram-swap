# Nopen Issue #10 | Kernel SWAP host-freeze investigation

## Verified incident (2026-10-31, local UTC+03)
- The normal game is functional **without Nopen GPU SWAP**, as reported by the tester. Do not change scope to game troubleshooting.
- At 20:10:32, `/dev/ublkb23` was activated, 4 GiB on VRAM direct OpenCL, priority 100. Existing `/dev/zram0` had priority 100.
- GPU prefill and `mlock` of Nopen's own metadata/staging succeeded. OpenCL runtime and driver allocations are **not guaranteed to be locked**.
- In 36 samples through 20:11:07, both swaps reported `used_kib=0`. This does **not** establish swap use at the subsequent loading/freeze time.
- The game loading screen and artwork were displayed after **Continue**, but expected running-horse animation failed to appear. Cursor initially moved, then desktop and cursor froze after Super key. Super is a temporal marker, not a proven trigger.
- A Steam update of Proton Experimental completed normally at 20:11:21. There is no captured driver reset, IPC fatal, coredump or game log from the freeze; forced reboot may have lost pending logs.

## Trace points added (opt-in)

Set `NOPEN_DIAG_IO_TRACE=1` in the process environment for a **non-swap** worker integration test:
- `NOPEN_DIAG_UBLK phase=BEGIN|END|FAIL`: ublk callback request, duration and result.
- `NOPEN_DIAG_IPC phase=BEGIN|END|FAIL`: parent/worker IPC, duration, stage and result.
- `NOPEN_DIAG_WORKER phase=BEGIN|GPU_ENTER|GPU_RETURN|RESPONSE_SENT`: isolated worker phases around the backend operation.

All events include monotonic millisecond timestamp, request operation, offset and byte size. They **never log user page contents** and are disabled by default.

## How to identify first stalled stage, if durable logs are available
1. `UBLK BEGIN` without `IPC BEGIN`: callback may be blocked before reaching the RPC, e.g. waiting on a serialized mutex; not proof by itself.
2. `IPC BEGIN` without `WORKER BEGIN`: worker scheduling, socket delivery or other IPC path needs investigation.
3. `WORKER GPU_ENTER` without `GPU_RETURN`: likely inside a backend operation, including OpenCL/driver, checksum or other backend functions. Not automatically a hardware hang.
4. `GPU_RETURN` without `RESPONSE_SENT`: worker response/socket send path.
5. `RESPONSE_SENT` without `IPC END`: parent socket receive, integrity verification, scheduling or timeout.
6. `IPC END` without `UBLK END`: downstream callback and completion path.

**Absence of a line is not proof of where a hang occurred.** Journald and file logs can lose final buffered records on forced reset, and enabling detailed per-request stderr logging changes timing.

## SAFE mock-only regression (no real OpenCL, no kernel SWAP)

From the repository on a normal user account:

```sh
cmake -S v060 -B build-issue10 -DCMAKE_BUILD_TYPE=Debug -DNOPEN_OPENCL=OFF
cmake --build build-issue10 --parallel
ctest --test-dir build-issue10 -R v060_issue10_trace_mock_only --output-on-failure
```

This exercises 601 mock-mode IPC transfers and a deliberately unresponsive **mock worker**, checking traces and the 100ms fail-closed behavior. It does **not** issue `swapon`, `swapoff`, `mkswap`, invoke the ublk target or modify zram. Building writes normal build artifacts under `build-issue10`.

After mock-mode validation, isolate GPU-process coexistence under game load **without activating kernel swap**, and only then investigate ublk/kernel forward progress using a recoverable lab host. No 4 GiB physical-host live SWAP test is authorized by this document.

## Root-cause candidates NOT confirmed
- Kernel reclaim depends on userland/OpenCL runtime allocations -> a forward-progress cycle.
- OpenCL GPU work and RADV/KWin contend on the GPU while game is loading.
- A ublk request is stalled while a single worker queue or IPC mutex is held.
- Other system driver/compositor issue correlated with enabled ublk.

The goal is evidence of the **first incomplete phase**, not tuning performance or blaming the game.
