# Nopen VRAM SWAP - engineering changelog

**Not a list of stable releases. All milestones below are experimental.**
Kernel SWAP activation remains `KERNEL_SWAP_BLOCKED`.

## v0.8.5 - 2026-10-31 (Reserved VRAM Phase A/B)

- Added read-only AMDGPU VRAM/GTT capacity preflight and counter observer.
- Implemented a bounded 1 GiB GPU-only OpenCL Holder, data tagging, complete readback, samples, and safety guards.
- **RX 9060 XT verified:** 1 GiB direct OpenCL arena pass (1.084 s); 15-second Holder start/end full-integrity checks PASS. Immediately-after-close VRAM counter remained ~1,931 MiB vs ~763 MiB baseline; further observation needed.
- No exclusive VRAM partition, GPU pin guarantee, game-visible 12 GiB limit or live kernel SWAP.

## v0.8.4 - 2026-10-31 (hypothetical kernel reclaim cycle)

- Modeled circular wait-for dependencies and an *idealized* independent-reserve alternative.
- Added mock-only READ/WRITE stalled-worker timeout tests; locally 2/2 PASS.
- Not a reproduction of the earlier full host freeze.

## v0.8.3 - 2026-10-31 (worker reliability)

- Added mock fault matrix: timeout, worker crash and corrupted IPC response.
- Fail-closed with unchanged read destination on failure; locally 2/2 PASS.
- Added conservative static reclaim audit; closed live-SWAP authorization.

## v0.8.1 and Issue #10 (raw-only ublk diagnosis)

- Real 256 MiB raw ublk OpenCL session completed 165/165 I/O requests, verified three blocks.
- The earlier **4 GiB kernel SWAP + Bannerlord experiment froze the host**. Cause not established. Do not repeat on a primary PC.

## v0.8.0 - v0.2.0 (historical experimental layers)

- OpenCL GPU transfer performance, shadow storage, prefetch, fault and lifecycle testing, process memory policies, integrity checks and diagnostics.
- Retained in version folders with historical documentation and tests. See [research index](docs/RELEASES.md).

## v0.12.0-alpha.1 ... alpha.5 (historical architecture series)

- Early `userfaultfd` process-local paging experiments and freestanding C11 page policies. The release numbering belongs to an earlier experimental track.

## v0.10 Vulkan probe (historical independent prototype)

- Standalone Vulkan `DEVICE_LOCAL | HOST_VISIBLE` memory probe still available via `NOPEN_BUILD_LEGACY_VULKAN=ON`; not the default product path.

## Legacy NBD (0.9.x archive)

- Historical active NBD kernel swap experiments produced I/O stalls, SIGBUS and serious system failures. Preserved only for traceability, not routine use.
