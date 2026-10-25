# Nopen VRAM-SWAP

**0.10.0-alpha.1 · Vulkan VRAM mapping research · not a working Linux swap replacement**

Nopen Studio's research project testing whether dedicated GPU VRAM can serve as a Linux memory backing store. **The active implementation is now Vulkan-only**; OpenCL is not linked, installed, or required by the current CMake project.

## Current version

`nopen-vulkan-probe` selects a discrete Vulkan GPU, prints its memory heaps and types, requires **DEVICE_LOCAL + HOST_VISIBLE** memory, and verifies two rounds of page data via a mapped Vulkan buffer. It does **not** create a block device or enable Linux swap.

> ⚠️ **Experimental:** Previous OpenCL/NBD swap prototypes froze the desktop and caused cascading SIGBUS process crashes. Vulkan cannot by itself fix the Linux NBD reclaim/deadlock problem. Do not enable swap with historical builds.

## Build (Garuda / Arch Linux)

```bash
sudo pacman -S --needed cmake gcc vulkan-headers vulkan-icd-loader vulkan-tools vulkan-radeon
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/nopen-vulkan-probe --list
./build/nopen-vulkan-probe --mib 64 --samples 256
```

The default is a **64 MiB**, non-privileged probe. After inspecting the selected heap and available Vulkan budget, optionally try `--mib 256`, then `--mib 2048` at your own risk. Do **not** attempt a 2 GiB allocation with a game running.

## Structure

- `src/`: Vulkan-only implementation, no OpenCL symbols
- `docs/ARCHITECTURE.md`: Vulkan BAR / memory mapping decision and limits
- `docs/RELEASES.md`: chronological OpenCL research history and crash warnings
- `.github/workflows/build.yml`: header/loader installation and CI build, GPU-independent `--help` smoke test
- `legacy/archives/`: original experimental packages bundled in the downloadable source bundle (not executed or compiled)

## Roadmap

- `0.10.0-alpha.1`: probe memory types and perform CPU-mapped VRAM verification (**this release**)
- `0.10.0-alpha.2`: repeated random 4 KiB I/O, validation and latency profiling
- `0.10.0-beta.1`: optional NBD raw blocks *only after testing*
- later beta: safe real swap experiments, if a forward-progress model can be demonstrated
- `1.0.0`: reserved for reliability-qualified release

## Licensing

New Vulkan implementation: MIT, see [LICENSE](LICENSE). Historical archive material should be reviewed separately before redistribution.

[Turkish README](README_TR.md) · [Architecture](docs/ARCHITECTURE.md) · [Releases](docs/RELEASES.md)