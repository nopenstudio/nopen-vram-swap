#!/usr/bin/env python3
"""Alpha.1 static guard: optional OpenCL profiling, no out-of-order queue."""
from pathlib import Path
source=(Path(__file__).resolve().parents[2] / "v012" / "src" / "nopen_store.c").read_text()
assert 'getenv("NOPEN_OPENCL_PROFILE")' in source
assert '(profile && strcmp(profile,"1")==0)?CL_QUEUE_PROFILING_ENABLE:0' in source
assert "NOPEN_V080_OPENCL_QUEUE profiling=" in source
assert "clCreateCommandQueue(s->ctx,gpu,props,&rc)" in source
# Do not introduce out-of-order transfer without dependency validation.
assert "CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE" not in source
print("V080_PROFILING_POLICY_PASS disabled_by_default=YES explicitly_opt_in=YES in_order=YES")
