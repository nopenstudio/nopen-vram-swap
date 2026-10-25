# Historical releases (OpenCL era)

OpenCL was removed from the active build in **v0.10.0-alpha.1**. Historical packages are retained **for reproducibility only**; the OpenCL code is not part of the current backend.

| Family | Scope | Historical state |
|---|---|---|
| Alpha / beta 0.2 | Early 128 MiB NBD raw block prototype | raw blocks only |
| beta 0.3 | 8 GiB segmented OpenCL storage | raw blocks only |
| beta 0.4 | Distributed raw block verification | raw blocks only |
| beta 0.5 | First experimental active SWAP | serious SIGBUS and crash |
| beta 0.6 | Crash diagnostics; disabled swap | raw blocks only |
| beta 0.7 | 2 GiB configurable VRAM and canary support | experimental |
| beta 0.8 | Random 4 KiB diagnostics; swap disabled | raw blocks only |
| beta 0.9 | Real SWAP; 1.26 GiB recorded use | system freeze |
| beta 0.9.1 and 0.9.1a | Real page-out; investigated swap cache | swap-in not yet proven |
| beta 0.9.2 | Verified swap-in (~1,473 MiB physical NBD reads) | NBD stall, SIGBUS, desktop crash |
| beta 0.9.3 | NBD nonblocking send and diagnostic phases | Bannerlord world-load freeze reported |
| **v0.10.0-alpha.1** | **Vulkan mapped-memory probe** | **no swap, no NBD** |

**Do not enable historical SWAP scripts on a machine with important work.** Unreliable userspace NBD can invalidate live swap pages, cause system-wide SIGBUS, or lose work.

Historical source archive ZIPs are part of the separately downloadable full project bundle under `legacy/archives/`, where available. Source owners retain responsibility for licensing and redistribution of historical third-party materials.