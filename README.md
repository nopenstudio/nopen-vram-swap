# Nopen VRAM-SWAP

**Experimental GPU VRAM-backed block storage for Linux**

Developed by **Nopen Studio**

**Version:** 0.4.0 Beta  
**Status:** Experimental Development  
**Platform:** Linux  
**Language:** C  
**GPU Backend:** OpenCL  
**Block Device Backend:** Linux NBD

---

## Overview

Nopen VRAM-SWAP is an experimental Linux project exploring the possibility of using dedicated GPU video memory (VRAM) as a storage layer for system swap.

Modern graphics cards often contain large amounts of dedicated high-bandwidth memory. While this memory is primarily intended for graphics and compute workloads, it can also be accessed through GPU computing APIs.

Nopen VRAM-SWAP investigates whether this memory can be exposed to the Linux operating system as a virtual block device.

The project uses AMD OpenCL for GPU memory management and the Linux Network Block Device (NBD) subsystem for block-level access.

**The long-term objective is to develop a reliable GPU-backed swap solution for Linux.**

At present, the project provides an experimental VRAM-backed block device. Reliable swap operation has not yet been demonstrated.

## Architecture

```text
+----------------------------------+
|           Linux System           |
|                                  |
|        Physical System RAM       |
|                                  |
|         Linux Swap Layer         |
+----------------------------------+
                 |
                 v
+----------------------------------+
|       Linux NBD Subsystem        |
|                                  |
|           /dev/nbd15             |
+----------------------------------+
                 |
                 v
+----------------------------------+
|         Nopen VRAM-NBD           |
|                                  |
|       Userspace C Backend        |
|                                  |
|     Block Request Processing     |
+----------------------------------+
                 |
                 v
+----------------------------------+
|           AMD OpenCL             |
|                                  |
|       GPU Memory Allocation      |
|       GPU Memory Transfers       |
+----------------------------------+
                 |
                 v
+----------------------------------+
|             GPU VRAM             |
|                                  |
|       Experimental Storage       |
|                                  |
|             8 GiB                |
+----------------------------------+
```

The swap layer shown above represents the intended future integration. Current testing is limited to raw block-device operations.

## Current Features

- AMD GPU detection through OpenCL
- Dedicated GPU memory allocation
- Segmented VRAM allocation
- Configurable virtual block-device capacity
- Linux NBD integration
- GPU-backed block read/write operations
- Experimental memory locking
- Multi-region data integrity testing
- SHA-256 verification
- VRAM release on normal shutdown
- Command-line interface

## Hardware Testing

Development testing has been performed using:

| Component | Hardware |
|---|---|
| GPU | AMD Radeon RX 9060 XT |
| GPU Memory | 16 GB GDDR6 |
| GPU Architecture | gfx1200 |
| CPU | AMD Ryzen 5 7500F |
| Operating System | Garuda Linux |
| Graphics API | OpenCL |
| Block Device | Linux NBD |

### Verified Results

**Beta 0.2**

Successfully allocated 128 MiB of GPU memory and verified 16 MiB of block-device data.

**Beta 0.3**

Successfully allocated 8 GiB of GPU memory using 32 OpenCL segments.

Verified data integrity at selected offsets, including segment boundaries and the end of the virtual device.

**Beta 0.4**

Successfully performed distributed block-device testing across an 8 GiB virtual device.

Test results:

- 64 independent regions
- 4 MiB per region
- 256 MiB total verified data
- Successful read/write verification
- No detected data corruption during the test

These results demonstrate basic GPU-backed block-device functionality.

They do not establish full-device integrity, sustained performance, or swap reliability.

## Development Roadmap

### Beta 0.4 — GPU Block Storage
**Completed**

- OpenCL memory allocation
- NBD integration
- Segmented GPU memory
- 8 GiB virtual device
- Distributed data verification

### Beta 0.5 — Reliability Improvements
**Planned**

- Improved memory locking
- Reduced memory allocation during I/O
- Error handling improvements
- Controlled shutdown behavior
- GPU failure handling

### Beta 0.6 — Experimental Swap
**Planned**

- Small-capacity swap testing
- Swap priority experiments
- Memory pressure testing
- Deadlock investigation

### Beta 0.7 — Performance
**Planned**

- Random I/O benchmarks
- Sequential I/O benchmarks
- PCIe transfer analysis
- Latency measurements
- Comparison with NVMe swap and ZRAM

### Beta 0.8 — Integration
**Planned**

- Linux service integration
- Configuration support
- Device lifecycle management
- Safer initialization and shutdown

### Version 1.0
**Long-term Goal**

A documented and tested GPU-backed swap solution suitable for broader experimental use.

A stable release depends on successful reliability and recovery testing.

## Important Limitations

GPU VRAM is not equivalent to conventional system RAM.

Accessing GPU memory through OpenCL introduces additional software and transfer overhead.

Performance depends on GPU architecture, PCIe bandwidth, driver behavior, and workload characteristics.

The current implementation also depends on a userspace process and GPU drivers remaining operational.

Under severe memory pressure, these dependencies may create deadlocks or system instability.

GPU resets may cause the loss of data stored in VRAM.

**Do not use the current version as production swap storage.**

## Safety Warning

This software is experimental.

Possible consequences include:

- System freezes
- GPU driver resets
- Application crashes
- Data loss
- Filesystem corruption if misused
- Kernel I/O errors

Do not run destructive block-device tests against devices containing important data.

Do not enable swap on the experimental device without understanding the risks.

Back up important files before testing.

## Project Philosophy

Nopen VRAM-SWAP explores unconventional approaches to Linux memory management.

The goal is not to replace physical RAM or claim that GPU memory is universally faster than SSD storage.

Instead, the project investigates whether unused dedicated GPU memory can provide an additional storage resource under controlled conditions.

Research, experimentation, reproducibility, and transparent reporting are central to the project.

## Contributing

Contributions, bug reports, hardware compatibility tests, and technical discussions are welcome.

Areas of particular interest include:

- AMD OpenCL compatibility
- Linux memory management
- NBD reliability
- GPU memory allocation
- Swap deadlock prevention
- PCIe performance
- Kernel and driver interactions

## Development

**Nopen Studio**

Independent software development and experimental systems research.

## License

License to be selected before the first public source release.

---

**Nopen VRAM-SWAP — Exploring the boundaries between GPU memory and Linux storage.**
