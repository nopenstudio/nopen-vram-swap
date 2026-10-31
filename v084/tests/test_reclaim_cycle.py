#!/usr/bin/env python3
"""Prove only the model behavior, never claim to prove real kernel liveness."""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import subprocess
import sys

script = Path(__file__).resolve().parents[1] / "tools/reclaim_cycle.py"
spec = spec_from_file_location("reclaim_cycle", script)
assert spec and spec.loader
mod = module_from_spec(spec)
spec.loader.exec_module(mod)

cycle = mod.evaluate(mod.POTENTIAL_RECLAIM_CYCLE, "cycle")
reserve = mod.evaluate(mod.HYPOTHETICAL_INDEPENDENT_RESERVE, "ideal")
assert cycle["cycle_present_in_model"] is True
assert cycle["cycle_path"][0] == cycle["cycle_path"][-1]
assert "host_allocation" in cycle["cycle_path"]
assert "kernel_reclaim" in cycle["cycle_path"]
assert reserve["cycle_present_in_model"] is False
assert reserve["cycle_path"] == []
for result in (cycle, reserve):
    assert result["safety_gate"] == "KERNEL_SWAP_BLOCKED"
    assert result["real_kernel_deadlock_observed"] is False
    assert result["kernel_swap_authorized"] is False
    assert result["opencl_runtime_reclaim_independence_proven"] is False

# Multiple independent cycles and self-loop must not be missed.
assert mod.cycle_in_wait_graph({"a": ("b",), "b": ("a",),
                                "c": ("d",), "d": ()})
assert mod.cycle_in_wait_graph({"single": ("single",)}) == ["single", "single"]
assert mod.cycle_in_wait_graph({"a": ("b",), "b": (), "z": ()}) == []
p = subprocess.run([sys.executable, str(script), "--scenario", "both"],
                   text=True, capture_output=True, check=False, timeout=5)
assert p.returncode == 0, p.stderr
assert "physical_kernel_swap=BLOCKED" in p.stdout
print("V084_RECLAIM_CYCLE_TEST_PASS cycle=DETECTED"
      " acyclic_hypothesis=DETECTED model_only=YES live_swap=BLOCKED")
