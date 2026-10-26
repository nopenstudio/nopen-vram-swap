# Nopen Memory Fabric v0.12.0-alpha.2

**Ultimate goal: real Linux SWAP on NopenOS. Current status: explicit cooperating-process memory tier; NOT kernel swap.**

New `nopen-vram-managed` demonstrates a bounded physical RAM page pool backed by discrete AMD GPU OpenCL buffers. Its 4KiB logical pages have ZERO / RESIDENT / GPU states and an LRU eviction policy. Full-page writes avoid a redundant GPU read. Reads validate 64-bit checksums before evicting live RAM pages. GPU read/write faults fail closed, without allowing NBD/SWAPON/PID1 interactions.

Build: `cmake -S v012 -B build-v012-a2 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON && cmake --build build-v012-a2 --parallel && ctest --test-dir build-v012-a2 --output-on-failure`

Mock CLI: `./build-v012-a2/nopen-vram-managed --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 --rounds 2`

GPU CLI (only after tests): `./build-v012-a2/nopen-vram-managed --backend opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 --rounds 3`

The previous `nopen-vram-lab --demo` userfaultfd app demo remains available but is NOT yet wired to the new LRU manager. No transparent swapping of unmodified applications; that needs a separate, NopenOS-kernel-aware VM integration with preallocated reclaim-safe I/O and recoverable GPU failure semantics.

[Turkish guide](README_TR.md) · [True swap roadmap](docs/TRUE_SWAP_ROADMAP_TR.md)