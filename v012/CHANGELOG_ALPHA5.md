# v0.12.0-alpha.5 — Process RSS economics + freestanding page policy

- Shared pure C11 page policy (`nopen_page_core`) for alpha.2 and alpha.3–5: page states, victim selection, FNV-1a integrity.
- Added `--baseline --stress` no-GPU/no-UFFD CPU anonymous-page replay of the same seeded mixed read/write workload.
- Added Linux process RSS snapshots at controlled stages and a comparison script that requires both PASS results.
- No system-wide SWAP, NBD, kernel module, auto-start services, kernel sysctls or root changes.
- Fixed release regression in randomized stress test when version string changed.
- Real AMD GPU economics comparison pending; no claim of net RAM savings or GNU-independent executable.