#!/usr/bin/env python3
"""Regression: game coexistence must not compile or run memory-heavy CTest.

Static control-flow and safety assertions, no GPU allocations or kernel SWAP.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
script = (root / "v080/tools/run_v081_bannerlord.sh").read_text()
baseline = script.split("if [[ \"$mode\" == baseline ]]; then", 1)
assert len(baseline) == 2, "baseline-only condition missing"
body = baseline[1].split("\nelse\n", 1)
assert len(body) == 2, "game branch missing"
game = body[1].split("\nfi\n", 1)
assert len(game) == 2, "game branch missing closing fi"
assert "cmake -S v080" in body[0]
assert "cmake --build build-v081" in body[0]
assert "ctest --test-dir build-v081" in body[0]
for forbidden in ("cmake -S", "cmake --build", "ctest ", "swapon ", "swapoff "):
    assert forbidden not in game[0], f"game path unexpectedly invokes {forbidden}"
assert '-x "$bench"' in game[0] and '-x "$worker"' in game[0]
assert "V081_GAME_REUSE_PREBUILT" in game[0]
assert 'exec python3 v080/tools/bannerlord_v081_probe.py' in game[1]
assert "active kernel ublk/NBD swap" in script
print("V081_GAME_RUNNER_GATE_PASS baseline_tests=YES game_tests=NO "
      "game_compile=NO prebuilt_required=YES kernel_swap=NO")
