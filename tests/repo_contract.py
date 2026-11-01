#!/usr/bin/env python3
"""Repository layout and safety-contract smoke test. Host read-only."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
def read(name):
    return (root/name).read_text(encoding="utf-8")

readme=read("README.md")
turkish=read("README_TR.md")
cmake=read("CMakeLists.txt")
ci=read(".github/workflows/build.yml")
gpu_ci=read(".github/workflows/v085-reserved-vram-mock.yml")
safety=read("docs/SAFETY.md")
status=read("docs/STATUS.md")
for content in (readme,turkish,status,safety):
    assert "KERNEL_SWAP_BLOCKED" in content
assert "v0.8.5" in readme and "v0.8.5" in turkish
assert "add_subdirectory(v085)" in cmake
assert 'NOPEN_OPENCL "Compile OpenCL backend' in cmake
assert " OFF)" in cmake
assert "NOPEN_BUILD_LEGACY_VULKAN" in cmake
assert "find_package(Vulkan REQUIRED)" in cmake
assert "NOPEN_OPENCL=OFF" in ci
assert "NOPEN_OPENCL=OFF" in gpu_ci
assert "kernel" in safety.lower()
assert (root/"v085/src/nopen_reserved_holder.c").is_file()
assert (root/"docs/reports/2026-10-31-rx9060xt.md").is_file()
active={p.name for p in (root/".github/workflows").glob("*.yml")}
assert active=={"build.yml","v085-reserved-vram-mock.yml"},active
archives=list((root/"docs/ci-history").glob("*.yml"))
assert len(archives)>=20,len(archives)
for target in (
    "docs/ARCHITECTURE.md","docs/SAFETY.md","docs/STATUS.md",
    "docs/DEVELOPMENT.md","docs/RELEASES.md","CHANGELOG.md"):
    assert (root/target).is_file(),target
print("REPOSITORY_CONTRACT_PASS current_docs=YES active_ci=2"
      " historical_ci_archived=YES mock_default=YES kernel_swap=BLOCKED")
