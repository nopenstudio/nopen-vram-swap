# Nopen VRAM-SWAP research releases

| Version | Technical milestone | Status |
|---|---|---|
| 0.9.x | OpenCL + NBD active global swap | **UNSAFE:** dead NBD, SIGBUS and PID1 panic |
| 0.10.x | Vulkan and OpenCL VRAM memory probes | VRAM allocation/transfer verified, not swap |
| 0.12.0-alpha.1 | 4KiB OpenCL + userfaultfd process pager | RX 9060 XT verified |
| 0.12.0-alpha.2 | Bounded RAM resident pages with GPU LRU storage | RX 9060 XT verified |
| 0.12.0-alpha.3 | Fault-driven bounded process-local GPU pager | RX 9060 XT verified |
| 0.12.0-alpha.4 | Randomized 4KiB mixed workload, tail latencies and integrity | RX 9060 XT 100,000 ops verified |
| **0.12.0-alpha.5** | **Portable page policy + identical CPU baseline and process-RSS accounting** | Local tests pass; CI and real GPU economics gate |

**NopenOS has not yet been implemented.** Actual Linux VM SWAP, GPU reset recovery and safe kernel reclaim path remain future research goals.

See [alpha.5 Turkish guide](../v012/README_ALPHA5_TR.md). Do not enable historical NBD swap.

