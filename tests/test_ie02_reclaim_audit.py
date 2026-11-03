#!/usr/bin/env python3
"""IE-02: source audit integrity and hypothetical-cycle regression.

No GPU, root privileges, kernel devices, synthetic memory pressure, or swap.
"""
from __future__ import annotations
import importlib.util
from pathlib import Path

root = Path(__file__).resolve().parents[1]
target = root / "tools/ie02_reclaim_audit.py"
spec = importlib.util.spec_from_file_location("ie02", target)
assert spec is not None and spec.loader is not None
ie02 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ie02)

def source_map() -> dict[str, str]:
    paths = {entry[1] for entry in ie02.EVIDENCE}
    paths.add("v080/tools/v081_direct4g_kernel_lab.sh")
    return {p: (root / p).read_text(encoding="utf-8") for p in paths}

actual = ie02.audit(root)
assert actual["schema"] == "NOPEN_IE02_RECLAIM_DEPENDENCY_AUDIT_V1"
assert actual["source_contract_intact"], actual["missing_or_reordered"]
assert len(actual["evidence"]) >= 30
assert all(e["line"] is not None and e["line"] > 0 for e in actual["evidence"])
assert all(actual["ordering_checks"].values())
assert actual["ipc_admission_mode"] in (
    "legacy_unbounded_mutex", "bounded_trylock_policy"
)
assert actual["gate"] == "KERNEL_SWAP_BLOCKED"
assert actual["observed_root_cause_of_issue_10"] == "UNKNOWN"
assert actual["unverified_kernel_or_driver_edges"]
assert actual["reclaim_independence_proven"] is False
assert actual["data_recovery_proven"] is False
assert actual["physical_gpu_test_run"] is False
assert actual["kernel_swap_test_run"] is False
assert actual["host_changed"] is False
assert actual["release_version"] == "NOT_RELEASED"

scenarios = {x["name"]: x for x in actual["model_scenarios"]}
assert len(scenarios) == 3
for key in ("gpu_only_conditional_host_allocation",
            "shadow_conditional_filesystem_reclaim"):
    case = scenarios[key]
    assert case["cycle_path"]
    assert case["cycle_path"][0] == case["cycle_path"][-1]
    assert case["conditional_edges"]
    assert case["kernel_stacks_captured"] is False
    assert case["real_forward_progress_proven"] is False

ideal = scenarios["idealized_nonallocating_service_not_implemented"]
assert ideal["cycle_path"] == []
assert ideal["real_forward_progress_proven"] is False

texts = source_map()
# If a critical lock disappears or source is silently refactored,
# the audit must FAIL rather than continue passing stale conclusions.
changed = dict(texts)
ipc_path = "v060/src/nopen_isolated_io.c"
admission_marker = (
    "pthread_mutex_lock(&r->lock);"
    if actual["ipc_admission_mode"] == "legacy_unbounded_mutex"
    else "const int admitted=admit(r);"
)
changed[ipc_path] = changed[ipc_path].replace(
    admission_marker, "/* deliberately removed admission */", 1
)
broken = ie02.audit_texts(changed)
assert broken["source_contract_intact"] is False
assert "ipc_admission_precedes_request_deadline" in broken["missing_or_reordered"]

# A proper IE-01 style trylock/admission policy should be recognized
# without falsely certifying kernel reclaim progress.
if actual["ipc_admission_mode"] == "legacy_unbounded_mutex":
    changed = dict(texts)
    ipc_path = "v060/src/nopen_isolated_io.c"
    changed[ipc_path] = changed[ipc_path].replace(
        "pthread_mutex_lock(&r->lock);",
        "const int admitted=admit(r);\n    if(admitted)return admitted;",
        1
    )
    changed[ipc_path] += ("\n/* source-only IE-01 fixture */\n"
                          "pthread_mutex_trylock(&r->lock);\n"
                          "deadline=start+r->admission_timeout_ms;\n")
    bounded = ie02.audit_texts(changed)
    assert bounded["source_contract_intact"], bounded["missing_or_reordered"]
    assert bounded["ipc_admission_mode"] == "bounded_trylock_policy"
assert actual["reclaim_independence_proven"] is False

# Source mutation: a READY sent early is NOT acceptable.
path = "v060/src/nopen_isolated_worker.c"
changed = dict(texts)
line = next(ln for ln in changed[path].splitlines()
            if "NOPEN_IPC_READY,0,0," in ln)
changed[path] = line + "\n" + changed[path].replace(line + "\n", "", 1)
order_bad = ie02.audit_texts(changed)
assert order_bad["source_contract_intact"] is False
assert "worker_staging_precedes_ready" in order_bad["missing_or_reordered"]

# Source mutation: moving OpenCL init before mlock is a regression.
path = "v050/src/nopen_blk_backend.c"
changed = dict(texts)
a = changed[path].find("if(mlock(b->meta,")
b = changed[path].find("if(nopen_store_open(&b->store")
assert 0 <= a < b
changed[path] = changed[path][:a] + "nopen_store_open(&b->store" + (
    changed[path][a:b]
) + changed[path][b:].replace("nopen_store_open(&b->store", "/* moved */", 1)
order_bad = ie02.audit_texts(changed)
assert "gpu_metadata_mlock_precedes_store_open" in order_bad["missing_or_reordered"] or "BLOCK_OPEN_GPU_AFTER_MLOCK" in order_bad["missing_or_reordered"]

# The current development baseline still contains a reachable legacy 4GiB
# launcher. This is flagged, never exercised by this regression suite.
changed = dict(texts)
launcher = "v080/tools/v081_direct4g_kernel_lab.sh"
if actual["live_direct4g_launcher_present_on_baseline"]:
    changed[launcher] = changed[launcher].replace(
        "start) :;;", "start) echo BLOCKED; exit 77;;"
    )
    safe_policy = ie02.audit_texts(changed)
    assert safe_policy["live_direct4g_launcher_present_on_baseline"] is False
else:
    changed[launcher] += "\nstart) :;;\n"
    historical_policy = ie02.audit_texts(changed)
    assert historical_policy["live_direct4g_launcher_present_on_baseline"] is True
assert actual["gate"] == "KERNEL_SWAP_BLOCKED"

# Strict read-only contract: source introspection and static graph only.
audit_source = target.read_text(encoding="utf-8")
for forbidden in ("subprocess.run(", "subprocess.Popen(",
                  "os.system(", "os.exec", "os.fork(",
                  "import pyopencl", "import ctypes", "os.kill("):
    assert forbidden not in audit_source, forbidden

print("IE02_SOURCE_AUDIT_TEST_PASS anchored_sources=YES"
      " model_cycles_conditional=YES stale_source_detected=YES"
      " ordering_regression=YES no_gpu=YES no_ublk=YES"
      " kernel_swap=BLOCKED")
