# Nopen VRAM-SWAP release history

**Active research: v0.12.0-alpha.4 — deterministic random endurance of process-local GPU-backed UFFD paging.**
**Strategic goal: safe real Linux swap for NopenOS. Not available yet.**

| Version | Experiment | Verified |
|---|---|---|
| v0.9.2–v0.9.3 | OpenCL GPU storage bridged to Linux swap via user-space NBD | **UNSAFE:** NBD stalls, swap-device I/O errors, SIGBUS, PID1 kernel panic |
| v0.10.0-alpha.1 | Vulkan GPU memory mapping probe | Test-stage only, no swap |
| OpenCL benchmark alpha.2 | Full allocation verification on RX 9060 XT | 8 GiB VRAM read/write passed; no swap |
| v0.12.0-alpha.1 | GPU-backed 4 KiB process-local page restoration with `userfaultfd` | Actual AMD GPU alpha.1 PASS |
| v0.12.0-alpha.2 | Bounded resident 4 KiB LRU-managed GPU page store | Actual AMD GPU alpha.2 PASS |
| v0.12.0-alpha.3 | Fault-driven bounded process-local pager | Actual AMD GPU alpha.3 PASS |
| **v0.12.0-alpha.4** | **Random read/write, hot/cold, reproducible seed, tail-latency and full validation** | GitHub mock+UFFD CI, real AMD OpenCL hardware validation pending |

**Do not enable archived NBD-based swap.** See [v0.12 Alpha.4 testing guide](../v012/README_ALPHA4_TR.md) and [NopenOS true swap roadmap](../v012/docs/TRUE_SWAP_ROADMAP_TR.md).
