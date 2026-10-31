#!/usr/bin/env python3
"""Unit guard: source audit must remain read-only and never approve live SWAP."""
import importlib.util
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
script = root / "v083/tools/reclaim_audit.py"
spec = importlib.util.spec_from_file_location("v083_reclaim_audit", script)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
src = {k:(root / p).read_text(encoding="utf-8")
       for k,p in mod.SOURCES.items()}
real = mod.audit_texts(src)
assert real["code_evidence_intact"], real
assert real["gate"] == "KERNEL_SWAP_BLOCKED"
assert real["physical_kernel_swap_authorized"] is False
assert real["reclaim_forward_progress_proven"] is False
assert len(real["blocking_risks"]) >= 4
assert all(b["severity"] == "BLOCKER" for b in real["blocking_risks"])

# Removing a crucial source assertion MUST make the audit fail.
mutated = dict(src)
mutated["worker"] = mutated["worker"].replace(
    "runtime/driver allocations are NOT mlocked", "UNPROVEN")
broken = mod.audit_texts(mutated)
assert not broken["code_evidence_intact"]
assert "OpenCL_runtime_not_mlocked" in broken["missing_required_evidence"]
assert broken["gate"] == "KERNEL_SWAP_BLOCKED"
assert broken["physical_kernel_swap_authorized"] is False

p = subprocess.run([sys.executable, str(script), "--ci"],
                   text=True, capture_output=True, check=False, timeout=5)
assert p.returncode == 0, p.stderr
assert "kernel_swap_gate=BLOCKED" in p.stdout
assert "physical_swap=NO" in p.stdout
# Guard against accidental introduction of privileged host-mutating commands.
source = script.read_text(encoding="utf-8")
for cmd in ("os.system(", "subprocess.Popen(", "subprocess.run(", "swapon(",
            "swapoff(", "modprobe ", "/dev/ublkb23", "mkswap "):
    assert cmd not in source, cmd
print("V083_RECLAIM_AUDIT_TEST_PASS source_invariants=YES "
      "missing_evidence_rejected=YES swap_gate=BLOCKED kernel_swap=NO")
