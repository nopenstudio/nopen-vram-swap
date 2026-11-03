# Nopen VRAM SWAP

**Experimental OpenCL/AMDGPU memory research for Linux and the future NopenOS.**

> **Current implementation:** [`develop/v0.8.5`](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5)  
> **Next engineering target:** **v0.9.0, crash stabilization. NOT RELEASED.**  
> **Physical system swap:** `KERNEL_SWAP_BLOCKED`. Do **not** run historical kernel-SWAP launchers on a daily computer.

[Türkçe](README_TR.md) · **[Critical work orders](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/CRITICAL_WORK_ORDERS_v0.9.md)** · **[Issue #18: master plan](https://github.com/nopenstudio/nopen-vram-swap/issues/18)** · [Current status](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/STATUS.md) · [Safety](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/SAFETY.md)

## Crash Stabilization: Critical Work Orders and Implementation

The project has a **single ordered 10-item P0/P1 plan**: IPC deadline/failure handling, reclaim audit, acknowledged-page integrity design, VRAM residency, reclaim-independent I/O, worker/GPU recovery, concurrency, zram policy, fault injection, and isolated hardware release acceptance.

The [canonical task board](https://github.com/nopenstudio/nopen-vram-swap/issues/18) links each work order to an individual GitHub Issue. **Work order 01 is being developed separately in [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17); it is not a fully proven fix.** Work order 02, reclaim-dependency audit, follows.

**v0.9.0 is a future acceptance target.** No `v090/` release implementation or v0.9.0 release/tag has been created. Existing 4 GiB GPU-backed kernel swap + Bannerlord host freeze and the earlier swap WRITE timeout remain unresolved.

## Where the code lives

| Place | Role |
|---|---|
| [`develop/v0.8.5`](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5) | Actual current OpenCL runtime, ublk/IPC prototypes, tests and documentation |
| [Issue #18](https://github.com/nopenstudio/nopen-vram-swap/issues/18) | One authoritative work-order queue for the v0.9 crash program |
| [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17) | Draft IPC deadline and failure-policy work, separate from current baseline |
| `main` | **Public landing + historical source archive**; original root Vulkan prototype is no longer the selected backend |

Do not build the root of `main` expecting the current OpenCL implementation. To run the safe current CPU/mock tests, use the development branch:

```sh
git clone --branch develop/v0.8.5 https://github.com/nopenstudio/nopen-vram-swap.git
cd nopen-vram-swap
cmake -S . -B build -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^(repository_|v060_isolated_gpu_process_deadline_and_ipc_checksum|v060_issue10_trace_mock_only|v070_fatal_gpu_ipc_forensic_log|v083_|v084_|v085_)' --output-on-failure
```

The command above never activates GPU-backed kernel swap. Historical Vulkan/NBD sources and retired workflows remain accessible for reproducibility, not for current product development.

**Release gate: blocked until all critical work orders and separately recoverable hardware tests pass.**
