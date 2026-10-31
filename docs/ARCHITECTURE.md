# Architecture | 2026-10-31

## Scope and boundaries

Nopen explores GPU memory as a *storage tier* for memory pages, eventually as an option for NopenOS. The application-level OpenCL allocator, host RAM staging, userspace IPC, kernel `ublk` and Linux VM reclaim are **distinct layers**. None is automatically safe merely because another works.

```text
                    READ-ONLY OBSERVATION (v085)
 /sys AMDGPU VRAM / GTT, /proc/meminfo, /proc/swaps
                          |
                          v
                capacity/health decision
                          |
                     [NOT A PIN]
                          |
     GPU-ONLY RESEARCH (no kernel swap)
  v085 Holder -> nopen_store -> OpenCL ctx/queue
                                      |
                              VRAM buffer objects
                 full tagged write -> verify -> hold -> verify

   PROHIBITED FOR NORMAL PC: LIVE SWAP PATH (historical)
  Linux reclaim -> ublk -> userspace server -> IPC -> GPU worker
    ^                                   OpenCL/driver may need RAM
    +--- possible circular reclaim dependency <-----------------+
```

## Current modules

- `v012/include/nopen_store.h` + `v012/src/nopen_store.c`: reusable contiguous page-store API; mock or discrete AMD OpenCL backend. `clCreateBuffer` and blocking transfer queue. OpenCL pages are **not proven physically pinned**.
- `v050/src/nopen_blk_backend.c`: per-page state, integrity metadata, staging; some buffers mlocked only in experimental physical lab configuration.
- `v060/src/nopen_isolated_io.c` and `v060/src/nopen_isolated_worker.c`: bounded IPC, isolated worker and mock fault injection. Userspace timeout ≠ guaranteed kernel forward progress.
- `v070/experimental/ublk/`: historical kernel-facing experimental target; never enable by default.
- `v083/`, `v084/`: closed-gate recovery audit and hypothetical dependency-cycle experiments.
- `v085/`: current read-only AMDGPU budget/telemetry and bounded 1 GiB userland Holder.

## Reservation is a layered problem

1. **Ownership isolation**: a GPU allocation is an object associated with Nopen's process/context. Games should not directly read/write arbitrary Nopen buffer objects.
2. **Residency**: the GPU memory manager may migrate/evict backing memory to system-managed pools such as GTT under pressure. Prefilling is not a residency guarantee.
3. **Hard reservation**: must be enforceable by a supported driver/kernel mechanism. Faking smaller VRAM capacity or setting `amdgpu.vramlimit` alone does not preserve a separately available 4 GiB pool for Nopen.
4. **Reclaim independence**: every participant needed for swap-in must run without waiting on pages that are themselves blocked waiting for swap-in.
5. **Data survival**: kernel swap cannot silently lose the only copy of anonymous pages on GPU reset or process death. Closing IPC on failure alone is insufficient.

None of (2–5) is proven for live kernel SWAP. Budget numbers are arithmetic only, e.g. AMDGPU reported 16,304 MiB VRAM, minus hypothetical 4,096 MiB -> 12,208 MiB, **not a graphics-device capacity reported to applications**.

## Build/deployment boundaries

Root CMake builds mock/CPU tests by default; current version CMake chains older modules because of source dependencies. The standalone historical Vulkan experiment is opt-in. No GUI, systemd service, boot changes, initramfs hook, kernel patch, real `swapon`, or third-party plugin is part of the default build.

Current Linux/Python diagnostics are host tests only. A future NopenOS userland must not depend on GNU assumptions merely because these prototypes run on Garuda Linux.

## Historic branch/numbering note

`v012` refers to `v0.12.0-alpha`, an **older** architecture track, not the chronological successor of v0.8.5. `v020` through `v085` are the newer development track. Original 0.10 Vulkan and 0.9.x NBD archives are independent/historic. See [release index](RELEASES.md).
