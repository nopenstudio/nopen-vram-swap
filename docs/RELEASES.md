# Research release chronology

**Current active experiment: v0.12.0-alpha.1 — Nopen Memory Fabric Lab.** Cooperative application-level GPU-backed 4 KiB pages using user-only `userfaultfd` and OpenCL. **No NBD, no Linux SWAP, no game-transparent RAM extension.**

Historical Vulkan v0.10 probe remains in `src/`. Earlier OpenCL+NBD system swap experiments are explicitly **unsafe** after the 2026-10-26 Bannerlord/PID1 crash.

| Family | Scope | Status |
|---|---|---|
| v0.9.2 and v0.9.3 | OpenCL + NBD real Linux swap | **UNSAFE**: NBD stalls, SIGBUS, PID1 panic |
| v0.10.0-alpha.1 | Vulkan mapped VRAM probe | Archived research; no swap |
| v0.10.0-alpha.2 | OpenCL PCIe DMA benchmark on experimental branch | 8 GiB full verify on RX 9060 XT, no swap |
| **v0.12.0-alpha.1** | **OpenCL GPU store + cooperative UFFD page lab** | **Mock/unit build passed; Garuda GPU+UFFD integration unverified** |

See [new architecture](../v012/docs/ARCHITECTURE_TR.md) and [version notes](../v012/CHANGELOG.md). The older `legacy/archives/` packages are kept for historical research only. Do not enable old swap services.
