# Nopen VRAM SWAP

**Experimental AMD GPU-memory research for Linux and future NopenOS.**  
**Current development: v0.8.5 Reserved VRAM, not a usable kernel SWAP product.**

[Türkçe](README_TR.md) · [Project status](docs/STATUS.md) · [Architecture](docs/ARCHITECTURE.md) · [Safety rules](docs/SAFETY.md) · [Build and tests](docs/DEVELOPMENT.md) · [Version history](docs/RELEASES.md)

> **SAFETY: KERNEL_SWAP_BLOCKED.** Nopen does not currently offer safe system-wide VRAM-backed SWAP. Earlier live NBD and 4 GiB ublk SWAP experiments produced serious failures, including a complete host freeze. Do not enable archived SWAP launchers on your everyday computer. The installed **zram** should be left alone.

## What is verified

| Component | Evidence | Important limitation |
|---|---|---|
| Process-local OpenCL GPU storage | Tagged 1 GiB full-arena write/read, AMD RX 9060 XT | Not pinned physical VRAM |
| v0.8.5 soft Holder | 1 GiB, 15-second real GPU run; initial and final integrity PASS | Immediately-after-close global VRAM accounting remained elevated; release not yet established |
| v0.8.5 preflight/observer | Real AMDGPU VRAM/GTT and MemAvailable counters | Read-only budget, not actual reservation |
| v0.8.4 reclaim-cycle model | Synthetic cycle and alive-but-stalled mock I/O tests | Real kernel deadlock cause still unknown |
| v0.8.3 isolated worker | Mock crash/timeout/bad-IPC failures handled fail-closed | Not a kernel reclaim survival proof |
| Issue #10 raw ublk | 256 MiB, 165/165 I/O requests complete, 3 verified block roundtrips | No kernel SWAP |

**Latest first-party hardware observations:** the v0.8.5 Holder raised global VRAM use from approximately **763 MiB** to **1,944–1,957 MiB** while holding a 1 GiB OpenCL pool. Immediately after closing, the counter was **1,931 MiB**, but the next-day read-only series returned to **399–426 MiB**. The elevated reading did not persist; **the time or mechanism of release remains unproven**, because observations were separated by hours and the intervening reboot/process history is unknown. See [measurement log](docs/reports/2026-10-31-rx9060xt.md).

## Repository map

| Path | Purpose |
|---|---|
| `v085/` | **Active:** Reserved VRAM diagnostics and bounded GPU-only soft Holder |
| `v084/` · `v083/` | Reclaim-dependency model and isolated-worker reliability tests |
| `v080/` · `v070/` · `v060/` | GPU benchmarks, shadow/ublk experiments and worker IPC |
| `v050/` · `v040/` · `v030/` · `v020/` · `v012/` | Historical foundations; **some experimental commands are unsafe** |
| `src/` | Archived, optional Vulkan 0.10 probe |
| `legacy/` | Original NBD archive (historical evidence; do not launch) |
| `docs/` | Authoritative current technical status, decisions and safety boundaries |

Version folders are intentionally retained because later versions compile and test code from earlier folders. They are **not** a list of independently production-ready releases.

## Build safely

The **repository-root CMake** defaults to a CPU/mock-only build; no GPU or SWAP access is needed:

```sh
cmake -S . -B build -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^v08[345]_' --output-on-failure
```

Read-only AMDGPU availability check, on an AMD-equipped Linux machine:

```sh
python3 v085/tools/reserved_vram_preflight.py --pool-mib 4096
python3 v085/tools/reserved_vram_observe.py --samples 5 --interval-seconds 1
```

A passing preflight only says a GPU-only experiment has enough apparent headroom; it **does not** allocate or reserve memory. The actual GPU-only Holder is a separate, explicitly acknowledged experiment described in [v085/README.md](v085/README.md). No `sudo`, kernel device or `swapon` belongs in the default verification path.

## Current research questions

1. After exiting the OpenCL Holder, **when and why** do global AMDGPU VRAM counters return toward baseline?
2. Can a driver-supported, **non-evictable** VRAM reservation be implemented without damaging graphics workloads?
3. Can the entire I/O path remain able to make progress during kernel reclaim, with recoverable data following GPU reset?
4. Only after (2) and (3) are demonstrated on recoverable lab hardware may the SWAP gate be reconsidered.

**Development branch:** [`develop/v0.8.5`](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5) · [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10) · [Development PR #14](https://github.com/nopenstudio/nopen-vram-swap/pull/14)

NopenOS is planned as a distinct, Linux-zen-based operating system. Python scripts and the present Linux host tools are *research harnesses*, not dependencies being imposed on a future GNU-free NopenOS userland. First stable software release has not been made.
