# Nopen VRAM-SWAP — v0.12.0-alpha.1

**Current research:** isolated user-space GPU-backed memory pages. **Not Linux SWAP or extra system RAM.**

The new primary proof-of-concept lives in [v012/](v012/). It uses a discrete AMD OpenCL GPU as a 4 KiB page store and Linux `userfaultfd(UFFD_USER_MODE_ONLY)` to retrieve pages in a cooperating application. Pages are explicitly evicted using `MADV_DONTNEED`; no kernel-global reclaim path, NBD, `swapon`, privileged service, or kernel module is involved.

**Status:** Source and mock-only tests built; hardware UFFD + OpenCL integration needs testing on Garuda. The former OpenCL+NBD real-swap experiments caused fatal Linux crashes, including PID 1 panic. **Do not re-enable those scripts.** This build does **not** transparently speed up Bannerlord or raise available RAM.

## Build (Garuda Linux)

```bash
cmake -S v012 -B build-v012 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012 --parallel
ctest --test-dir build-v012 --output-on-failure
./build-v012/nopen-vram-lab --probe
./build-v012/nopen-vram-lab --raw --backend opencl --mib 64 --pages 256 --rounds 3
./build-v012/nopen-vram-lab --demo --backend opencl --mib 64 --pages 256 --rounds 3
```

Read the [Turkish testing guide](v012/README_TR.md), [architecture and NopenOS roadmap](v012/docs/ARCHITECTURE_TR.md), and [0.12 changelog](v012/CHANGELOG.md).

Previous Vulkan (v0.10) prototype remains under `src/` with its separate top-level CMake build. Previous NBD source archives remain historical only; no old global swap experiment has been certified safe.
