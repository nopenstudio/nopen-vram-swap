# Development guide

This project is research code. The quickest safe path requires **no GPU, root, system service or boot modifications**.

## Get the current research code

```sh
git clone --branch develop/v0.8.5 https://github.com/nopenstudio/nopen-vram-swap.git
cd nopen-vram-swap
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^(v083_|v084_|v085_|repository_)' --output-on-failure
python3 v085/tests/test_v085_safety_contract.py
```

Root CMake selects v085 and its inherited regression suite. Root is mock/CPU by default; `NOPEN_OPENCL=ON` means **compilation** enables the backend, it does not launch a GPU context without a user invoking the specific program. The legacy Vulkan probe can be built independently with `-DNOPEN_BUILD_LEGACY_VULKAN=ON` if Vulkan development headers are available. The default CI path never requests GPU or root.

## Read-only hardware observation

```sh
python3 v085/tools/reserved_vram_preflight.py --pool-mib 4096
python3 v085/tools/reserved_vram_observe.py --samples 12 --interval-seconds 5
```

Hardware counters are global and cannot be uniquely attributed to Nopen. These commands do not allocate VRAM or enable swap.

## Optional real GPU-only Holder

**Not a system-swap test.** Close games, keep normal zram, do not run as root:

```sh
cmake -S . -B build-gpu -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-gpu --target nopen-v085-reserved-holder --parallel 2
./build-gpu/v085/nopen-v085-reserved-holder \
  --backend opencl --pool-mib 1024 --seconds 15 \
  --ack I_ACCEPT_1GIB_HOLDER_NO_SWAP
```

The Holder deliberately refuses 4 GiB attempts, unauthorized invocations, insufficient headroom, and active NBD/ublk kernel swap. GPU drivers can hang despite userspace timeout checks; use only for explicit hardware research. After exiting, sample counters again because immediate VRAM accounting did not return to baseline in the first local experiment.

## Code map and contribution rules

- Keep the stable `nopen_store` API and older tests while editing current versions. New module should live in the current version folder; don't move cross-version files without updating every include and test.
- Never normalize safety gates away to make a test pass.
- Separate **mock evidence**, **OpenCL GPU test evidence**, **raw-only ublk evidence**, and **live kernel SWAP evidence** in logs and docs.
- Add a regression test for changes to storage/IPC checksums, failure recovery, or VRAM accounting.
- Prefer C11 for runtime policies and avoid assumed GNU userland dependencies in future NopenOS architecture. Python is permitted for host diagnostic tooling only.

### CI organization

The active workflows are `.github/workflows/build.yml` (mock/CPU root build plus optional standalone Vulkan) and `.github/workflows/v085-reserved-vram-mock.yml` (current v085/84/83 tests). Historical workflows are preserved as **non-executable references** in `docs/ci-history/`. See [CI index](CI.md).

No stable v1.0 release currently exists.
