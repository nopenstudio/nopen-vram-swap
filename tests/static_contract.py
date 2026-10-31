#!/usr/bin/env python3
"""Historical Vulkan standalone probe safety contract.

The root CMake now defaults to v085 mock tests, so Vulkan is optional.
No GPU, block device or kernel SWAP is touched here.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/"src/nopen_vulkan_probe.c").read_text()
cmake=(root/"CMakeLists.txt").read_text()
for marker in ("vkMapMemory","vkFlushMappedMemoryRanges",
               "VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT",
               "VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT",
               "VK_EXT_MEMORY_BUDGET_EXTENSION_NAME"):
    assert marker in source, marker
assert "Vulkan::Vulkan" in cmake
assert "NOPEN_BUILD_LEGACY_VULKAN" in cmake
assert "swapon" not in source and "NBD_SET_SOCK" not in source
print("LEGACY_VULKAN_STATIC_PASS optional_only=YES kernel_swap=BLOCKED")
