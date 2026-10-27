# Nopen VRAM SWAP v0.5.0-dev — Linux-zen VM-safe architecture gate

**Status: experimental kernel-aware userspace architecture prototype, NOT a kernel module or a Linux swap driver.** The first stable release remains v1.0.

## What v0.4 actually proved

Ryzen 5 7500F / RX 9060 XT, 64MiB, prefetch16:
- 17/17 local tests passed; both baseline and GPU backends verified all 16384 pages.
- CPU baseline: fill 0.097196 s, verification 0.090826 s, peak RSS 67940 KiB.
- OpenCL GPU pager: fill 0.169479 s, verification 1.105549 s, peak RSS 108100 KiB.
- GPU pager **12.17x slower at sequential readback**, whole fill+verify **6.78x slower**, +40160 KiB process RSS (+39.22 MiB); 64 MiB was a NEGATIVE memory-economics result.
- Prior v0.2 4MiB block transfer: >20 GB/s H2D and D2H, but **not** equivalent to a 4KiB random fault or an application's paging path.

Therefore, no 2x/3x speedup claim is justified. v0.4 tests at 256MiB and 1024MiB remain unverified on the actual GPU.

## Architectural boundary v0.5

The dangerous approach is to issue GPU commands synchronously from Linux direct reclaim. A GPU allocation / firmware / driver completion path may itself need pageable RAM and reclaim; the reclaim thread then waits for a GPU operation whose progress depends on reclaim. This can produce deadlock, CPU stalls, or system panic. Existing failed NBD global-swap experiments make this particularly important.

This first v0.5 implementation is a **read-only Linux VM advisor**, not yet kernel VM integration:

- Sensor reads /proc/meminfo (MemTotal, MemAvailable); /proc/pressure/memory (PSI some/full avg10); /proc/swaps (active NBD guard); and discrete AMD /sys/bus/pci/devices/.../mem_info_vram_total/used.
- No sysctl writes, no cgroup mutation, no swapon/swapoff, no NBD, no ioctl, no kernel modules and no root required.
- Freestanding pure C11 policy has ZERO external symbols, independent of GNU and Linux. Runtime observer is POSIX/Linux-specific and therefore NOT GNU-free NopenOS runtime.
- Policy outputs only a recommendation: NORMAL=16 pages, CONSERVATIVE=4 pages, PAUSE=0, REFUSE=0. **These are engineering defaults, not empirically optimized thresholds.** Missing/contradictory sensor readings or NBD swap => REFUSE.
- Fail-safe headroom: >=2GiB (and 20% RAM) available, >=4GiB GPU VRAM spare beyond a small 64MiB experimental reservation, PSI full avg10 below 2%, PSI some avg10 below 10%.
- PSI some >=2% or full >=0.25% => conservative prefetch; PSI some >=10% or full >=2% => pause. The PSI thresholds need validation on the user's real workload.
- The probe cannot protect against all AMDGPU kernel driver bugs. Read-only polling also cannot prove that a GPU memory object is permanently physically resident in VRAM.

## On-device read-only test

~~~bash
cd ~
git clone --single-branch --branch develop/v0.5.0 \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v050
cd ~/nopen-v050
cmake -S v050 -B build-v050 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v050 --parallel
ctest --test-dir build-v050 --output-on-failure
./build-v050/nopen-vm-probe --watch 3 | tee ~/nopen-v050-vm-observation.log
~~~

This command does not touch Bannerlord, kernel paging or swap. It only reads the current state. VM_PROBE_PASS means the sensor successfully read the fields, NOT that system swap works or the policy was adopted by a runtime pager.

The two larger v0.4 economics experiments remain a hard research gate. Do **not** attempt kernel memory integration on the main PC merely because the sensor succeeds.

## Architecture target for a future isolated kernel test

Strict requirements BEFORE any kernel module, reclaim hook or swap hook:
1. A separate physically isolated test machine or otherwise safe containment with independent console, crash logs, watchdog and out-of-band recovery; single-GPU passthrough may not be viable.
2. No GPU command submission or pageable RAM allocation in direct reclaim; strictly bounded, preallocated pinned staging, nonblocking enqueue and clear progress invariants. Review exact AMDGPU lock ordering, buffer eviction/residency and required kernel allocation context.
3. Faults and device reset have a defined data-fate policy; do not destroy the sole authoritative page copy while a GPU command is in-flight. Reserved CPU fallback memory or reliable replica must exist.
4. Proven bounded page-in/out latency including p99/max under memory pressure; DMA bulk bandwidth is not a substitute.
5. No changes to init/PID1 memory, system swap, host VM allocation policy or GPU device binding during initial testing.
6. Verify net host RAM economy at larger v0.4 logical sizes BEFORE risking kernel integration.

Official Linux kernel references:
- PSI: https://docs.kernel.org/accounting/psi.html
- MM allocation contexts / direct reclaim: https://docs.kernel.org/core-api/mm-api.html
- GFP and recursion deadlocks: https://docs.kernel.org/core-api/gfp_mask-from-fs-io.html

**v0.5 first phase means architecture gates and a safe VM telemetry control plane. It does not deliver the promised kernel-side fast path.**
