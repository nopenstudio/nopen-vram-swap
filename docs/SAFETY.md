# Safety and experimental gates

**Default and only approved status on the primary computer: `KERNEL_SWAP_BLOCKED`.**

### Allowed by default

- Compile code with `-DNOPEN_OPENCL=OFF`; run unit/mocks and static audits.
- Read AMDGPU sysfs and `/proc` counters without root.
- On an idle machine and with **explicit acknowledgment**, run bounded **GPU-only** OpenCL integrity tests/Holder according to versioned README. These may still encounter GPU driver bugs; they are not risk-free.

### Not authorized for routine testing

- `mkswap` / `swapon` on GPU, `ublk` or NBD-backed devices.
- Old scripts in `legacy/`, `v070/tools/…` or `v080/tools/v081_direct4g_kernel_lab.sh` which can manipulate block devices or system swap.
- Disabling zram or changing swap priorities to force pages into a GPU-backed swap target.
- Reboot/initramfs/kernel/GRUB edits as a shortcut to reserving GPU memory.
- Claiming that `clCreateBuffer`, `clFinish`, `mlock` of a staging buffer or `amdgpu.vramlimit` guarantees 4 GiB of physically pinned, non-evictable VRAM.

### Why

The historic OpenCL+NBD global swap had I/O stalls and severe system failures. A later 4 GiB `ublk` + live SWAP trial with Bannerlord froze the host. The cause is not established. Under memory pressure, kernel reclaim may require `ublk` I/O completion while the userland GPU service / runtime itself needs kernel memory or scheduler progress. Closing an IPC on timeout does not prove that reclaim can complete. VRAM page loss after GPU reset could make swapped-out pages unrecoverable.

### Required for changing the kernel swap gate

1. Driver-backed, non-evictable VRAM reservation *or* an equivalently proven backing-store policy.
2. A provably reclaim-independent kernel+userspace+GPU I/O path, including allocation failures and watchdog behavior.
3. Durable and tested recovery semantics for GPU reset or loss of the VRAM swap service, so acknowledged swap data is not silently lost.
4. Reproducible pressure/timeout tests performed on separately recoverable hardware or an equivalent isolated rig, with logs stored outside the target.
5. Independent review and explicit documented decision to authorize a new physical test.

Passing normal CTest, a 1 GiB OpenCL readback, or read-only preflight does not satisfy these conditions.

See [status](STATUS.md), [architecture](ARCHITECTURE.md), [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10).
