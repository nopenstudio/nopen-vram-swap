#!/usr/bin/env python3
"""Static safety checks. No GPU access; not a runtime correctness test."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/'src/nopen_vulkan_probe.c').read_text()
cmake=(root/'CMakeLists.txt').read_text()
assert 'vkMapMemory' in src and 'vkFlushMappedMemoryRanges' in src
assert 'VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT' in src and 'VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT' in src
assert 'VK_EXT_MEMORY_BUDGET_EXTENSION_NAME' in src
assert '--mib' in src and '2048' in src
assert 'OpenCL' not in src and 'OpenCL' not in cmake
assert 'swapon' not in src and 'NBD_SET_SOCK' not in src
assert 'Vulkan::Vulkan' in cmake
print('PASS: Vulkan-only alpha.1 static safety contract (NOT a GPU runtime test)')