# Nopen VRAM-SWAP — v0.12.0-alpha.3

**New milestone:** bounded userfaultfd missing-page handler with fault-recency eviction into discrete AMD OpenCL VRAM. The new `nopen-vram-demand` is a **single-process laboratory**, NOT global Linux swap, a kernel driver, or transparent Bannerlord RAM. The previous NBD swap experiments caused fatal system crashes. The real-swap target for future NopenOS remains conditional on forward-progress and recovery guarantees.

[Alpha.3 guide](v012/README_ALPHA3_TR.md) · [Source](v012/src/nopen_fault_managed.c)

---

# Nopen VRAM-SWAP — v0.12.0-alpha.2

**Research goal: real Linux SWAP on NopenOS. Current milestone: bounded, process-local VRAM memory tier — NOT system SWAP.**

We retired the unsafe OpenCL+NBD global swap path after real GPU load caused system-wide SIGBUS and PID1 panic (2026-10-26). The replacement does not make OpenCL disappear: it treats discrete AMD GPU VRAM as a page store but keeps global Linux memory reclaim and system swap outside the experiment until a kernel-grade forward-progress and GPU-failure model is demonstrated.

## New: managed page memory (v0.12.0-alpha.2)

- ZERO / resident RAM / VRAM page table for 4 KiB logical pages
- Strict, user-controlled resident RAM page limit and LRU victim selection
- GPU page-out/page-in via OpenCL or explicit mock backend; CRC-like 64-bit error-detecting digest on page-in
- Invalid GPU pages are refused; failed page-out retains the last live CPU page
- Process-local `madvise(MADV_DONTNEED)` on evicted staging pages, no NBD, SWAPON, root privileges, or kernel module

Build on Garuda:

```bash
cmake -S v012 -B build-v012-a2 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a2 --parallel
ctest --test-dir build-v012-a2 --output-on-failure
./build-v012-a2/nopen-vram-managed --backend mock --pool-mib 4 --logical-pages 256 --ram-pages 8 --rounds 2
./build-v012-a2/nopen-vram-managed --backend opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 --rounds 3
```

The alpha.1 userfaultfd pager remains as `nopen-vram-lab`, separate from the new bounded LRU manager. Merging them and subsequently designing the NopenOS kernel VM interface are future work. Neither the executable nor the existing libraries extend unmodified Bannerlord's RAM yet.

[Turkish guide](v012/README_TR.md) · [True Linux SWAP architecture gates](v012/docs/TRUE_SWAP_ROADMAP_TR.md) · [Release notes](v012/CHANGELOG.md).

The old Vulkan v0.10 research probe remains available at repository root with its own top-level CMake project. Historical real swap ZIP archives are **not safe to launch**.