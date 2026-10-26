# Nopen VRAM-SWAP v0.12.0-alpha.1 — isolated GPU page lab

This is a **non-system-swap** research prototype for a cooperative userspace page store. Linux `userfaultfd(UFFD_USER_MODE_ONLY)` resolves 4KiB missing faults from OpenCL GPU buffers; the caller must explicitly quiesce all readers/writers before eviction. Nothing touches `swapon`, `/dev/nbd`, PID 1, existing RAM policy, or a kernel module.

**It is NOT extra RAM for arbitrary applications.** A game like Bannerlord cannot benefit until an explicit allocator integration or a separate, reliability-proven kernel architecture exists. Historical OpenCL NBD swap experiments caused full-system kernel panic; never reactivate those to test this build.

Build and run (AMD discrete GPU/OpenCL headers/ICD required for the real backend):

```bash
cmake -S v012 -B build-v012 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012 --parallel
ctest --test-dir build-v012 --output-on-failure
./build-v012/nopen-vram-lab --probe
./build-v012/nopen-vram-lab --raw --backend opencl --mib 64 --pages 256 --rounds 3
./build-v012/nopen-vram-lab --demo --backend opencl --mib 64 --pages 256 --rounds 3
```

For mock-only CI `-DNOPEN_OPENCL=OFF`, and `--backend mock` explicitly. `--demo` requires kernel support for unprivileged user-only UFFD; the program never changes sysctls or privileges. Also read [Turkish guide](README_TR.md), [Architecture](docs/ARCHITECTURE_TR.md).