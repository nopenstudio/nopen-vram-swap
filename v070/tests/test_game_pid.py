#!/usr/bin/env python3
"""Synthetic /proc fixtures including Steam + launcher + Wine game executable."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from find_bannerlord import find_game_pid, entries, executable_basename

def fake(root, pid, comm, argv0, rss, state="S"):
    d=root/str(pid); d.mkdir()
    (d/"comm").write_text(comm+"\n")
    (d/"cmdline").write_bytes(argv0.encode()+b"\0--foo\0")
    (d/"status").write_text(f"Name:\t{comm}\nState:\t{state} (sleeping)\nVmRSS:\t{rss} kB\n")

with TemporaryDirectory() as td:
    root=Path(td)
    fake(root, 102, "reaper", "/Steam/SteamLaunch", 80000)
    fake(root, 103, "TaleWorlds.Mount", "C:\\SteamLibrary\\steamapps\\common\\Mount & Blade II Bannerlord\\bin\\TaleWorlds.MountAndBlade.Launcher.exe", 95000)
    fake(root, 104, "Bannerlord.exe", "Z:\\Steam\\Bannerlord\\Bannerlord.exe", 124000)
    fake(root, 105, "Bannerlord.exe", "Z:\\Steam\\Bannerlord\\Bannerlord.exe", 500, "Z")
    fake(root, 106, "wineserver", "/usr/bin/wineserver", 220000)
    assert executable_basename("C:\\Game\\Bannerlord.exe")=="Bannerlord.exe"
    assert find_game_pid(root)==104
    assert len(entries(root))==1
    # Wine often reports just 15 chars of the true Bannerlord.Native.exe.
    fake(root, 107, "Bannerlord.Nativ", "/usr/bin/wine64-preloader", 256000)
    assert find_game_pid(root)==107
    assert len(entries(root))==2
    (root/"107"/"comm").write_text("Launcher.Native\n")
    (root/"104"/"comm").write_text("TaleWorlds.Launch\n")
    (root/"104"/"cmdline").write_bytes(b"C:\\Game\\TaleWorlds.MountAndBlade.Launcher.exe\0")
    try:
        find_game_pid(root)
    except ValueError as exc:
        assert "no real Bannerlord" in str(exc)
    else:
        raise AssertionError("accepted launcher process as active game")

# User's actual Proton setup reports Starter, with TaleWorlds launcher argv and
# ~2.5GiB RSS. Do not confuse the small, separate TaleWorlds launcher with it.
with TemporaryDirectory() as td:
    root=Path(td)
    launcher=r"S:\steamapps\common\Mount & Blade II Bannerlord\bin\Win64_Shipping_Client\TaleWorlds.MountAndBlade.Launcher.exe"
    fake(root,21347,"Starter",launcher,2625492)
    fake(root,21348,"Starter",launcher,43000)
    fake(root,19641,"TaleWorlds.Mount",launcher,105000)
    fake(root,21612,"Coop.CrashRepor",
         r"S:\steamapps\workshop\content\261550\3770450698\Coop.CrashReporter.exe",
         65000)
    assert find_game_pid(root)==21347
    assert len(entries(root))==1
    # A launcher with the same argv but 100MiB RSS is not proof of game.
    (root/"21347"/"status").write_text(
        "State:\tS (sleeping)\nVmRSS:\t110000 kB\n")
    try:
        find_game_pid(root)
    except ValueError:
        pass
    else:
        raise AssertionError("classified low-RSS launcher as game")
    # High-RSS generic Starter unrelated to the game should be ignored.
    (root/"21347"/"status").write_text(
        "State:\tS (sleeping)\nVmRSS:\t2625492 kB\n")
    (root/"21347"/"cmdline").write_bytes(
        b"Z:\\OtherGame\\Starter.exe\0")
    try:
        find_game_pid(root)
    except ValueError:
        pass
    else:
        raise AssertionError("classified unrelated Starter as Bannerlord")

print("BANNERLORD_PID_TEST_PASS real_proton_game_selected=YES "
      "truncated_comm_supported=YES starter_host_supported=YES "
      "low_rss_launcher_excluded=YES zombie_excluded=YES")

