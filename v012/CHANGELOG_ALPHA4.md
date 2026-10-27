# v0.12.0-alpha.4 — Randomized demand-paging endurance

- Preserved the isolated AMD OpenCL backing store and bounded 4KiB `userfaultfd` pager.
- Added deterministic hot/cold random read and write access workload with per-page versioned data and exhaustive final verification.
- Added sampled end-to-end operation latency p50/p95/p99, actual observed maximum, throughput duration, process peak RSS and periodic progress.
- Added strict CLI argument bounds and independently seeded CI workloads, alongside original simulated read, write and corruption failure isolation.
- **No** Linux `swapon`, NBD, kernel module, global reclaim, automatic Bannerlord memory extensions or reset-safe GPU paging.
- Source/CI evidence must be distinguished from real AMD OpenCL host testing.