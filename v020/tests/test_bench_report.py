#!/usr/bin/env python3
"""Offline parser tests with synthetic reports: no GPU or Linux privileges."""
import importlib.util
from pathlib import Path
import sys
import unittest

SCRIPT = Path(__file__).resolve().parent.parent / "tools" / "bench_report.py"
spec = importlib.util.spec_from_file_location("nopen_bench_report", SCRIPT)
assert spec is not None and spec.loader is not None
mod = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = mod
spec.loader.exec_module(mod)


def fixture() -> str:
    lines = [
        "MATRIX_START 2026-10-27T18:25:27Z",
        "git_revision=" + "a" * 40,
        "AMD_GPU_PCI address=/sys/bus/pci/devices/0000:03:00.0",
        "current_link_speed=32.0 GT/s PCIe",
        "current_link_width=16",
    ]
    for mode, chunk in sorted(mod.EXPECTED):
        lines.extend([
            f"MATRIX_RUN mode={mode} chunk_kib={chunk}",
            "GPU_SELECTED name=gfx1200 reserved_mib=64 discrete_only=YES",
            ("LINK_RESULT version=0.2.0-dev backend=opencl pool_mib=64 "
             f"chunk_kib={chunk} passes=3 h2d_mib_s=1000.00 "
             "d2h_mib_s=2000.00 process_maxrss_kib=238532 "
             f"bulk_only=YES no_global_swap=YES mode={mode}"),
            "BENCH_PASS backend=opencl verified_bytes=67108864 passes=3 no_global_swap=YES",
        ])
    lines.append("MATRIX_PASS all_opencl_runs_complete=YES")
    return "\n".join(lines) + "\n"


class TestReport(unittest.TestCase):
    def test_complete_valid(self) -> None:
        rows, ceiling = mod.analyze(fixture())
        self.assertEqual(len(rows), 9)
        self.assertAlmostEqual(ceiling, 63.0153846154, places=4)
        self.assertAlmostEqual(rows[0]["h2d_gb_s"], 1.048576, places=5)
        self.assertAlmostEqual(rows[0]["peak_rss_mib"], 238532 / 1024)

    def test_truncated_does_not_count(self) -> None:
        with self.assertRaises(ValueError):
            mod.analyze(fixture().replace("MATRIX_PASS all_opencl_runs_complete=YES", ""))

    def test_failed_or_incomplete_run(self) -> None:
        with self.assertRaises(ValueError):
            mod.analyze(fixture().replace("BENCH_PASS backend=opencl ", "BENCH_FAILED backend=opencl ", 1))

    def test_metadata_mismatch(self) -> None:
        with self.assertRaises(ValueError):
            mod.analyze(fixture().replace("backend=opencl pool_mib=64", "backend=mock pool_mib=64", 1))

    def test_duplicate_rejected(self) -> None:
        data = fixture()
        extra = ("MATRIX_RUN mode=queued chunk_kib=4\n"
                 "LINK_RESULT version=0.2.0-dev backend=opencl pool_mib=64 "
                 "chunk_kib=4 passes=3 h2d_mib_s=10 d2h_mib_s=10 "
                 "process_maxrss_kib=100 no_global_swap=YES mode=queued\n"
                 "BENCH_PASS backend=opencl verified_bytes=67108864 passes=3 no_global_swap=YES\n")
        with self.assertRaises(ValueError):
            mod.analyze(data.replace("MATRIX_PASS ", extra + "MATRIX_PASS "))


if __name__ == "__main__":
    unittest.main()
