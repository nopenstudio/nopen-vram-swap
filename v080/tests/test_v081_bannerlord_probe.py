#!/usr/bin/env python3
"""Offline v0.8.1 process detection and VRAM safety gates, no real processes used."""
import importlib.util
from pathlib import Path
import tempfile

tool = Path(__file__).resolve().parents[1] / "tools" / "bannerlord_v081_probe.py"
spec = importlib.util.spec_from_file_location("nopen_v081_probe", tool)
assert spec and spec.loader
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

with tempfile.TemporaryDirectory(prefix="nopen-v081-fixture-") as root:
    r = Path(root)
    proc = r / "proc"
    proc.mkdir()
    m.PROC = proc
    (proc / "swaps").write_text("Filename Type Size Used Priority\n/dev/zram0 partition 123 0 100\n")
    assert not m.active_gpu_swap()
    (proc / "swaps").write_text("Filename Type Size Used Priority\n/dev/ublkb23 partition 123 0 100\n")
    assert m.active_gpu_swap()
    (proc / "swaps").write_text("Filename Type Size Used Priority\n/dev/zram0 partition 123 0 100\n")
    (proc / "meminfo").write_text("MemAvailable: 5242880 kB\n")
    (proc / "100").mkdir()
    (proc / "100" / "comm").write_text("SteamLaunch\n")
    (proc / "100" / "cmdline").write_bytes(b"SteamLaunch\0Bannerlord.Native.exe\0")
    (proc / "100" / "status").write_text("VmRSS: 8000000 kB\n")
    assert m.bannerlord_process() is None, "Steam wrapper falsely recognized as game"
    (proc / "101").mkdir()
    (proc / "101" / "comm").write_text("Bannerlord.Native\n")
    (proc / "101" / "cmdline").write_bytes(b"Bannerlord.Native.exe\0")
    (proc / "101" / "status").write_text("VmRSS: 1048576 kB\n")
    game = m.bannerlord_process()
    assert game and game["pid"] == 101 and game["rss_mib"] == 1024
    (proc / "102").mkdir()
    (proc / "102" / "comm").write_text("Bannerlord.Launcher\n")
    (proc / "102" / "cmdline").write_bytes(b"Bannerlord.Launcher.exe\0")
    (proc / "102" / "status").write_text("VmRSS: 2200000 kB\n")
    assert m.bannerlord_process()["pid"] == 101, "launcher falsely recognized as game"

    # Regression for the real user's Proton setup: 2.5GiB Bannerlord
    # runs inside "Starter" with TaleWorlds launcher as argv[0].
    # Explicit game exe must win when present. The tiny launcher alone
    # must not count as a running game.
    (proc / "103").mkdir()
    (proc / "103" / "comm").write_text("Starter\n")
    (proc / "103" / "cmdline").write_bytes(
        b"S:\\steamapps\\common\\Mount & Blade II Bannerlord\\bin\\"
        b"Win64_Shipping_Client\\TaleWorlds.MountAndBlade.Launcher.exe\0")
    (proc / "103" / "status").write_text("VmRSS: 2625492 kB\n")
    assert m.bannerlord_process()["pid"] == 101, "explicit exe not preferred"
    (proc / "101" / "comm").write_text("NotGame\n")
    (proc / "101" / "cmdline").write_bytes(b"/usr/bin/notgame\0")
    starter = m.bannerlord_process()
    assert starter and starter["pid"] == 103 and starter["rss_mib"] > 2000, (
        "Proton Starter game host was not recognized")
    (proc / "103" / "status").write_text("VmRSS: 43000 kB\n")
    assert m.bannerlord_process() is None, "small launcher falsely detected"
    (proc / "101" / "comm").write_text("Bannerlord.Native\n")
    (proc / "101" / "cmdline").write_bytes(b"Bannerlord.Native.exe\0")

    gpu = r / "gpu"
    m.GPU_DIR = gpu
    d = gpu / "0000:03:00.0"
    d.mkdir(parents=True)
    (d / "vendor").write_text("0x1002\n")
    (d / "class").write_text("0x030000\n")
    (d / "mem_info_vram_total").write_text(str(16304 * m.MIB))
    (d / "mem_info_vram_used").write_text(str(3000 * m.MIB))
    result = m.check_budget(4096)
    assert result["total_mib"] == 16304
    assert result["headroom_after_pool_mib"] == 9208
    (d / "mem_info_vram_used").write_text(str(10500 * m.MIB))
    try:
        m.check_budget(4096)
        raise AssertionError("insufficient GPU VRAM not rejected")
    except RuntimeError as e:
        assert "headroom" in str(e)
    (d / "mem_info_vram_used").write_text(str(3000 * m.MIB))
    (proc / "meminfo").write_text("MemAvailable: 900000 kB\n")
    try:
        m.check_budget(256)
        raise AssertionError("RAM pressure not rejected")
    except RuntimeError as e:
        assert "MemAvailable" in str(e)

print("V081_BANNERLORD_GATES_PASS steam_launcher_excluded=YES"
      " true_game_detected=YES gpu_headroom=GUARDED"
      " ram_headroom=GUARDED kernel_swap=NO")
