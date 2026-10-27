> **In development:** [v0.2.0 GPU transfer laboratory](v020/README_TR.md) — Alpha.5 is retained as the final architecture alpha. Bulk OpenCL + contiguous page transfer development does not yet constitute global SWAP or a proven speedup.

---

# Nopen VRAM-SWAP — v0.12.0-alpha.5

**New milestone:** standalone freestanding-friendly ISO C11 page policy and honest process RSS economics. Compare an identical deterministic CPU anonymous-RAM workload against the OpenCL+userfaultfd application-level pager.

**AMD RX 9060 XT alpha.4 evidence:** 100,000 random 4KiB operations, 1,024/1,024 pages final verification, p99=120.71µs, 78,753 page-outs / 77,761 page-ins. **Alpha.5 OpenCL economics awaits actual GPU measurements.**

**NOT global Linux SWAP, NOT a kernel module, NOT GNU-independent end-to-end, and NOT an implemented NopenOS.** Legacy OpenCL+NBD global swap caused critical system failures; do not restart it.

[Alpha.5 Turkish guide](v012/README_ALPHA5_TR.md) · [Portability and future swap](v012/docs/PORTABILITY_AND_SWAP_TR.md)

---

# Nopen VRAM-SWAP — v0.12.0-alpha.4

**Research goal:** real, safe AMD GPU VRAM-backed Linux swap for future NopenOS.
**Current status:** deterministic random endurance of a *process-local* OpenCL + `userfaultfd` pager, NOT system SWAP.

The previously tested OpenCL+NBD *global* swap path suffered unrecoverable I/O stalls, cascading SIGBUS errors, and PID 1 kernel panic. Do **not** restart the archived NBD swap services. Neither `/dev/nbd15` nor `swapon` is used in the active v0.12 laboratory.

### Verified on RX 9060 XT

| Release | Actual hardware observations |
|---|---|
| Alpha.1 | AMD OpenCL 64 MiB + userfaultfd 768 page restorations |
| Alpha.2 | 1,024 logical pages / 32 resident pages; 6,144 evictions, 3,072 page-ins; verified |
| Alpha.3 | 512 logical pages / 16 resident pages; 2,976 evictions, 2,480 page-ins; verified |
| Alpha.4 | Random access endurance, sampled operation latency percentiles, full final page verification; **AMD hardware test pending** |

### Build and test (Garuda Linux)

```bash
cmake -S v012 -B build-v012 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012 --parallel
ctest --test-dir build-v012 --output-on-failure
./build-v012/nopen-vram-demand --probe
# Ensure swapon --show contains no NBD device before running:
./build-v012/nopen-vram-demand \
  --backend opencl --pool-mib 64 --logical-pages 512 --ram-pages 16 \
  --stress --operations 10000 --seed 20261027 \
  --hot-pages 32 --hot-percent 80 --write-percent 30
```

The alpha.4 stress mode caps GPU allocations at 256 MiB; start with 64 MiB and without running games. A `STRESS_PASS` proves checksums/data equality for the explicit application arena only; it does **not** extend other processes' RAM, guarantee GPU reset recovery, or imply kernel/global swap safety.

[Turkish Alpha.4 guide](v012/README_ALPHA4_TR.md) · [Real Linux SWAP roadmap](v012/docs/TRUE_SWAP_ROADMAP_TR.md) · [History](docs/RELEASES.md)

The historical Vulkan-only probe remains in `src/`; `cmake -S v012` is the current Nopen Memory Fabric build.
