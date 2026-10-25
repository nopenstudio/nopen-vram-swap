# Changelog

## 0.10.0-alpha.1 — 2026-10-25

- Removed OpenCL entirely from the active build.
- Introduced Vulkan 1.1 discrete-GPU memory-type probe.
- Added `DEVICE_LOCAL | HOST_VISIBLE` strict selection without system-RAM fallback.
- Added buffer-compatible heap and estimated budget checks.
- Added mapped 4 KiB page pattern verification, two rounds, two-passes.
- Added explicit 1–2,048 MiB bounds and optional GPU selection.
- Deliberately removed NBD and SWAP activation from this alpha to prevent accidental system-wide crashes.
- Added CI, bilingual docs, historical release index, and preserved historical source packages in the full downloadable bundle.

### Verification status

GPU runtime validation **pending real Radeon RX 9060 test**. This environment has no Vulkan development headers or compatible GPU; Vulkan source must be built on a machine with Vulkan SDK. No swap stability claim.