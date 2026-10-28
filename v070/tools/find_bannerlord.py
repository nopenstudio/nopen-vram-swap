#!/usr/bin/env python3
"""Locate a live Bannerlord GAME process, never Steam, Proton or TW launcher.

Use /proc/<pid>/comm or argv[0]; not arbitrary cmdline substrings. Launcher
argv contains 'Bannerlord' in its path and must NOT be counted as the game.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import sys

GAME_EXE = re.compile(r"(?i)^bannerlord(?:[._-]native)?(?:\.exe)?$")
# Linux task comm is limited to TASK_COMM_LEN - 1 = 15 bytes. Wine often
# exposes Bannerlord.Native.exe in /proc/PID/comm as "Bannerlord.Nativ".
TRUNCATED_GAME_COMM = re.compile(r"(?i)^bannerlord\.nativ$")
NOT_GAME = re.compile(r"(?i)(launcher|steam|proton|wineserver|reaper|pressure-vessel)")

def executable_basename(raw: str) -> str:
    return raw.strip().replace("\\", "/").rsplit("/", 1)[-1]

def entries(root: Path = Path("/proc")) -> list[tuple[int, int, str]]:
    candidates = []
    try:
        folders = list(root.iterdir())
    except OSError as e:
        raise ValueError(f"cannot list process table: {e}") from e
    for folder in folders:
        if not folder.name.isdecimal():
            continue
        try:
            pid = int(folder.name)
            comm = (folder / "comm").read_text(encoding="utf-8", errors="replace").strip()
            cmd_raw = (folder / "cmdline").read_bytes()
            argv0 = cmd_raw.split(b"\0")[0].decode("utf-8", errors="replace") if cmd_raw else ""
            name = executable_basename(argv0)
            if not (GAME_EXE.fullmatch(name) or GAME_EXE.fullmatch(comm)
                    or TRUNCATED_GAME_COMM.fullmatch(comm)):
                continue
            if NOT_GAME.search(name) or NOT_GAME.search(comm):
                continue
            status = (folder / "status").read_text(encoding="utf-8", errors="replace")
            if any(line.startswith("State:") and "Z" in line.split()[1:2]
                   for line in status.splitlines()):
                continue
            rss = 0
            for line in status.splitlines():
                if line.startswith("VmRSS:"):
                    rss = int(line.split()[1])
                    break
            candidates.append((pid, rss, name or comm))
        except (OSError, ValueError, IndexError):
            continue
    return sorted(candidates, key=lambda item: (-item[1], item[0]))

def find_game_pid(root: Path=Path("/proc")) -> int:
    found = entries(root)
    if not found:
        raise ValueError("no real Bannerlord game executable found; launch the game, not just the launcher")
    # Multiple matching game processes is ambiguous; do not silently sample
    # a helper/proton launcher if it looks equally plausible.
    if len(found)>1 and found[0][1] == found[1][1]:
        raise ValueError("multiple candidate game PIDs with equal RSS; specify --pid explicitly")
    return found[0][0]

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pid-only", action="store_true")
    ns = ap.parse_args()
    try:
        pid=find_game_pid()
        if ns.pid_only:
            print(pid)
        else:
            print(f"BANNERLORD_PID_FOUND pid={pid} source=real_game_executable excludes_launcher=YES")
        return 0
    except ValueError as e:
        print(f"BANNERLORD_PID_NOT_FOUND reason={e}", file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
