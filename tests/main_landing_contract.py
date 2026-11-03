#!/usr/bin/env python3
"""Public landing contract for historical main branch, no GPU or host changes."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
en = (root / "README.md").read_text(encoding="utf-8")
tr = (root / "README_TR.md").read_text(encoding="utf-8")
ci = (root / ".github/workflows/build.yml").read_text(encoding="utf-8")

for content in (en, tr):
    assert "KERNEL_SWAP_BLOCKED" in content
    assert "develop/v0.8.5" in content
    assert "v0.9.0" in content
    assert "/issues/18" in content
    assert "/pull/17" in content
    assert "CRITICAL_WORK_ORDERS_v0.9.md" in content
assert "tests/main_landing_contract.py" in ci
assert "apt-get" not in ci and "cmake" not in ci
assert "vulkan_not_built=YES" in ci
assert "branches: [main]" in ci
print("MAIN_LANDING_CONTRACT_PASS public_docs=YES current_OpenCL_link=YES"
      " future_v0.9_target=YES no_vulkan_build=YES kernel_swap=BLOCKED")
