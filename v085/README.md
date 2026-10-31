# Nopen VRAM SWAP v0.8.5 | Reserved VRAM (Phase A)

**Development state: budget/preflight & observation tools implemented.**
**Driver-enforced partitioning, guaranteed VRAM pinning and live kernel SWAP
are NOT implemented.** This revision is deliberately a research milestone,
not a boot-time SWAP service and not a production allocator.

## Goal and architecture

On an AMD Radeon card nominally containing 16 GiB of VRAM, the proposed
**future** architecture separates an early-boot Nopen pool (example 4 GiB)
from an application graphics budget (example ~12 GiB). The accurate feature
requirement is **enforced backing-store residency and forward progress**,
not lying to an application's VRAM memory-capacity query. The kernel driver
must enforce the budget if this is ever to be a hard partition.

A normal OpenCL `clCreateBuffer` allocation is process-isolated and
preloading/prefilling data can touch each segment, but **neither action
guarantees that its backing pages cannot be evicted/migrated to GTT**.
Likewise `amdgpu.vramlimit` is a driver **testing** limit, **not** a hidden
4 GiB reserve accessible to Nopen. v0.8.5 never changes that setting.

### Verified previously

- Nopen's existing GPU OpenCL path allocates multiple segments with
  `clCreateBuffer`, optionally touches them using `clEnqueueFillBuffer`
  and `clFinish`; those calls were present before v0.8.5, including
  earlier 4GiB GPU-only benchmarks.
- Issue #10 raw-only 256MiB ublk session: 165/165 I/O requests, three
  independently checked write/read roundtrips, no physical kernel SWAP.
- v0.8.3 failure injection and v0.8.4 synthetic reclaim wait-loop
  experiments passed locally, but did not demonstrate GPU driver forward
  progress in real reclaim, kernel resilience or recovery after GPU reset.

### Phase A: implemented in v0.8.5

- `tools/reserved_vram_preflight.py`: **read only**. Finds an AMD graphics
  PCI device via sysfs (requires explicit `--pci` if ambiguous), reads
  `mem_info_vram_total`, `mem_info_vram_used`, `mem_info_gtt_used`,
  checks `MemAvailable` and checks active swap devices. Default
  candidate pool is **4096 MiB** with at least **4096 MiB additional VRAM**
  and **1024 MiB host RAM** available after consideration. Existing Nopen
  `ublk` / NBD kernel swap or missing counters BLOCK the probe; regular
  zram remains untouched. The output is a **hypothetical capacity budget**
  rather than an actual GPU reservation.
- `tools/reserved_vram_observe.py`: samples AMDGPU VRAM/GTT and host
  availability deltas as a **read-only observation**. Global counters
  can change because of ANY process, so no delta is automatically
  attributed to Nopen, and none proves GPU residency or page pinning.
  Bails out if ublk/NBD swap is active or readings cannot be trusted.
- Deterministic CI fixture tests for insufficient VRAM/RAM headroom,
  active GPU SWAP refusal, multiple GPU ambiguity, unsupported sizes,
  observer deltas, preserved mock-only v0.8.3 and v0.8.4 regressions.
- No kernel modules, no OpenCL initialization, no `swapon`, no
  `mkswap`, no `swapoff`, no changes to grub/initramfs or game settings.

## Safe commands (read-only; NO sudo or reboot)

From your v0.8.5 source checkout:

```sh
python3 v085/tools/reserved_vram_preflight.py --pool-mib 4096
python3 v085/tools/reserved_vram_observe.py --samples 5 --interval-seconds 1
cmake -S v085 -B build-v085-mock -DNOPEN_OPENCL=OFF
cmake --build build-v085-mock --parallel 2
ctest --test-dir build-v085-mock -R '^v085_' --output-on-failure
```

On a multi-GPU system specify the PCI address with `--pci 0000:03:00.0`
(or its actual value). Use the `--pci-root`, `--meminfo` and
`--swaps-file` flags for mock fixtures only, not to bypass hardware
preflight. The tools never request root. The result may say
`V085_RESERVED_PREFLIGHT_PASS`; this means only **eligible to investigate
userspace GPU-only allocation**, not `4GiB VRAM reserved` or
`kernel swap safe`.

## Next engineering phases (NOT shipped)

B. A bounded user-space-only VRAM allocation/holding prototype with
   explicit opt-in, read-back verification, AMDGPU global counters and
   independently measured game coexistence. Use existing OpenCL code,
   not an unreviewed boot script. Such a pool will still be **soft**,
   potentially migrated, and inaccessible if the worker fails.
C. Investigate an actual AMDGPU kernel/DRM memory-manager-reservation
   integration or supported equivalent, accounting for driver scheduling,
   user virtual memory and boot-time display/multi-GPU initialization.
   Any new kernel interface must be reviewed: ordinary OpenCL cannot
   automatically consume VRAM which has been hidden with vramlimit.
D. Only after **reclaim-independent I/O completion** and **GPU reset page
   recovery** have both been demonstrated in a recoverable separate lab,
   consider enabling kernel SWAP. A reboot can be an orderly deployment
   decision for persistent driver-level reservation; Linux itself does
   NOT require a reboot merely to activate SWAP.

## Source references

- Linux AMDGPU sysfs: https://docs.kernel.org/gpu/amdgpu/driver-misc.html
- AMDGPU `vramlimit` parameter is a testing limit:
  https://dri.freedesktop.org/docs/drm/gpu/amdgpu/module-parameters.html
- Linux ublk block queue and user-space I/O:
  https://docs.kernel.org/block/ublk.html

## Release policy

The gate remains **`KERNEL_SWAP_BLOCKED`** even if the v0.8.5 preflight
and observer CI pass. VRAM migration, kernel reclaim deadlocks, GPU reset
data loss, and the previous 4GiB live GPU swap + Bannerlord host freeze
remain unsolved.

The Python utilities are temporary host diagnostics and CI fixtures,
**not requirements of NopenOS's planned GNU-free operating system**.
No GNU userspace dependency is imposed on a future native allocator.
