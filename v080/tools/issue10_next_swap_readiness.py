#!/usr/bin/env python3
"""Issue #10: read-only gate before ANY further experimental VRAM kernel swap.
This program NEVER starts ublk, activates SWAP, modifies sysctl or writes files.
It only reports readiness for separate diagnostic preparation. NO GO on physical swap.
"""
from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

MIB = 1024 * 1024
ROOT = Path(__file__).resolve().parents[2]
EXPECTED_BRANCH = "diagnostics/issue-10-io-trace"


def command(*args: str) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, timeout=5, check=False
        )
        return proc.returncode, proc.stdout.strip()
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError):
        return 127, ""


def numeric(path: Path) -> int:
    s = path.read_text(encoding="ascii").strip()
    if not s.isdecimal():
        raise ValueError("invalid numeric sysfs value")
    return int(s)


def gpu_snapshot() -> dict:
    cards = []
    for d in Path("/sys/bus/pci/devices").iterdir():
        try:
            if (d / "vendor").read_text().strip().lower() != "0x1002":
                continue
            if not (d / "class").read_text().strip().lower().startswith("0x03"):
                continue
            total = numeric(d / "mem_info_vram_total") // MIB
            used = (numeric(d / "mem_info_vram_used") + MIB - 1) // MIB
            cards.append({"pci": d.name, "total_mib": total, "used_mib": used})
        except (OSError, ValueError):
            continue
    if len(cards) != 1:
        raise ValueError(f"expected one AMD GPU with VRAM counters, found {len(cards)}")
    card = cards[0]
    card["remaining_after_4g_mib"] = card["total_mib"] - card["used_mib"] - 4096
    card["min_headroom_mib"] = max(4096, (card["total_mib"] + 3) // 4)
    return card


def assess() -> dict:
    failures, notes = [], []
    try:
        swaps_raw = Path("/proc/swaps").read_text().splitlines()
        if not swaps_raw or not swaps_raw[0].startswith("Filename"):
            raise ValueError("invalid /proc/swaps")
        swaps = []
        for row in swaps_raw[1:]:
            cells = row.split()
            if len(cells) < 5:
                raise ValueError("malformed /proc/swaps row")
            swaps.append({"name": cells[0], "used_kib": int(cells[3]),
                          "priority": int(cells[4])})
    except (OSError, ValueError) as exc:
        failures.append(f"SWAP_SNAPSHOT_FAILED: {exc}")
        swaps = []
    names = [x["name"] for x in swaps]
    if "/dev/zram0" not in names:
        failures.append("ZRAM_BASELINE_MISSING")
    if any(s.startswith(("/dev/ublkb", "/dev/nbd")) for s in names):
        failures.append("KERNEL_GPU_NBD_SWAP_STILL_ACTIVE")
    other = [name for name in names if name != "/dev/zram0"]
    if other:
        failures.append("EXTRA_SWAP_DEVICE_ACTIVE")
    if Path("/dev/ublkb23").exists() or Path("/dev/ublkc23").exists():
        failures.append("OLD_UBLK_DEVICE_PRESENT")
    services = {}
    for unit in ("nopen-v070-ublk.service",
                 "nopen-v081-direct4g-monitor.service"):
        rc, out = command("systemctl", "is-active", unit)
        services[unit] = out or ("unknown" if rc == 127 else "inactive")
        if rc == 127:
            failures.append(f"SERVICE_STATE_UNAVAILABLE: {unit}")
        elif out not in ("inactive", "failed", "unknown"):
            failures.append(f"SERVICE_NOT_INACTIVE: {unit}={out}")
    rc, branch = command("git", "-C", str(ROOT),
                         "rev-parse", "--abbrev-ref", "HEAD")
    if rc != 0 or branch != EXPECTED_BRANCH:
        failures.append(f"WRONG_DIAGNOSTIC_BRANCH: {branch or 'unavailable'}")
    rc, revision = command("git", "-C", str(ROOT), "rev-parse", "HEAD")
    if rc or not revision:
        failures.append("GIT_REVISION_UNKNOWN")
    rc, dirty = command("git", "-C", str(ROOT), "status", "--porcelain")
    if rc:
        failures.append("GIT_STATUS_UNAVAILABLE")
    elif dirty:
        failures.append("WORKTREE_DIRTY_REVIEW_BEFORE_TEST")
    try:
        card = gpu_snapshot()
        if card["remaining_after_4g_mib"] < card["min_headroom_mib"]:
            failures.append("INSUFFICIENT_GPU_HEADROOM")
    except (OSError, ValueError) as exc:
        card = {"error": str(exc)}
        failures.append("GPU_BASELINE_UNAVAILABLE")
    try:
        available = next(int(row.split()[1]) // 1024
                         for row in Path("/proc/meminfo").read_text().splitlines()
                         if row.startswith("MemAvailable:"))
        if available < 2048:
            failures.append("MEMAVAILABLE_UNDER_2GIB")
    except (OSError, ValueError, IndexError, StopIteration):
        available = None
        failures.append("MEMAVAILABLE_UNAVAILABLE")
    build_bin = ROOT / "build-v070/nopen-v070-ublk"
    worker_bin = ROOT / "build-v070/v060/nopen-isolated-worker"
    if not build_bin.is_file() or not worker_bin.is_file():
        notes.append("BUILD_NOT_PRESENT: compile and test before any future deployment")
    notes.append("PREVIOUS_FULL_HOST_FREEZE_UNEXPLAINED")
    notes.append("SWAP_PRIORITY_100_MATCHES_ZRAM: reclaim may use both devices")
    notes.append("NO PHYSICAL TRIAL AUTHORIZATION FROM THIS CHECK")
    return {
        "schema": "NOPEN_ISSUE10_NEXT_SWAP_PREPARATION",
        "utc": datetime.now(timezone.utc).isoformat(),
        "branch": branch, "revision": revision if rc == 0 else "unknown",
        "services": services, "active_swaps": swaps,
        "gpu": card, "mem_available_mib": available,
        "failures": failures, "notes": notes,
        "preparation_gate": "PASS" if not failures else "BLOCKED",
        "kernel_swap_activated": False,
        "kernel_swap_activation_authorized": False,
        "host_disks_modified": False,
        "existing_zram_modified": False,
        "reclaim_forward_progress_proven": False
    }


def main() -> int:
    snapshot = assess()
    print(json.dumps(snapshot, ensure_ascii=False, indent=2))
    print("ISSUE10_PREP_" + snapshot["preparation_gate"] +
          " physical_swap=NOT_AUTHORIZED kernel_swap=NOT_STARTED")
    return 0 if not snapshot["failures"] else 2


if __name__ == "__main__":
    sys.exit(main())
