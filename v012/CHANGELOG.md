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