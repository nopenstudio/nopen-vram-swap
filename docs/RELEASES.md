# Research version map

The directory names reflect several **historically separate experimental tracks**, not a stable monotonic release history.

| Folder/track | Purpose | Status |
|---|---|---|
| **v085 (v0.8.5)** | Reserved VRAM preflight, GPU accounting and 1 GiB bounded Holder | **Active research; no hard reservation or swap** |
| v084 (v0.8.4) | Hypothetical reclaim dependency cycle and mock stalled requests | Regression tests |
| v083 (v0.8.3) | Worker crash, timeout, malformed IPC, reclaim audit | Regression tests |
| v080 (v0.8.x/v0.8.1) | 1/2/4 GiB GPU-only transfer research, raw ublk diagnostics | Experimental; kernel swap lab unsafe |
| v070 (v0.7.0) | Userspace worker, shadow backend, experimental ublk | Historical; physical swap unqualified |
| v060 (v0.6.0) | IPC isolation, mock fault injection and recovery design | Dependency and tests |
| v050 (v0.5.0) | VM telemetry, policy gate and block metadata | Dependency and tests |
| v040 (v0.4.0) | Page-migration economics | Historical |
| v030 (v0.3.0) | Prefetch research | Historical |
| v020 (v0.2.0) | GPU link and bandwidth measurement | Historical |
| v012 (v0.12.0-alpha.1 ... alpha.5) | **Earlier independent** process-local `userfaultfd` architecture | Historical, not next after v085 |
| Root `src/` (0.10.0) | Independent Vulkan VRAM probe | Archived, opt-in build |
| `legacy/` (NBD 0.9.x) | Old real NBD swap attempts and artifacts | **Unsafe: archive only** |

No production-ready version and no verified live system-wide VRAM SWAP exists. Version numbers on historical experiments do **not** authorize launchers or imply kernel SWAP stability.

[CHANGELOG](../CHANGELOG.md) · [Current status](STATUS.md) · [Safety](SAFETY.md)
