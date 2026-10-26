# Research release chronology

**Current experiment: v0.12.0-alpha.2 — bounded, process-local GPU-backed LRU page management.** NopenOS system SWAP remains the strategic target, not an existing product.

| Family | Scope | Status |
|---|---|---|
| v0.9.2 and v0.9.3 | OpenCL + NBD real Linux swap | **UNSAFE:** dead NBD connection, SIGBUS, PID1 panic |
| v0.10.0-alpha.1 | Vulkan host-visible VRAM probe | Historical experiment; no swap |
| v0.10.0-alpha.2 | OpenCL PCIe DMA benchmark | Verified 8 GiB VRAM on RX 9060 XT; no swap |
| v0.12.0-alpha.1 | OpenCL + `userfaultfd` cooperating application | RX 9060 XT: 64 MiB store, 768 page restores PASS |
| **v0.12.0-alpha.2** | **Bounded CPU page slots + LRU / full-page GPU swap simulation** | Mock/CI and sanitizers PASS; real GPU managed test pending |
| Future NopenOS kernel integration | Linux VM swap-out/swap-in with reclaim-safe DMA, GPU reset handling | Design stage only |

Read [the architecture and true-swap acceptance gates](../v012/docs/TRUE_SWAP_ROADMAP_TR.md).

Do not launch historical NBD swap scripts on a machine with important work.