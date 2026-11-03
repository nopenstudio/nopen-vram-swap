# CI: tek etkin mock güvenlik iş akışı

**Aktif uygulama dalı: `develop/v0.8.5`; hedef: `v0.9.0` çökme stabilizasyonu (henüz release değil).**

## Only active workflow

`.github/workflows/build.yml` is the **only active workflow**. It runs on pull requests and selected branches, with `NOPEN_OPENCL=OFF` and **no root, physical GPU runtime, Vulkan binary, ublk/NBD device, mkswap, swapon, swapoff, reboot or host zram changes**.

One job builds the root mock CMake target and executes the following classes:
- `repository_*` static structure / safety / canonical work orders;
- `v060_isolated_gpu_process_deadline_and_ipc_checksum` and `v060_issue10_trace_mock_only`;
- `v070_fatal_gpu_ipc_forensic_log`;
- `v083_*`, `v084_*`, `v085_*` mock and read-only fixtures;
- `v085/tests/test_v085_safety_contract.py` for current budget/monitoring assertions.

**The archived Vulkan job is deliberately not scheduled.** The historical source and opt-in root CMake option remain for evidence only, not for new development.

## Historical Actions archive

All former version-specific workflows, including `v085-reserved-vram-mock.yml`, are retained as inert references under `docs/ci-history/`. GitHub does not schedule files in that directory. A new work order must extend the single active build, not add a workflow per version or branch.

## Test truthfulness

- `MOCK_ONLY` never establishes real AMDGPU reclaim or physical VRAM pinning.
- OpenCL-only or raw-ublk testing may require separate explicit acceptance, but is never an automatic CI side effect.
- Live kernel-SWAP safety gate remains `KERNEL_SWAP_BLOCKED`.
- Each work order adds CI evidence plus a clear **not-proven** section to its pull request.
- No v0.9.0 release/tag exists merely because mock CI passes.

[Canonical work orders](CRITICAL_WORK_ORDERS_v0.9.md) · [Safety](SAFETY.md) · [Development](DEVELOPMENT.md)
