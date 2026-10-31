# Nopen VRAM SWAP

**Research project exploring AMD GPU VRAM as an auxiliary memory tier for Linux and the future NopenOS.**

> **Current experimental development: [v0.8.5 Reserved VRAM](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5).** There is **no production-ready VRAM SWAP**. The safety decision is **`KERNEL_SWAP_BLOCKED`**.

[Türkçe](README_TR.md) · [Current experimental code](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5) · [Latest status](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/STATUS.md) · [Architecture](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/ARCHITECTURE.md) · [Safety](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/SAFETY.md) · [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10)

## Where to start

**The default `main` branch preserves the independent early Vulkan probe and historical NBD research archive.** Its root CMake is **not** the latest Nopen build and must not be confused with the current version.

For the actively developed v0.8.5 code:

```sh
git clone --branch develop/v0.8.5 https://github.com/nopenstudio/nopen-vram-swap.git
cd nopen-vram-swap
cmake -S . -B build -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^(v083_|v084_|v085_|repository_)' --output-on-failure
```

These commands build only CPU/mock tests, **not kernel SWAP**. For up-to-date technical details, always follow the links above to the development branch. Historical NBD archives, `ublk` swap-lab scripts and kernel SWAP experiments must not be launched on an everyday workstation.

## Results and limits

- **Verified:** RX 9060 XT OpenCL tagged 1 GiB read/write; bounded 15-second v0.8.5 Holder with full integrity checks; read-only AMDGPU VRAM/GTT budget monitoring.
- **Investigating:** VRAM accounting immediately after Holder shutdown. The first observation stayed elevated, and delayed release has not yet been established.
- **Not implemented:** driver-enforced 4 GiB VRAM reservation; application-visible 12 GiB hard limit; reclaim-independent GPU I/O; guaranteed GPU-reset recovery; safe live kernel SWAP.
- **Previously unsafe:** 4 GiB GPU-backed kernel SWAP + Bannerlord froze the host. Earlier NBD swap trials also caused severe system failures.

**No stable v1.0 release has been published.** NopenOS is planned as a distinct Linux-zen-based operating system without a GNU userland dependency; current Python/Linux diagnostics are research tools only.
