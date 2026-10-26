# 0.12.0-alpha.1 (2026-10-26)

- Replaced the research direction: OS-global NBD swap is NOT used by the new prototype.
- OpenCL discrete AMD page store, explicit `mock` for test-only operation, 4KiB get/put.
- Linux UFFD_USER_MODE_ONLY missing-page handler with cooperative store-to-VRAM+MADV_DONTNEED+fault-in from VRAM.
- Full bytewise verification, repeated rounds, bounds checks, corruption detection and injected failures.
- Two independent CI configurations (mock-only, OpenCL compile without GPU).
- NopenOS integration design and explicit release gates. Not a production/system memory manager.