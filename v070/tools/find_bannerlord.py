#!/usr/bin/env python3
"""Identify Bannerlord under Proton, including large Wine Starter hosts.

Some Proton/modified Bannerlord setups execute the game inside a process
whose Linux comm is merely "Starter" while argv[0] remains the TaleWorlds
launcher path. Never infer the game from the launcher path alone: require
Starter + 512MiB+ resident memory, and prefer a true game executable.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import sys

GAME_EXE = re.compile(r"(?i)^bannerlord(?:[._-]native)?(?:\.exe)?$")
TRUNCATED_GAME_COMM = re.compile(r"(?i)^bannerlord\.nativ$")
NOT_GAME = re.compile(r"(?i)(launcher|steam|proton|wineserver|reaper|pressure-vessel)")
STARTER_MIN_RSS_KIB = 512 * 1024

def executable_basename(raw: str) -> str:
    return raw.strip().replace("\\", "/").rsplit("/", 1)[-1]

def is_bannerlord_starter(comm: str, argv0: str, rss_kib: int) -> bool:
    """Conservative fallback for the actual 'Starter' seen using ~2.5GiB RSS."""
    normalized = argv0.replace("\\", "/").casefold()
    return (comm.casefold() == "starter"
            and rss_kib >= STARTER_MIN_RSS_KIB
            and executable_basename(argv0).casefold()
                == "taleworlds.mountandblade.launcher.exe"
            and "mount & blade ii bannerlord" in normalized
            and "/bin/win64_shipping_client/" in normalized)

def entries(root: Path = Path("/proc")) -> list[tuple[int, int, str]]:
    explicit = []
    starter_fallback = []
    try:
        folders = list(root.iterdir())
    except OSError as exc:
        raise ValueError(f"cannot list process table: {exc}") from exc
    for folder in folders:
        if not folder.name.isdecimal():
            continue
        try:
            pid = int(folder.name)
            comm = (folder / "comm").read_text(
                encoding="utf-8", errors="replace").strip()
            cmd_raw = (folder / "cmdline").read_bytes()
            argv0 = (cmd_raw.split(b"\0")[0].decode(
                "utf-8", errors="replace") if cmd_raw else "")
            name = executable_basename(argv0)
            status = (folder / "status").read_text(
                encoding="utf-8", errors="replace")
            if any(line.startswith("State:") and "Z" in line.split()[1:2]
                   for line in status.splitlines()):
                continue
            rss = 0
            for line in status.splitlines():
                if line.startswith("VmRSS:"):
                    rss = int(line.split()[1])
                    break
            is_explicit = bool(
                (GAME_EXE.fullmatch(name) or GAME_EXE.fullmatch(comm)
                 or TRUNCATED_GAME_COMM.fullmatch(comm))
                and not (NOT_GAME.search(name) or NOT_GAME.search(comm)))
            if is_explicit:
                explicit.append((pid, rss, name or comm))
            elif is_bannerlord_starter(comm, argv0, rss):
                starter_fallback.append((pid, rss, "Starter (Bannerlord host)"))
        except (OSError, ValueError, IndexError):
            continue
    # An actual Bannerlord.exe is always stronger evidence than the fallback.
    result = explicit if explicit else starter_fallback
    return sorted(result, key=lambda item: (-item[1], item[0]))

def find_game_pid(root: Path = Path("/proc")) -> int:
    found = entries(root)
    if not found:
        raise ValueError(
            "no Bannerlord game process found; Wine 'Starter' host checked, "
            "launcher-only process deliberately excluded")
    if len(found) > 1 and found[0][1] == found[1][1]:
        raise ValueError(
            "multiple equal-RSS game candidates; specify --pid explicitly")
    return found[0][0]

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pid-only", action="store_true")
    ns = ap.parse_args()
    try:
        pid = find_game_pid()
        name = next(item[2] for item in entries() if item[0] == pid)
        if ns.pid_only:
            print(pid)
        else:
            basis = ("high_rss_starter_fallback"
                     if name.startswith("Starter (") else "game_executable")
            print(f"BANNERLORD_PID_FOUND pid={pid} source={basis} "
                  f"process={name!r} excludes_small_launcher=YES")
        return 0
    except ValueError as exc:
        print(f"BANNERLORD_PID_NOT_FOUND reason={exc}", file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
