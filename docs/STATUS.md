# Current research status

**Date:** 2026-10-31 · **Active development:** v0.8.5 Reserved VRAM · **Release:** none

| Gate | Status | Confidence |
|---|---|---|
| Real discrete AMD OpenCL storage | PASS | 1 GiB tagged full-arena readback, RX 9060 XT |
| Real userland 1 GiB Holder | PASS | 15-second open, sampled integrity, full final readback |
| GPU-memory accounting after close | **PARTIAL** | VRAM remained elevated immediately after `nopen_store_close`; next-day counter returned to normal, but release timing unknown |
| Preflight VRAM/GTT/RAM counters | PASS | Read-only PCI `0000:03:00.0` |
| Isolated worker mock reliability | PASS | v0.8.3 test matrix |
| Reclaim circular dependency model | PASS, *hypothetical* | v0.8.4 synthetic graph and mock stall |
| GPU-side non-evictable VRAM guarantee | **NOT IMPLEMENTED** | `clCreateBuffer` / `clFinish` not equivalent to a pin |
| Real kernel SWAP reclaim forward progress | **BLOCKED** | Earlier 4 GiB ublk + Bannerlord full-host freeze unexplained |
| GPU reset / loss of SWAP data recovery | **BLOCKED** | No validated durable fallback |
| Game-visible 12 GiB hard partition | **NOT IMPLEMENTED** | Driver has not been modified |

## Last confirmed hardware test

See [2026-10-31 RX 9060 XT report](reports/2026-10-31-rx9060xt.md). 1 GiB OpenCL readback and Holder PASS, but a PASS here applies to userland storage only. The counter after OpenCL handle close does not establish whether/when memory is released.

## Next session

1. **No new GPU allocation first.** Read-only delayed VRAM/GTT time series after Holder exit; inspect per-process GPU allocations if usage stays high.
2. Keep `KERNEL_SWAP_BLOCKED`. Preserve existing zram.
3. If release behavior is understood, investigate supported AMDGPU DRM/kernel mechanisms for hard, non-evictable VRAM reservations *without using `amdgpu.vramlimit` as a pretend separate 4 GiB pool*.
4. Any live kernel SWAP testing needs separate recoverable lab hardware with external logs; do not repeat the freeze scenario on the primary workstation.

Source of record: [GitHub Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10), [v085 README](../v085/README.md), [experimental PR #14](https://github.com/nopenstudio/nopen-vram-swap/pull/14).

This project does not claim any published v1.0 or production-ready VRAM-backed swap.

## 2026-11-01: read-only client visibility diagnostic

Added `v085/tools/drm_vram_owners.py` to distinguish a system-wide
VRAM count from the subset of **accessible per-client DRM fdinfo**.
This is a research diagnostic, not a pinning interface or a full GPU
allocator inventory; clients may share buffers, and some processes may
be invisible without permissions. It is safe to run after Holder exit
without reboot, OpenCL allocation or kernel SWAP.


## 2026-11-01: delayed read-only GPU accounting observed

A later 12-sample, five-second-interval read-only diagnostic on the user's
RX 9060 XT recorded AMDGPU global VRAM usage at 425.727 MiB initially,
398.570 MiB minimum and 419.723 MiB final; GTT ~60.93–67.04 MiB.
Previously, immediately after the Holder store-close, global usage remained
1,931 MiB. This later reading shows the elevated amount was **not still
accounted for during this later sample**. The observations are many hours
apart; whether the driver reclaimed resources immediately after process exit,
after a delay, after other processes exited, or following a reboot is unknown.
Do not state that GPU-memory release timing is proven.

The `drm_vram_owners.py` tool scanned 317 process directories, skipped
263 unreadable *fdinfo entries*, found six visible DRM clients, primarily
Brave 144.555 MiB and Plasma Shell 107.906 MiB. Those are partial,
potentially shared DRM-client counters rather than the full GPU VRAM tally.
See [Issue #10 follow-up](https://github.com/nopenstudio/nopen-vram-swap/issues/10).
