#!/usr/bin/env bash
# IE-04/A1: backward-compatible path, safe policy lives in Python.
# Default MOCK + exactly 256 MiB. No GPU work or CMake before preflight.
# Real OpenCL needs an explicit manual risk approval; CI never supplies it.
# No SSD data path, ublk, NBD, kernel swap or zram modification.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$repo/v080/tools/ie04_a1_runner.py" "$@"
