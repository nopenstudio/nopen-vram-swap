# Nopen VRAM SWAP | Issue #10 | NEXT RUN prerequisites and staging

**Purpose:** Prepare evidence capture for the unexplained 4 GiB host freeze during Bannerlord loading. Normal Bannerlord is functional when Nopen swap is off. A failed game loading UI is **not** being investigated as an unrelated problem.

## What we know

- The physical GPU VRAM allocation and `/dev/ublkb23` activation succeeded; the desktop later completely hung while launching the game after Continue.
- Nopen and zram both had SWAP priority 100. In samples up to 20:11:07, both showed 0 KiB usage. **Nothing proves they were still unused at the later hang.**
- The first known fault and kernel reclaim forward progress are **unproven**. The GPU OpenCL worker runs in userspace and might depend on memory allocation or GPU driver work while the host is reclaiming memory.
- Previous forced reboot left no useful GPU reset, IPC fatal, kernel panic dump or Bannerlord crash dump.
- **No existing test proves another 4 GiB live physical swap run is safe.**

## Phase 0 | prerequisites (NO real swap)

1. Save all work and verify backups. Ensure the machine can be recovered offline. This host has already suffered forced reboots; never risk unsaved data.
2. Ensure only normal zram is active. Stop any previously running Nopen experimental services **only if their experimental swap is inactive**, following existing lifecycle rules.
3. Use the diagnostic branch `diagnostics/issue-10-io-trace` without local uncommitted changes.
4. Verify local build version, actual GPU VRAM available, MemAvailable, services, and /proc/swaps. Our helper prints a structured report and **does not activate swap**:
   ```sh
   python3 v080/tools/issue10_next_swap_readiness.py
   python3 v080/tests/test_issue10_preparation.py
   bash v080/tools/v081_direct4g_kernel_lab.sh preflight
   ```
   A `preparation_gate=PASS` means **read-only prerequisites only**; `kernel_swap_activation_authorized=false` remains true by design.
5. Confirm that the retained zram SWAP can be used for recovery. Nopen's GPU SWAP must not replace zram.
6. Confirm that the local user is not root for mock tests, and that live game/Steam sessions are closed before changing memory allocation state.

## Phase 1 | compile and validate only

```sh
cmake -S v060 -B build-issue10 -DCMAKE_BUILD_TYPE=Debug -DNOPEN_OPENCL=OFF
cmake --build build-issue10 --parallel
ctest --test-dir build-issue10 -R v060_issue10_trace_mock_only --output-on-failure
python3 v080/tests/test_issue10_preparation.py
```

This runs **mock IPC** only. It includes an intentional failure of the **mock** GPU worker, not the physical GPU. It does not activate ublk, mkswap, swapon, swapoff, or modify zram/partitions. The code is not yet a fix for the incident.

## Phase 2 | optional GPU-only coexistence (still NO kernel swap)

Only after Phase 1 passes, separately verify that the isolated GPU worker and Vulkan application coexist **without kernel SWAP**. Prefer short low-capacity tests, then 4 GiB allocation only after its preflight and independent pilot. Use the existing guarded user-space probes; do **not** run the `start` action of `v081_direct4g_kernel_lab.sh`.

## Phase 2A | separate raw ublk transport, no SWAP (experimental)

With both 256MiB and 4GiB GPU-only + Bannerlord coexistence already PASSED, the next unproven component is the **real kernel ublk block-request route**. There is now an independent diagnostic entrypoint, `v080/tools/issue10_ublk_raw_only.sh`, on the Issue #10 branch. It does NOT mount, format, `mkswap`, `swapon` or alter zram. It uses a distinct transient systemd unit, accepts **256MiB only**, and validates three 4KiB raw positions (start/middle/end of a newly created GPU-backed `/dev/ublkb23`).

**Preparation, read-only, non-root:**

```sh
bash v080/tools/issue10_ublk_raw_only.sh preflight 256
python3 v080/tests/test_issue10_raw_only_launcher.py
```

The **real** pilot creates a privileged ublk block device and executes real GPU I/O. Unlike the two commands above, this can still hang the GPU or kernel even without SWAP. It **must not** be treated as a normal game benchmark or as safe on a daily desktop: first ensure verified backups, unsaved work closed, an independent recovery/observer plan and explicit user risk acceptance. The command is intentionally documented in the script's usage output rather than provided as a default stage. Do not run it with Bannerlord open. Running in a disposable/recoverable machine with a suitable AMD GPU is preferable.

**Evidence required:** `ISSUE10_RAW_PAGE_PASS` for all three offsets; `NOPEN_DIAG_UBLK BEGIN/END`, `NOPEN_DIAG_IPC BEGIN/END`, `NOPEN_DIAG_WORKER GPU_ENTER/GPU_RETURN`; successful device removal and service stop. If any step fails or cleanup is incomplete, inspect logs rather than running again.

**Important:** This probes I/O dispatch through ublk but still does not test Linux swap reclaim, swap-in / swap-out, GPU page preservation after reset, or safety of future real 4GiB swap.

## Phase 3 | real SWAP gate remains CLOSED

Before a future physical SWAP experiment, arrange an out-of-band observer (e.g., a separate machine collecting system logs), validate telemetry and recovery, capture a stable baseline, and perform the first live kernel swap in a disposable/recoverable environment rather than on the daily desktop. The current real-swap launcher is **not** an authorized next step.

If a future approved real test is ever performed, it is important to remember:
- GPU buffers, OpenCL runtime host memory and userspace worker do **not** provide a proven deadlock-free kernel swap backend.
- The `NOPEN_DIAG_IO_TRACE=1` setting is opt-in and generates a high volume of events; it can alter performance/timing. Do not enable without a defined bounded trace test.
- The new monitor's `--output-file` calls `fsync()` each second. This improves survival of written samples across a forced reboot, but cannot save any sample that was never taken, and a fully hung disk or filesystem still cannot guarantee persistence.
- Logging to disk is **diagnostic telemetry**, not an SSD-backed swap shadow. Real Nopen swap pages stay in VRAM in direct mode.
- Don't stop the ublk worker while any pages are still swapped to it; forcing cleanup can lose swapped memory.

## Decision criteria

| Check | Result required to proceed |
| --- | --- |
| GitHub mock trace CI | PASS |
| GitHub new preparation/telemetry CI | PASS |
| Local read-only preparation gate | PASS |
| Nopen real SWAP in `/proc/swaps` before test | ABSENT |
| Existing `/dev/zram0` | ACTIVE |
| GPU VRAM headroom after hypothetical 4096 MiB allocation | >= 4096 MiB |
| MemAvailable | >= 2048 MiB |
| Logs, trace, and recovery plan | Ready, independently verified |
| Kernel reclaim / GPU driver forward progress | STILL UNPROVEN |
| Further 4 GiB live physical swap trial | **NO-GO** until isolated evidence & recovery |

**Desired outcome:** isolate whether the first incomplete step is the ublk callback, IPC, or OpenCL worker, without bringing down the main desktop again.
