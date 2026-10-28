#!/usr/bin/env bash
# Reproducible build of Nopen v0.7 real Linux ublk adapter and ctl tool.
# Compiles only. Does not load kernel modules, create devices or activate swap.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-$repo/build-v070}"
[[ "$build" = /* ]] || build="$repo/$build"
mkdir -p "$build"
cmake -S "$repo/v070" -B "$build" -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build "$build" --parallel
vendor="$build/ublksrv-upstream"
if [[ ! -d "$vendor/.git" ]]; then
  git clone --filter=blob:none https://github.com/ublk-org/ublksrv.git "$vendor"
fi
git -C "$vendor" checkout 6d0d922614ec18e6f2b5d476af8d540a1d9c1de8
(
  cd "$vendor"
  autoreconf -fi
  ./configure --without-libnfs --without-libiscsi --without-gnutls
  make -C lib -j2
  make -j2 ublk
)
g++ -std=c++20 -Wall -Wextra -Wpedantic \
  -I"$vendor/include" -I"$vendor/targets/include" \
  -I"$repo/v070/include" -I"$repo/v060/include" -I"$repo/v050/include" -I"$repo/v012/include" \
  -I"$vendor" \
  "$vendor/targets/ublksrv_tgt.cpp" \
  "$repo/v070/experimental/ublk/nopen_ublk_swap_target.cpp" \
  "$build/libnopen_shadow.a" \
  "$build/v060/libnopen_isolated_io.a" \
  "$build/v060/libnopen_recovery.a" \
  "$build/v060/v050/libnopen_blk_backend.a" \
  "$build/v060/v050/v040/v030/v012/libnopen_vram_store.a" \
  "$build/v060/v050/v040/v030/v012/libnopen_page_core.a" \
  "$vendor/lib/.libs/libublksrv.a" \
  -luring -lOpenCL -lpthread -ldl \
  -o "$build/nopen-v070-ublk"
test -x "$build/nopen-v070-ublk"
test -x "$build/v060/nopen-isolated-worker"
test -x "$vendor/ublk"
printf 'V070_BUILD_PASS target=%s controller=%s worker=%s swap_activated=NO\n' \
  "$build/nopen-v070-ublk" "$vendor/ublk" "$build/v060/nopen-isolated-worker"
