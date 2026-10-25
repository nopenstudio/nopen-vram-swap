# Vulkan architecture decision — 0.10.0-alpha.1

## Why a new backend?

The historical OpenCL + userspace NBD path confirmed real 1,536 MiB swap-out and approximately 1,473 MiB device reads, but failed under Bannerlord due to NBD connection stalls followed by systemic SIGBUS faults. Moving to Vulkan does not fix NBD's reclaim dependencies automatically.

## Alpha.1 scope

```
Linux process -> Vulkan 1.1 memory types -> DEVICE_LOCAL | HOST_VISIBLE buffer
                                               |-> vkMapMemory -> CPU page writes/reads
```

No `/dev/nbd*` node, no `mkswap`, no `swapon`, no OpenCL runtime, no root privileges required. No claim that DEVICE_LOCAL allocation is pinned to physical GDDR forever; memory types/driver budget can change or migrate backing storage. Discrete GPU selection is mandatory.

## Why HOST_VISIBLE + DEVICE_LOCAL?

May expose PCIe BAR/ReBAR-mapped VRAM for CPU read and write, avoiding an OpenCL GPU transfer queue for each page. Some drivers expose only tiny BAR-visible heaps; some map without `HOST_CACHED`, which makes CPU reads comparatively slow. Vulkan queries alone do not prove a full 2 GiB mappable VRAM allocation is safe.

## Validation gates

1. Detect a discrete GPU and inspect heaps/types.
2. Refuse device-local memory that cannot be mapped by the host.
3. Confirm a buffer-compatible memory type and check driver-provided budget when available.
4. Map a small allocation (default 64 MiB), write patterns to up to 256 4 KiB pages, reread and compare in two passes.
5. Increase allocation only with an explicit `--mib` parameter; 2,048 MiB is the hard experimental maximum.
6. No NBD or swap integration before passing focused GPU tests and addressing userspace-NBD deadlock risk.

## Risks

Mapped PCIe VRAM is not system RAM. Direct CPU reads can be slow, mappings may become invalid on GPU reset, and a successful data round-trip test does not establish reliability as Linux swap. A future swap backend must guarantee forward progress under memory pressure, avoid deadlocks and device loss, and have an explicit failure model. Running a graphical game concurrently is a separate qualification test.

## References

- Khronos Vulkan memory specification: https://docs.vulkan.org/spec/latest/chapters/memory.html
- VK_EXT_memory_budget: https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html