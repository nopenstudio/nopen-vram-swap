#!/usr/bin/env python3
"""IE-04 read-only AMDGPU VRAM/GTT and selected-process DRM fdinfo observer.

Evidence only. Counts describe DRM client allocations, not a particular OpenCL
BO and never guarantee a physical pin. Does not start a GPU workload, use
debugfs, modify swap, load modules, change drivers or signal other processes.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "v085" / "tools"))
from reserved_vram_preflight import (  # noqa: E402
    MIB, gpu_devices, read_counter, read_mem_available, read_swaps,
)
from drm_vram_owners import parse_size  # noqa: E402

PCI_RE = re.compile(r"^[a-fA-F0-9]{4}:[a-fA-F0-9]{2}:[a-fA-F0-9]{2}\.[0-7]$")
FIELDS = ("vram", "gtt")
MEM_KEYS = ("resident", "total", "shared", "active")
SCHEMA = "NOPEN_IE04_AMDGPU_RESIDENCY_OBSERVATION_V1"


def select_gpu(pci_root: Path, pci: str) -> Path:
    if not PCI_RE.fullmatch(pci):
        raise ValueError("PCI must be a full AMD GPU BDF, e.g. 0000:03:00.0")
    candidates = [p for p in gpu_devices(pci_root) if p.name.lower() == pci.lower()]
    if len(candidates) != 1:
        raise ValueError("exactly one matching AMD graphics PCI device required")
    return candidates[0]


def proc_starttime(proc: Path, pid: int) -> str:
    """Linux /proc/PID/stat field 22, robust to parentheses in comm."""
    if pid <= 0:
        raise ValueError("PID must be positive")
    raw = (proc / str(pid) / "stat").read_text(encoding="utf-8").strip()
    if not raw.startswith(f"{pid} (") or ")" not in raw:
        raise ValueError("unexpected PID stat header")
    rest = raw[raw.rfind(")") + 1:].strip().split()
    if len(rest) < 20 or not rest[19].isdecimal():
        raise ValueError("invalid proc starttime")
    return rest[19]


def parse_client(raw: str, pci: str) -> dict | None:
    props = {}
    for line in raw.splitlines():
        if ":" not in line:
            continue
        k, v = line.split(":", 1)
        props[k.strip()] = v.strip()
    if props.get("drm-driver") != "amdgpu" or props.get("drm-pdev", "").lower() != pci.lower():
        return None
    cid = props.get("drm-client-id", "")
    if not cid.isdecimal():
        return None
    row = {"client_id": int(cid)}
    for region in FIELDS:
        for metric in MEM_KEYS:
            options = ([f"drm-resident-{region}", f"drm-memory-{region}"]
                       if metric == "resident" else [f"drm-{metric}-{region}"])
            selected = next((props[k] for k in options if k in props), None)
            if selected is None:
                value = None
            else:
                value = parse_size(selected)
                if value is None:
                    raise ValueError(f"invalid DRM {metric} {region} counter")
            row[f"{metric}_{region}_bytes"] = value
    return row


def inspect_pid(proc: Path, pid: int, pci: str, expected_start: str) -> dict:
    if proc_starttime(proc, pid) != expected_start:
        raise ValueError("PID starttime changed: process was replaced")
    directory = proc / str(pid) / "fdinfo"
    try:
        entries = sorted((f for f in directory.iterdir() if f.name.isdecimal()),
                         key=lambda x: int(x.name))
    except OSError as exc:
        raise ValueError(f"cannot enumerate PID fdinfo: {exc}") from exc
    clients: dict[int, dict] = {}
    unreadable = 0
    duplicates = 0
    for file in entries:
        try:
            obj = parse_client(file.read_text(encoding="utf-8"), pci)
        except (OSError, UnicodeError, ValueError):
            unreadable += 1
            continue
        if obj is None:
            continue
        cid = obj["client_id"]
        if cid in clients:
            duplicates += 1
            continue
        clients[cid] = obj
    if proc_starttime(proc, pid) != expected_start:
        raise ValueError("PID starttime changed during DRM snapshot")
    records = sorted(clients.values(), key=lambda x: x["client_id"])
    # Sums are ONLY for clients visible in this PID; shared GEMs and missing
    # fdinfo can make sums over-/under-count actual unique physical pages.
    visible_sums = {}
    complete = {}
    for region in FIELDS:
        for metric in MEM_KEYS:
            key = f"{metric}_{region}_bytes"
            vals = [c[key] for c in records if c[key] is not None]
            visible_sums[key] = sum(vals) if vals else None
            complete[key] = bool(records) and len(vals) == len(records) and unreadable == 0
    return {
        "pid": pid,
        "starttime_ticks": expected_start,
        "clients": records,
        "unique_clients": len(records),
        "duplicate_fds": duplicates,
        "unreadable_fdinfo": unreadable,
        "visible_sum_bytes": visible_sums,
        "all_visible_clients_reported_metric": complete,
        "drm_coverage": ("PARTIAL_OR_UNKNOWN" if unreadable else
                         "VISIBLE_DRM_CLIENTS" if records else
                         "NO_MATCHING_DRM_CLIENT"),
        "sum_is_exclusive_physical_bytes": False,
        "missing_client_means_no_gpu_memory": False,
    }


def snapshot(gpu: Path, meminfo: Path, swaps_file: Path, proc: Path,
             pid: int | None, starttime: str | None) -> dict:
    swap_names = read_swaps(swaps_file)
    if any(name.startswith(("/dev/ublk", "/dev/nbd")) for name in swap_names):
        raise ValueError("active ublk/NBD swap: IE-04 observation refused")
    total = read_counter(gpu, "mem_info_vram_total")
    used = read_counter(gpu, "mem_info_vram_used")
    gtt = read_counter(gpu, "mem_info_gtt_used")
    if total <= 0 or used > total:
        raise ValueError("invalid AMDGPU global VRAM accounting")
    return {
        "pci": gpu.name,
        "vram_total_bytes": total,
        "vram_used_bytes": used,
        "gtt_used_bytes": gtt,
        "mem_available_bytes": read_mem_available(meminfo),
        "active_swaps": swap_names,
        "target": (inspect_pid(proc, pid, gpu.name, starttime)
                   if pid is not None and starttime is not None else None),
    }


def compare_clients(before: dict | None, current: dict | None) -> list[dict]:
    if before is None or current is None:
        return []
    a = {c["client_id"]: c for c in before["clients"]}
    b = {c["client_id"]: c for c in current["clients"]}
    candidates = []
    for cid in sorted(a.keys() & b.keys()):
        old, new = a[cid], b[cid]
        ov, nv = old["resident_vram_bytes"], new["resident_vram_bytes"]
        og, ng = old["resident_gtt_bytes"], new["resident_gtt_bytes"]
        if None in (ov, nv, og, ng):
            continue
        dv, dg = nv - ov, ng - og
        if dv < -MIB and dg > MIB:
            candidates.append({
                "client_id": cid,
                "vram_delta_bytes": dv, "gtt_delta_bytes": dg,
                "class": "CORRELATED_COUNTER_CHANGE_NOT_BO_MIGRATION_PROOF",
            })
    return candidates


def observe(gpu: Path, meminfo: Path, swaps_file: Path, proc: Path,
            pid: int | None, samples: int, interval_seconds: float,
            sleep=time.sleep) -> list[dict]:
    if not (1 <= samples <= 120) or not (0 <= interval_seconds <= 10):
        raise ValueError("sample count or interval outside read-only bounds")
    expected_start = proc_starttime(proc, pid) if pid is not None else None
    items = []
    initial = None
    previous = None
    for n in range(samples):
        now = snapshot(gpu, meminfo, swaps_file, proc, pid, expected_start)
        if initial is None:
            initial = now
        if now["pci"] != initial["pci"] or now["vram_total_bytes"] != initial["vram_total_bytes"]:
            raise ValueError("device identity/capacity changed")
        candidates = compare_clients(previous["target"] if previous else None, now["target"])
        record = {
            "schema": SCHEMA,
            "sample": n,
            "gpu_pci": now["pci"],
            "global_vram_total_mib": round(now["vram_total_bytes"] / MIB, 3),
            "global_vram_used_mib": round(now["vram_used_bytes"] / MIB, 3),
            "global_gtt_used_mib": round(now["gtt_used_bytes"] / MIB, 3),
            "global_vram_delta_mib": round((now["vram_used_bytes"] - initial["vram_used_bytes"]) / MIB, 3),
            "global_gtt_delta_mib": round((now["gtt_used_bytes"] - initial["gtt_used_bytes"]) / MIB, 3),
            "host_mem_available_mib": round(now["mem_available_bytes"] / MIB, 3),
            "active_swaps": now["active_swaps"],
            "target": now["target"],
            "correlated_client_counter_changes": candidates,
            "possible_bo_migration_observed": bool(candidates),
            "bo_migration_proven": False,
            "particular_opencl_allocation_identified": False,
            "physical_vram_pinning_proven": False,
            "gpu_game_budget_enforced": False,
            "driver_changed": False,
            "gpu_allocations_started": False,
            "kernel_swap_authorized": False,
            "gate": "KERNEL_SWAP_BLOCKED",
        }
        items.append(record)
        previous = now
        if n + 1 < samples:
            sleep(interval_seconds)
    return items


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--pci", required=True, help="AMD graphics PCI BDF (no name-based selection)")
    p.add_argument("--pid", type=int, help="Existing process ID; never spawned or signaled")
    p.add_argument("--samples", type=int, default=5)
    p.add_argument("--interval-seconds", type=float, default=1.0)
    p.add_argument("--pci-root", type=Path, default=Path("/sys/bus/pci/devices"))
    p.add_argument("--proc-root", type=Path, default=Path("/proc"))
    p.add_argument("--meminfo", type=Path, default=Path("/proc/meminfo"))
    p.add_argument("--swaps-file", type=Path, default=Path("/proc/swaps"))
    args = p.parse_args()
    try:
        gpu = select_gpu(args.pci_root, args.pci)
        data = observe(gpu, args.meminfo, args.swaps_file, args.proc_root,
                       args.pid, args.samples, args.interval_seconds)
    except (ValueError, OSError) as exc:
        print(f"IE04_RESIDENCY_REFUSED reason={exc}", file=sys.stderr)
        return 2
    for row in data:
        print(json.dumps(row, ensure_ascii=False))
    print("IE04_READONLY_OBSERVATION_PASS physical_pin=UNPROVEN"
          " driver_bo_identity=UNPROVEN actual_migration=UNPROVEN"
          " kernel_swap=BLOCKED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
