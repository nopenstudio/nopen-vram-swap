#!/usr/bin/env python3
"""AMD VRAM/GTT sysfs probe unit tests; entirely mocked, no hardware required."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

SRC = Path(__file__).resolve().parent.parent / "tools" / "vram_observe.py"
spec = importlib.util.spec_from_file_location("nopen_vram_observe", SRC)
assert spec is not None and spec.loader is not None
lib = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = lib
spec.loader.exec_module(lib)

class TestObserve(unittest.TestCase):
    @staticmethod
    def make_device(root: Path, name: str, vendor: str = "0x1002", klass: str = "0x030000") -> Path:
        dev = root / name
        dev.mkdir()
        (dev / "vendor").write_text(vendor, encoding="ascii")
        (dev / "class").write_text(klass, encoding="ascii")
        for attr, value in {
            "mem_info_vram_used": 128 * 1024 * 1024,
            "mem_info_gtt_used": 32 * 1024 * 1024,
            "mem_info_vram_total": 16 * 1024 * 1024 * 1024,
            "mem_info_gtt_total": 4 * 1024 * 1024 * 1024,
        }.items():
            (dev / attr).write_text(str(value), encoding="ascii")
        return dev

    def test_select_only_real_amd_display(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            good = self.make_device(root, "0000:03:00.0")
            self.make_device(root, "0000:00:01.0", "0x8086")
            self.make_device(root, "0000:04:00.0", "0x1002", "0x040300")
            self.assertEqual(lib.discover_gpu(root), good)
            data = lib.snapshot(good)
            self.assertEqual(data["mem_info_vram_used"], 128 * 1024 * 1024)
            self.assertEqual(data["mem_info_gtt_used"], 32 * 1024 * 1024)
            self.assertEqual(lib.fmt_mib(data["mem_info_vram_used"]), "128.00")

    def test_ambiguous_gpu_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.make_device(root, "0000:03:00.0")
            self.make_device(root, "0000:04:00.0")
            with self.assertRaises(ValueError):
                lib.discover_gpu(root)

    def test_missing_gpu_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                lib.discover_gpu(Path(tmp))

    def test_unavailable_counter_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            device = self.make_device(Path(tmp), "0000:03:00.0")
            (device / "mem_info_vram_used").unlink()
            with self.assertRaises(ValueError):
                lib.snapshot(device)

    def test_negative_counter_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            device = self.make_device(Path(tmp), "0000:03:00.0")
            (device / "mem_info_gtt_used").write_text("-42", encoding="ascii")
            with self.assertRaises(ValueError):
                lib.snapshot(device)

if __name__ == "__main__":
    unittest.main()
