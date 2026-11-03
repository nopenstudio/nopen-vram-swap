# Archived GitHub Actions workflows

These YAML definitions are **historical evidence**, preserved from earlier experimental branches and the former v0.8.5 dedicated mock workflow. They are no longer under `.github/workflows/`, so **GitHub Actions will not schedule them**.

The single active, safe `NOPEN_OPENCL=OFF` workflow is [`.github/workflows/build.yml`](../../.github/workflows/build.yml), documented in [CI.md](../CI.md).

**Legacy Vulkan and historical NBD/ublk swap test workflows are archived, not part of the planned v0.9.0 OpenCL stabilization path.** Some historical experiments can freeze or damage unsaved work on a physical computer. Do not reproduce them on a primary workstation. The safety gate is `KERNEL_SWAP_BLOCKED`.

See [critical work orders](../CRITICAL_WORK_ORDERS_v0.9.md) and [safety policy](../SAFETY.md).
