#!/usr/bin/env python3
"""Offline mock fixtures for end-to-end ublk/IPC/GPU trace parsing."""
import importlib.util
from pathlib import Path

script = Path(__file__).resolve().parents[1] / "tools/issue10_analyze_raw_trace.py"
spec = importlib.util.spec_from_file_location("issue10_trace_analyzer", script)
assert spec and spec.loader
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


def block(op: str, off: int, nbytes: int) -> list[str]:
    op_code = {"READ": 0, "WRITE": 1}[op]
    prefix = f"t_ms=7952446 offset={off} bytes={nbytes}"
    return [
        f"NOPEN_DIAG_UBLK phase=BEGIN t_ms=7952446 op={op_code} offset={off} bytes={nbytes} rc=0elapsed_ms=0",
        f"NOPEN_DIAG_IPC phase=BEGIN t_ms=7952446 worker_pid=123 op={op} offset={off} bytes={nbytes} stage=dispatch rc=0 elapsed_ms=0",
        f"NOPEN_DIAG_WORKER phase=BEGIN {prefix} op={op} status=0 elapsed_ms=0",
        f"NOPEN_DIAG_WORKER phase=GPU_ENTER {prefix} op={op} status=0 elapsed_ms=0",
        f"NOPEN_DIAG_WORKER phase=GPU_RETURN {prefix} op={op} status=0 elapsed_ms=0",
        f"NOPEN_DIAG_WORKER phase=RESPONSE_SENT {prefix} op={op} status=0 elapsed_ms=0",
        f"NOPEN_DIAG_IPC phase=END t_ms=7952446 worker_pid=123 op={op} offset={off} bytes={nbytes} stage=complete rc=0 elapsed_ms=0",
        f"NOPEN_DIAG_UBLK phase=END t_ms=7952447 op={op_code} offset={off} bytes={nbytes} rc=0elapsed_ms=1",
    ]


raw = ["NOPEN_VRAM_DIRECT_PREFILL_PASS segments=4"]
for page in (0, 32768, 65535):
    off = page * 4096
    raw += block("WRITE", off, 4096)
    raw += block("READ", off, 4096)
raw += block("READ", 4096, 65536)  # unsolicited metadata probe
valid = m.analyze(raw, full=True)
assert valid["status"] == "FULL_TRACE_PASS", valid
assert valid["ublk_completed"] == 7
assert valid["complete_eight_stage_paths"] == 7
assert valid["operation_counts"]["WRITE"] == 3
assert valid["operation_counts"]["READ"] == 4
assert valid["t_ms_span_observed"] == 1

clipped = m.analyze(raw[27:], full=False)
assert clipped["status"] == "EXCERPT_CLEAN_NOT_COMPLETE", clipped
assert clipped["incomplete_paths"]
assert clipped["operation_counts"]["WRITE"] < 3
assert not clipped["errors"], clipped

fault = raw.copy()
fault[-1] = fault[-1].replace("rc=0elapsed_ms", "rc=-5elapsed_ms")
bad = m.analyze(fault, full=True)
assert bad["status"] == "FAIL"
assert any("error=-5" in line for line in bad["errors"])

partial = m.analyze(raw[:-1], full=True)
assert partial["status"] == "FAIL", partial
assert partial["incomplete_paths"]

mismatch = raw.copy()
mismatch[-1] = mismatch[-1].replace("offset=4096", "offset=8192")
assert m.analyze(mismatch, full=True)["status"] == "FAIL"
print("ISSUE10_TRACE_ANALYZER_TEST_PASS full=YES clipped_not_overclaimed=YES "
      "errors=DETECTED mismatches=DETECTED no_gpu=YES kernel_swap=NO")
