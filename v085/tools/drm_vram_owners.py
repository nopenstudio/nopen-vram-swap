#!/usr/bin/env python3
"""Read-only per-client DRM VRAM/GTT snapshot, for post-Holder investigations.

Only reports accessible /proc/<pid>/fdinfo records for one PCI device.
No privilege escalation, debugfs writes, kernel modules, GPU allocations,
SWAP operations, process signalling, or driver configuration.
DRM counters are per-client, not necessarily unique physical allocations:
shared buffers may be counted by multiple clients. Missing clients are NOT
evidence that no process uses the GPU.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

MIB = 1024 * 1024
SIZE = re.compile(r"^\s*(\d+)(?:\s+(B|KiB|MiB|GiB))?\s*$")


def parse_size(value: str) -> int | None:
    match = SIZE.fullmatch(value)
    if match is None:
        return None
    factor = {"B": 1, "KiB": 1024, "MiB": MIB,
              "GiB": 1024 * MIB}[match.group(2) or "B"]
    return int(match.group(1)) * factor


def parse_fdinfo(text: str, pci: str) -> dict | None:
    props = {}
    for row in text.splitlines():
        if ":" not in row:
            continue
        key, value = row.split(":", 1)
        props[key.strip()] = value.strip()
    if props.get("drm-driver") != "amdgpu":
        return None
    # Do not silently attribute memory from another GPU to the selected one.
    if props.get("drm-pdev") != pci:
        return None
    client_id = props.get("drm-client-id")
    if client_id is None or not client_id.isdecimal():
        return None
    def memory(region: str) -> int | None:
        # drm-memory-* is an older amdgpu alias of drm-resident-*.
        for field in (f"drm-resident-{region}", f"drm-memory-{region}"):
            if field in props:
                return parse_size(props[field])
        return None
    vram = memory("vram")
    gtt = memory("gtt")
    if vram is None and gtt is None:
        return None
    return {
        "pci": pci, "client_id": int(client_id),
        "resident_vram_bytes": vram,
        "resident_gtt_bytes": gtt,
    }


def scan(proc: Path, pci: str, limit: int = 12) -> dict:
    if limit < 1 or limit > 50:
        raise ValueError("limit must be 1..50")
    if not re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]", pci):
        raise ValueError("PCI identifier must have format 0000:03:00.0")
    if not proc.is_dir():
        raise ValueError("The proc root is unavailable")
    clients = {}
    scanned = 0
    permission_skips = 0
    for pid_dir in proc.iterdir():
        if not pid_dir.name.isdecimal() or not pid_dir.is_dir():
            continue
        scanned += 1
        fdinfo = pid_dir / "fdinfo"
        try:
            entries = list(fdinfo.iterdir())
        except OSError:
            permission_skips += 1
            continue
        for entry in entries:
            if not entry.name.isdecimal():
                continue
            try:
                values = entry.read_text(encoding="utf-8")
            except (OSError, UnicodeError):
                permission_skips += 1
                continue
            info = parse_fdinfo(values, pci)
            if info is None:
                continue
            key = (pci, info["client_id"])
            if key in clients:
                continue  # duplicate fd or shared reference, same DRM client
            try:
                name = (pid_dir / "comm").read_text(encoding="utf-8").strip()
            except (OSError, UnicodeError):
                name = "(unreadable)"
            info["pid"] = int(pid_dir.name)
            info["process_name"] = name[:128]
            for region in ("vram", "gtt"):
                v = info.pop(f"resident_{region}_bytes")
                info[f"resident_{region}_mib"] = (
                    round(v / MIB, 3) if v is not None else None)
            clients[key] = info
    ranked = sorted(clients.values(),
                    key=lambda a: a["resident_vram_mib"]
                    if a["resident_vram_mib"] is not None else -1,
                    reverse=True)
    return {
        "schema": "NOPEN_V085_DRM_CLIENT_ACCOUNTING",
        "pci": pci,
        "visibility": "ACCESSIBLE_PROC_FDINFO_ONLY",
        "process_directories_scanned": scanned,
        "unreadable_entries": permission_skips,
        "unique_drm_clients_found": len(ranked),
        "clients": ranked[:limit],
        "truncated_to_top": limit,
        "clients_may_share_backing_memory": True,
        "per_client_counters_prove_exclusive_physical_residency": False,
        "missing_clients_prove_no_gpu_usage": False,
        "gpu_memory_allocated": False,
        "kernel_swap_authorized": False,
        "safety_gate": "KERNEL_SWAP_BLOCKED",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pci", default="0000:03:00.0",
                        help="Exact AMDGPU PCI address")
    parser.add_argument("--top", type=int, default=12)
    parser.add_argument("--proc-root", type=Path, default=Path("/proc"),
                        help="Alternative proc root, for fixture tests only")
    args = parser.parse_args()
    try:
        report = scan(args.proc_root, args.pci, args.top)
    except (OSError, ValueError) as exc:
        print(f"V085_DRM_OWNERS_REFUSED reason={exc}", file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2, ensure_ascii=False))
    print("V085_DRM_OWNERS_PASS read_only=YES"
          " partial_visibility=YES no_exclusive_attribution=YES"
          " kernel_swap=BLOCKED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
