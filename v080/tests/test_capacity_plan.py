#!/usr/bin/env python3
"""v0.8 size policy: 1 -> 2 -> 4GiB staged, no host swap."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

module = Path(__file__).resolve().parents[1] / "tools" / "capacity_plan.py"
spec = importlib.util.spec_from_file_location("capacity_plan",module)
planner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(planner)

for mib in (1024,2048,4096):
    r=planner.evaluate(mib,16304,2048,8192,2048)
    assert r["feasible_now"], r
    assert r["logical_pages"]==mib*256
    assert r["highest_page_byte_offset"]==mib*1048576-4096
    assert r["known_metadata_estimate_mib"] in (6,12,24)
    assert not r["kernel_swap_activated_by_planner"] and not r["actual_gpu_allocated"]
    assert not r["performance_gain_proven"]
    for case in (
        planner.evaluate(mib,16304,13000,8192,2048),
        planner.evaluate(mib,16304,2048,mib+511,2048),
        planner.evaluate(mib,16304,2048,8192,1023),
    ):
        assert not case["feasible_now"]
    # Pure, deterministic override-mode CLI must not inspect physical GPU.
    run=subprocess.run([
        sys.executable,str(module),"--stage-mib",str(mib),
        "--vram-total-mib","16304","--vram-used-mib","2048",
        "--disk-free-mib","8192","--mem-available-mib","2048"],
        capture_output=True,text=True,timeout=5)
    assert run.returncode==0,(run.stdout,run.stderr)
    assert "V080_PLAN_PASS" in run.stdout
    assert json.loads(run.stdout.split("\nV080_PLAN_PASS")[0])["stage_mib"]==mib
try:
    planner.evaluate(8192,16304,1000,12000,2000)
except ValueError:
    pass
else:
    raise AssertionError("8GiB accepted outside alpha.1 scope")

with tempfile.TemporaryDirectory() as td:
    root=Path(td)
    one=root/"0000:01:00.0";one.mkdir()
    (one/"vendor").write_text("0x1002")
    (one/"class").write_text("0x030000")
    (one/"mem_info_vram_total").write_text(str(16304*1048576))
    (one/"mem_info_vram_used").write_text(str(2*1048576+1))
    assert planner.amd_discrete_vram(root)==(16304,3)
    swaps=root/"swaps"
    swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 1024 0 100\n")
    assert not planner.gpu_kernel_swap_active(swaps)
    swaps.write_text("Filename Type Size Used Priority\n/dev/zram0 partition 1024 0 100\n/dev/ublkb23 partition 65532 65500 200\n")
    assert planner.gpu_kernel_swap_active(swaps)
    another=root/"0000:03:00.0";another.mkdir()
    for name in ("vendor","class","mem_info_vram_total","mem_info_vram_used"):
        (another/name).write_text((one/name).read_text())
    try:planner.amd_discrete_vram(root)
    except ValueError:pass
    else:raise AssertionError("ambiguous AMD GPU silently accepted")
print("V080_CAPACITY_POLICY_PASS staged=1024,2048,4096 strict_headroom=YES "
      "no_global_swap=YES address_64bit=YES")
