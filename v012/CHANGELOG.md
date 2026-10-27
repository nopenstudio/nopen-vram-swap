# v0.12.0-alpha.5 — GPU memory economics and policy portability

- Shared page checksum, state types and victim policy compiled without external runtime symbols in ISO C11 freestanding mode.
- Linux-specific CPU baseline workload using identical deterministic random operations; staged process RSS telemetry for honest comparison.
- Same GPU UFFD pager as alpha.4, no global SWAP.
- Release and ASan/UBSan local tests passed; CI gate and actual AMD GPU economics comparison required before claiming RAM advantage.
- Future NopenOS is unimplemented. Existing OpenCL program still glibc dependent.

---

# v0.12.0-alpha.3 — bounded userfaultfd demand pager

- New `nopen-vram-demand` integrates page-fault-driven restoration, resident-budget eviction, and discrete AMD OpenCL backing.
- Deterministic single-thread application access contract. Eviction based on **fault recency**, not perfect LRU/dirty tracking.
- Data hash checked before displacing a valid page; write success required before `MADV_DONTNEED`.
- Injected mock read/write/corruption failure exits ONLY the test process with distinct exit codes; no system swap modified.
- GitHub CI mock/UFFD, OpenCL build, sanitizer checks; RX 9060 XT hardware test pending.
- Not a Linux swap solution yet. NopenOS kernel integration and GPU reset recovery remain research tasks.

---

# v0.12.0-alpha.2 — managed process-local VRAM tier

- Added page table with ZERO, CPU-resident, and GPU-backed states.
- Bounded 4KiB resident slot pool with LRU victim selection.
- Full-page writes and explicit page-out/page-in via `nopen_store` OpenCL or mock.
- FNV-1a 64-bit error-detection check on each restored GPU-backed page.
- Read failure/corruption never silently fabricates a zero page or drops a live resident page; write failure leaves the last valid RAM copy intact.
- Explicit trimming calls MADV_DONTNEED on CPU staging pages (the only process affected).
- Managed CLI and unit tests; test assertions always execute in Release builds.
- Kept alpha.1 `userfaultfd` sample independent, slated for integration in alpha.3.
- **Not yet system SWAP, block device, NopenOS kernel module or Bannerlord RAM extension.**
- Originated after real RX 9060 XT alpha.1: 64MiB/768 restores success and prior v0.9 NBD-induced PID1 panic.