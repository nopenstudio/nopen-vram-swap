# CI organization

Previously, every research version added a permanent GitHub Actions workflow. That produced dozens of overlapping jobs, mostly for superseded development versions.

The active default jobs have been consolidated:

1. `.github/workflows/build.yml`: current root CMake (mock and CPU only) with v085/v084/v083 CTests; a separate optional job builds the independent archived Vulkan prototype.
2. `.github/workflows/v085-reserved-vram-mock.yml`: focused policy, Holder mock integrity, fake AMDGPU sysfs fixtures and historical IPC/reclaim regressions; **OpenCL OFF**.

Previous YAML definitions were moved verbatim to `docs/ci-history/` to preserve what earlier experiments did. This directory is **documentation**, not a GitHub Actions execution directory. Some archived jobs or lab scripts once exercised physical swap paths and must not be run on the main workstation.

Any new automated test must not start an actual OpenCL GPU context, `ublk` kernel device, NBD device, `mkswap`, `swapon`, `swapoff` or alter zram.

When a new development version supersedes v085, update the root CMake and focused workflow in the same PR, rather than adding another permanently active workflow.

[Safety](SAFETY.md) · [Development](DEVELOPMENT.md)
