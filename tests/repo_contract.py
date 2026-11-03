#!/usr/bin/env python3
"""Repo layout/safety contract for the v0.9 crash-stabilization work orders.
This is a read-only MOCK governance test; it does not exercise kernel SWAP.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
def read(name: str) -> str:
    return (root / name).read_text(encoding="utf-8")

readme = read("README.md")
turkish = read("README_TR.md")
cmake = read("CMakeLists.txt")
ci = read(".github/workflows/build.yml")
safety = read("docs/SAFETY.md")
status = read("docs/STATUS.md")
orders = read("docs/CRITICAL_WORK_ORDERS_v0.9.md")
releases = read("docs/RELEASES.md")
ci_docs = read("docs/CI.md")

for text in (readme, turkish, status, safety, orders):
    assert "KERNEL_SWAP_BLOCKED" in text
    assert "CRITICAL_WORK_ORDERS_v0.9.md" in (readme + turkish + status + safety)
assert "v0.8.5" in readme and "v0.8.5" in turkish
assert "v0.9.0" in orders and "NOT RELEASED" in orders.upper()
assert "v0.9.0" in releases and "NOT RELEASED" in releases
assert "add_subdirectory(v085)" in cmake
assert 'NOPEN_OPENCL "Compile OpenCL backend' in cmake
assert " OFF)" in cmake
assert "NOPEN_BUILD_LEGACY_VULKAN" in cmake
assert "NOPEN_OPENCL=OFF" in ci
assert "vulkan_archive_not_built=YES" in ci
assert "kernel" in safety.lower()
assert (root / "v085/src/nopen_reserved_holder.c").is_file()
assert (root / "docs/reports/2026-10-31-rx9060xt.md").is_file()
assert (root / "docs/ci-history/v085-reserved-vram-mock.yml").is_file()
active = {p.name for p in (root / ".github/workflows").glob("*.yml")}
assert active == {"build.yml"}, active
archives = list((root / "docs/ci-history").glob("*.yml"))
assert len(archives) >= 21, len(archives)
for target in ("docs/ARCHITECTURE.md","docs/SAFETY.md","docs/STATUS.md",
               "docs/DEVELOPMENT.md","docs/RELEASES.md","CHANGELOG.md"):
    assert (root / target).is_file(), target
for n in range(1, 11):
    key = f"İE-{n:02d}"
    assert key in orders, key
    assert f"/issues/{18+n}" in orders, key
assert "#18" in orders and "/issues/18" in orders
assert "only active workflow" in ci_docs.lower()
print("REPOSITORY_CONTRACT_PASS canonical_orders=10 active_ci=1"
      " vulkan_archived=YES historical_ci_archived=YES"
      " mock_default=YES kernel_swap=BLOCKED")
