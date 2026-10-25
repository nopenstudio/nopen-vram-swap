# Archived OpenCL / NBD experiments (unsafe)

These releases reproduce the project's historical OpenCL architecture. They are **not** used by Vulkan v0.10.0-alpha.1.

Several of these programs enabled GPU-backed Linux swap and caused full-session freezes, cascading `SIGBUS` crashes and swap read/write errors under Bannerlord. Do **not** run their `enable-*` scripts on a valuable system. No automated CI or supported installation path is provided for the archived code.

See [historical release index](../docs/RELEASES.md). Integrity hashes are available in `SHA256SUMS`.