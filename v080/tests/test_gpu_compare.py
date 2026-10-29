#!/usr/bin/env python3
"""Alpha.2 transfer parser: reject incomplete or misleading GPU reports."""
from pathlib import Path
from tempfile import TemporaryDirectory
import importlib.util

module=Path(__file__).resolve().parents[1]/"tools"/"compare_gpu_paths.py"
spec=importlib.util.spec_from_file_location("gpu_compare",module)
cmp=importlib.util.module_from_spec(spec)
spec.loader.exec_module(cmp)

def mock(mode,ops=128,complete=True,backend="opencl"):
    lines=[f"V080_GPU_FULL_ARENA_VERIFIED backend={backend} mode={mode} "
           "capacity_mib=1024 full_bytes_checked=1073741824 "
           "all_writes_before_reads=YES simultaneous_logical_capacity=YES "
           "host_staging_mib=8 kernel_swap=NO"]
    for i,size in enumerate((4,64,1024,4096)):
        lines.append(f"V080_GPU_BENCH_RESULT backend={backend} mode={mode}"
                     f" capacity_mib=1024 chunk_kib={size} operations={ops}"
                     f" write_mib_s={100*(i+1)} read_mib_s={200*(i+1)}"
                     " write_p50_us=10 write_p95_us=20"
                     " read_p50_us=10 read_p95_us=20"
                     " measured_bytes_per_direction=524288"
                     " all_bytes_verified=YES kernel_swap=NO")
    if complete:
        lines.append(f"V080_GPU_BENCH_PASS backend={backend} mode={mode}"
                     " capacity_mib=1024 full_arena_integrity=YES kernel_swap=NO"
                     " elapsed_seconds=10")
    return "\n".join(lines)

with TemporaryDirectory() as td:
    d=Path(td);a=d/"direct.log";p=d/"pinned.log"
    a.write_text(mock("direct"));p.write_text(mock("pinned"))
    result=cmp.compare(cmp.parse(a,"direct"),cmp.parse(p,"pinned"))
    assert len(result["chunks"])==4
    assert result["kernel_swap"] is False
    assert result["ssd_shadow_included"] is False
    for wrong in (mock("direct",complete=False),
                  mock("direct",backend="mock"),
                  mock("pinned")):
        a.write_text(wrong)
        try:
            cmp.parse(a,"direct")
        except ValueError:
            pass
        else:
            raise AssertionError("accepted mismatched/non-OpenCL/incomplete data")
    a.write_text(mock("direct",ops=127))
    try:
        cmp.compare(cmp.parse(a,"direct"),cmp.parse(p,"pinned"))
    except ValueError:
        pass
    else:
        raise AssertionError("accepted unmatched operation counts")
print("V080_ALPHA2_GPU_COMPARE_TEST_PASS strict_gpu_integrity=YES "
      "paired_workload=YES kernel_swap=NO")
