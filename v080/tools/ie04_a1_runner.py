#!/usr/bin/env python3
"""IE-04/A1: guarded 256 MiB VRAM-DIRECT benchmark + conservative telemetry.

This is NOT a live-swap launcher. DEFAULT backend=mock. A future manual
OpenCL run needs explicit CLI risk approval plus live host preflight, and is
NOT run by CI. This script never creates ublk/NBD, changes zram, touches
swap settings, or initializes a GPU itself.

SSD data authority: NONE. CMake and optional result logging use the normal
host filesystem, but page data is held only by the existing isolated worker.
No claim of VRAM pin, exact BO residency, or kernel swap crash safety.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "v085" / "tools"))
sys.path.insert(0, str(REPO / "tools"))

from reserved_vram_preflight import evaluate, read_counter, read_mem_available, read_swaps
from ie04_residency_observe import inspect_pid, proc_starttime, select_gpu

MIB = 1024 * 1024
APPROVAL = "I_ACCEPT_256_MIB_REAL_OPENCL_NO_SWAP"
PASSPHRASE = "NOPEN_VRAM_DIRECT_PASS"
PCI_PATTERN = re.compile(r"^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]$")
MIN_FREE_VRAM_MIB = 4096
MIN_HOST_AVAILABLE_MIB = 1024
MAX_GPU_BUSY_PERCENT = 25
SCHEMA = "NOPEN_IE04_A1_GPU_ONLY_256M_REPORT_V1"


def optional_ascii(path: Path) -> str | None:
    try:
        return path.read_text(encoding="ascii").strip()
    except (OSError, UnicodeError):
        return None


def busy_percent(device: Path) -> int | None:
    raw = optional_ascii(device / "gpu_busy_percent")
    if raw is None or not raw.isdecimal():
        return None
    value = int(raw)
    return value if 0 <= value <= 100 else None


def game_busy(proc_root: Path) -> bool | None:
    """Best-effort extra protection; GPU-busy sysfs remains mandatory.

    Never kills games. If /proc enumeration itself fails, real OpenCL is
    refused; individual unreadable processes do not prove no game.
    """
    try:
        entries = list(proc_root.iterdir())
    except OSError:
        return None
    for p in entries:
        if not p.name.isdecimal():
            continue
        name = optional_ascii(p / "comm")
        if name is None:
            continue
        lowered = name.lower()
        if "bannerlord" in lowered or "taleworlds" in lowered:
            return True
        if lowered != "starter":
            continue
        status = optional_ascii(p / "status")
        if status is None:
            return True
        m = re.search(r"^VmRSS:\s+(\d+)\s+kB$", status, re.MULTILINE)
        if m is None or int(m.group(1)) >= 512 * 1024:
            return True
    return False


def policy(args: argparse.Namespace, get_euid=os.geteuid) -> dict:
    reasons: list[str] = []
    if get_euid() == 0:
        reasons.append("ROOT_FORBIDDEN")
    if args.capacity_mib != 256:
        reasons.append("EXACT_256_MIB_ONLY")
    if args.backend not in ("mock", "opencl"):
        reasons.append("BACKEND_UNSUPPORTED")
    if not PCI_PATTERN.fullmatch(args.pci):
        reasons.append("INVALID_PCI_BDF")
    if args.backend == "opencl" and args.real_opencl_ack != APPROVAL:
        reasons.append("REAL_OPENCL_REQUIRES_EXPLICIT_MANUAL_ACK")
    if args.backend == "opencl":
        # F3: v012 nopen_store_open() selects the FIRST AMD discrete GPU,
        # not the BDF checked here. No signed device_id ↔ PCI binding exists
        # in this A1 scope. ALWAYS refuse real OpenCL before any allocation.
        # Fixing discovery belongs to an explicitly authorized core task.
        reasons.append("OPENCL_DEVICE_PCI_IDENTITY_UNVERIFIED")
        if args.skip_build:
            reasons.append("REAL_OPENCL_SKIP_BUILD_FORBIDDEN")
    if args.backend == "opencl" and (
        args.pci_root != Path("/sys/bus/pci/devices") or
        args.meminfo != Path("/proc/meminfo") or
        args.swaps_file != Path("/proc/swaps") or
        args.proc_root != Path("/proc")
    ):
        reasons.append("REAL_GPU_MUST_USE_REAL_HOST_SENSORS")
    snap = evaluate(args.pci_root, args.meminfo, args.swaps_file,
                    pool_mib=256, min_free_mib=MIN_FREE_VRAM_MIB,
                    min_mem_available_mib=MIN_HOST_AVAILABLE_MIB,
                    pci=args.pci)
    reasons.extend(snap["reasons"])
    gpu = None
    try:
        gpu = select_gpu(args.pci_root, args.pci)
    except (ValueError, OSError) as exc:
        reasons.append("GPU_IDENTITY_NOT_VERIFIED")
        snap["gpu_identity_error"] = str(exc)
    occupancy = busy_percent(gpu) if gpu is not None else None
    snap["gpu_busy_percent"] = occupancy if occupancy is not None else "UNKNOWN"
    snap["gpu_name"] = optional_ascii(gpu / "product_name") if gpu else None
    snap["gpu_name"] = snap["gpu_name"] or "UNKNOWN"
    # MOCK itself allocates no GPU objects and needs no busy sensor. Real
    # OpenCL fails closed if usage is unknown/high or a known game is active.
    if args.backend == "opencl":
        if occupancy is None:
            reasons.append("GPU_BUSY_COUNTER_UNKNOWN")
        elif occupancy > MAX_GPU_BUSY_PERCENT:
            reasons.append("GPU_BUSY_OVER_25_PERCENT")
        games = game_busy(args.proc_root)
        snap["known_game_running"] = games if games is not None else "UNKNOWN"
        if games is None:
            reasons.append("GAME_PROCESS_INSPECTION_UNKNOWN")
        elif games:
            reasons.append("ACTIVE_GAME_DETECTED")
    else:
        snap["known_game_running"] = "NOT_CHECKED_MOCK_NO_GPU_ALLOCATION"
    return {
        "task": "IE-04/A1", "schema": SCHEMA,
        "backend": args.backend, "capacity_mib": args.capacity_mib,
        "requested_io_kib": [4, 64] if args.io_kib == "both" else [int(args.io_kib)],
        "gpu_pci": args.pci, "preflight": snap,
        "reasons": sorted(set(reasons)),
        "status": "REFUSED" if reasons else "ELIGIBLE_FOR_LOCAL_NO_SWAP_BENCH",
        "physical_vram_pin_proven": False,
        "kernel_swap": "BLOCKED", "ssd_data_io": "NONE",
        "evidence_level": ("MOCK_ONLY" if args.backend == "mock"
                           else "REAL_OPENCL_NO_SWAP_NOT_YET_RUN"),
    }


def proc_status_rss(proc: Path, pid: int | None) -> int | None:
    if pid is None:
        return None
    raw = optional_ascii(proc / str(pid) / "status")
    if raw is None:
        return None
    m = re.search(r"^VmRSS:\s+(\d+)\s+kB$", raw, re.MULTILINE)
    return int(m.group(1)) * 1024 if m else None


def child_worker(proc: Path, parent_pid: int, expected_worker: Path) -> int | None:
    """Identify exact direct child by cmdline, never by PID/name alone."""
    children = optional_ascii(proc / str(parent_pid) / "task" /
                              str(parent_pid) / "children")
    if children is None:
        return None
    for token in children.split():
        if not token.isdecimal():
            continue
        pid = int(token)
        try:
            stat = (proc / str(pid) / "stat").read_text(encoding="utf-8")
            tail = stat[stat.rfind(")") + 1:].split()
            if len(tail) < 20 or int(tail[1]) != parent_pid:
                continue
            argv = (proc / str(pid) / "cmdline").read_bytes().split(b"\0", 1)[0]
            if os.fsdecode(argv) == str(expected_worker):
                return pid
        except (OSError, ValueError, UnicodeError):
            continue
    return None


def record_snapshot(pci_root: Path, meminfo: Path, swaps: Path,
                    proc: Path, pci: str, parent_pid: int | None,
                    parent_start: str | None, worker_path: Path | None) -> dict:
    # Re-evaluate active swap and GPU identity on every snapshot.
    names = read_swaps(swaps)
    if any(n.startswith(("/dev/ublk", "/dev/nbd")) for n in names):
        raise ValueError("unsafe live ublk/NBD swap appeared during measurement")
    gpu = select_gpu(pci_root, pci)
    vram_total = read_counter(gpu, "mem_info_vram_total")
    vram_used = read_counter(gpu, "mem_info_vram_used")
    gtt_used = read_counter(gpu, "mem_info_gtt_used")
    if vram_total <= 0 or vram_used > vram_total:
        raise ValueError("invalid global VRAM accounting")
    frame = {
        "gpu_pci": pci,
        "global_vram_total_mib": round(vram_total / MIB, 3),
        "global_vram_used_mib": round(vram_used / MIB, 3),
        "global_gtt_used_mib": round(gtt_used / MIB, 3),
        "host_mem_available_mib": round(read_mem_available(meminfo) / MIB, 3),
        "active_swaps": names,
        "gpu_busy_percent": busy_percent(gpu),
        "pcie_current_link_speed": optional_ascii(gpu / "current_link_speed") or "UNKNOWN",
        "pcie_current_link_width": optional_ascii(gpu / "current_link_width") or "UNKNOWN",
        "bench_pid": parent_pid,
        "bench_starttime_ticks": parent_start if parent_pid else None,
        "bench_rss_mib": "UNKNOWN",
        "worker_pid": None, "worker_starttime_ticks": None,
        "worker_rss_mib": "UNKNOWN",
        "drm_pid": None, "drm_evidence": "UNKNOWN",
        "drm_resident_vram_mib": "UNKNOWN", "drm_total_vram_mib": "UNKNOWN",
        "drm_resident_gtt_mib": "UNKNOWN", "drm_total_gtt_mib": "UNKNOWN",
        "physical_vram_pin_proven": False, "bo_migration_proven": False,
    }
    if parent_pid is None:
        return frame
    try:
        if proc_starttime(proc, parent_pid) != parent_start:
            raise ValueError("benchmark PID reused")
    except (OSError, ValueError) as exc:
        raise ValueError(f"benchmark PID identity changed: {exc}") from exc
    rss = proc_status_rss(proc, parent_pid)
    frame["bench_rss_mib"] = round(rss / MIB, 3) if rss is not None else "UNKNOWN"
    child = child_worker(proc, parent_pid, worker_path) if worker_path else None
    frame["worker_pid"] = child
    if child is None:
        return frame
    try:
        start = proc_starttime(proc, child)
        info = inspect_pid(proc, child, pci, start)
        if proc_starttime(proc, child) != start:
            raise ValueError("worker PID reused")
    except (ValueError, OSError):
        frame["drm_evidence"] = "UNKNOWN_PID_OR_FDINFO"
        return frame
    wrss = proc_status_rss(proc, child)
    frame["worker_starttime_ticks"] = start
    frame["worker_rss_mib"] = round(wrss / MIB, 3) if wrss is not None else "UNKNOWN"
    frame["drm_pid"] = child
    # Any missing counters / permissions / no DRM client means UNKNOWN,
    # not zero memory usage, not failed OpenCL allocation.
    if info["unique_clients"] != 1 or info["unreadable_fdinfo"]:
        frame["drm_evidence"] = "UNKNOWN_MISSING_OR_AMBIGUOUS_CLIENT"
        return frame
    client = info["clients"][0]
    for region in ("vram", "gtt"):
        for metric in ("resident", "total"):
            value = client[f"{metric}_{region}_bytes"]
            if value is None:
                frame["drm_evidence"] = "UNKNOWN_DRM_COUNTERS"
                return frame
            frame[f"drm_{metric}_{region}_mib"] = round(value / MIB, 3)
    frame["drm_evidence"] = "COMPLETE_CLIENT_COUNTERS_NOT_PHYSICAL_BO_PROOF"
    return frame


def parse_pass(stdout: str, requested_io: int, backend: str) -> dict:
    rows = [line for line in stdout.splitlines() if line.startswith(PASSPHRASE + " ")]
    if len(rows) != 1:
        raise ValueError("missing or duplicate integrity PASS line")
    attrs = dict(chunk.split("=", 1) for chunk in rows[0].split()[1:]
                 if "=" in chunk)
    for field, expected in {
        "backend": backend, "capacity_mib": "256",
        "io_kib": str(requested_io), "shadow_file": "NONE",
        "ssd_write_through": "NO", "ssd_data_io": "NONE",
        "kernel_swap": "NO", "spread_across_vram": "YES",
        "benchmark_data_authority": "VRAM_ONLY",
        "physical_vram_pin": "UNPROVEN",
        "evidence_level": ("MOCK_ONLY" if backend == "mock"
                           else "REAL_OPENCL_NO_SWAP"),
    }.items():
        if attrs.get(field) != expected:
            raise ValueError(f"incorrect or absent PASS attribute {field}")
    for field in ("read_operations", "checksum_verified_reads",
                  "zero_page_checks", "segment_edge_checks"):
        if field not in attrs or int(attrs[field]) < 1:
            raise ValueError(f"missing integrity check: {field}")
    for field in ("write_p50_us", "write_p95_us", "write_p99_us",
                  "read_p50_us", "read_p95_us", "read_p99_us",
                  "write_mib_s", "read_mib_s", "write_iops", "read_iops"):
        if field not in attrs or float(attrs[field]) <= 0:
            raise ValueError(f"missing valid measurement {field}")
    return attrs


def abort_owned_benchmark(process: subprocess.Popen) -> tuple[str, str]:
    """Reap the owned new-session benchmark AND any worker holding pipes.

    F1: the session leader can already have exited while its direct child
    remains alive, inherits stdout/stderr, and prevents communicate() EOF.
    Never decide group cleanup from Popen.poll() alone. The Popen was created
    with start_new_session=True: its dedicated PGID equals process.pid.
    TERM once, bound pipe EOF; escalate group KILL on timeout, including when
    the parent was already reaped. Signal NO unrelated PID or group.
    """
    pgid = process.pid  # owned dedicated session, not a global name search
    if pgid <= 0:
        raise ValueError("invalid owned process group")
    if process.poll() is None:
        try:
            if os.getpgid(process.pid) != pgid:
                raise ValueError("refuse cleanup of non-owned process group")
        except ProcessLookupError:
            pass  # exited leader; owned PGID cannot be reused while group lives

    def signal_owned(signum: int) -> None:
        try:
            os.killpg(pgid, signum)
        except ProcessLookupError:
            pass  # group already gone; still collect any open pipe output

    signal_owned(signal.SIGTERM)
    try:
        return process.communicate(timeout=2)
    except subprocess.TimeoutExpired:
        signal_owned(signal.SIGKILL)
        try:
            return process.communicate(timeout=2)
        except subprocess.TimeoutExpired as exc:
            # Never block indefinitely on inherited pipe FDs.
            if process.stdout is not None:
                process.stdout.close()
            if process.stderr is not None:
                process.stderr.close()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                pass
            raise ValueError("owned process group pipe drain did not finish") from exc


def assess_drm_coverage(frames: list[dict]) -> dict:
    """F5: all observed intervals must identify the same parent/worker.

    One valid sample cannot turn unknown / truncated / missing KFD fdinfo
    into a residency PASS. This is ONLY per-client accounting, not an
    OpenCL BO identity, migration trace or hardware VRAM pin.
    """
    metric_keys = [f"drm_{metric}_{region}_mib"
                   for metric in ("resident", "total")
                   for region in ("vram", "gtt")]
    identities = [(f.get("bench_pid"), f.get("bench_starttime_ticks"),
                   f.get("worker_pid"), f.get("worker_starttime_ticks"),
                   f.get("drm_pid")) for f in frames]
    identity_ok = (len(frames) >= 2 and all(
        all(x is not None for x in identity) and identity[0] > 0
        and identity[2] > 0 and identity[2] == identity[4]
        and identity == identities[0] for identity in identities))
    complete = sum(
        f.get("drm_evidence") ==
        "COMPLETE_CLIENT_COUNTERS_NOT_PHYSICAL_BO_PROOF"
        and all(isinstance(f.get(k), (int, float)) and f[k] >= 0
                for k in metric_keys)
        for f in frames)
    all_complete = bool(frames) and complete == len(frames)
    accepted = identity_ok and all_complete
    return {
        "samples_total": len(frames),
        "samples_complete": complete,
        "all_samples_complete": all_complete,
        "identity_continuity_confirmed": identity_ok,
        "acceptance": ("COUNTERS_ONLY_NOT_BO_PIN_PROOF" if accepted
                       else "BLOCKED_MISSING_DRM_OR_PID_COVERAGE"),
        "physical_vram_pin_proven": False,
        "single_bo_migration_proven": False,
    }


def benchmark_once(args: argparse.Namespace, bench: Path, worker: Path,
                   io_kib: int) -> dict:
    command = [str(bench), "--backend", args.backend,
               "--capacity-mib", "256", "--io-kib", str(io_kib),
               "--operations", str(args.operations),
               "--spread-across-vram", "--worker", str(worker)]
    if args.backend == "opencl":
        # F3/F4 defense-in-depth: even a direct benchmark_once caller cannot
        # allocate on a worker whose chosen cl_device_id ↔ PCI is unknown.
        live = policy(args)
        if live["reasons"]:
            return {
                "io_kib": io_kib, "backend": args.backend, "status": "BLOCKED",
                "failure": "PRESTART_POLICY: " + ",".join(live["reasons"]),
                "benchmark_integrity_pass": False,
                "physical_vram_allocation_proven": False,
                "opencl_device_pci_identity_verified": False,
                "metrics": "UNKNOWN", "monitor_samples": 0,
                "drm_counters_complete_during_benchmark": False,
                "residency_measurement_acceptance": "BLOCKED_NOT_STARTED",
                "ssd_data_io": "NONE", "safety_gate": "KERNEL_SWAP_BLOCKED",
            }
    before = record_snapshot(args.pci_root, args.meminfo, args.swaps_file,
                             args.proc_root, args.pci, None, None, None)
    proc = subprocess.Popen(command, cwd=REPO, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True,
                            start_new_session=True)
    frames = []
    error = None
    begin = time.monotonic()
    start = None
    try:
        while proc.poll() is None:
            if time.monotonic() - begin > args.max_seconds:
                raise ValueError("benchmark exceeded bounded runtime")
            try:
                if start is None:
                    start = proc_starttime(args.proc_root, proc.pid)
                frames.append(record_snapshot(args.pci_root, args.meminfo,
                              args.swaps_file, args.proc_root, args.pci,
                              proc.pid, start, worker))
            except (ValueError, OSError) as exc:
                # A missing optional fdinfo is UNKNOWN, but a changed process
                # or active unsafe swap is a hard safety stop.
                error = str(exc)
                raise
            # Busy-percent rises because the benchmark itself uses the GPU.
            # Record that telemetry, but don't mistake our own work for a
            # preexisting active game. Fail closed if VRAM headroom collapses.
            if args.backend == "opencl":
                gpu = select_gpu(args.pci_root, args.pci)
                free_now = (read_counter(gpu, "mem_info_vram_total") -
                            read_counter(gpu, "mem_info_vram_used"))
                if free_now < MIN_FREE_VRAM_MIB * MIB:
                    raise ValueError("VRAM headroom fell below safety floor")
            time.sleep(0.05)
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        error = str(exc)
    finally:
        # F1: always reap the new-session group, even after the parent
        # exited with code 0 while an orphan worker kept PIPEs open.
        try:
            out, err = abort_owned_benchmark(proc)
        except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
            error = error or f"OWNED_PROCESS_CLEANUP_FAILED: {exc}"
            out, err = "", ""
    try:
        after = record_snapshot(args.pci_root, args.meminfo, args.swaps_file,
                                args.proc_root, args.pci, None, None, None)
    except (ValueError, OSError) as exc:
        after = {"error": str(exc), "drm_evidence": "UNKNOWN"}
        error = error or str(exc)
    metrics = None
    if proc.returncode == 0 and error is None:
        try:
            metrics = parse_pass(out, io_kib, args.backend)
        except (ValueError, TypeError) as exc:
            error = str(exc)
    elif error is None:
        error = f"benchmark exit status {proc.returncode}"
    coverage = assess_drm_coverage(frames)
    benchmark_integrity_pass = (
        error is None and proc.returncode == 0 and metrics is not None)
    # A1 real GPU execution is presently blocked by F3. This status remains
    # conservative if an authorized later task ever adds device binding.
    status = "PASS" if benchmark_integrity_pass else "FAIL"
    if args.backend == "opencl" and benchmark_integrity_pass and (
            coverage["acceptance"].startswith("BLOCKED")):
        status = "BLOCKED"
        error = "incomplete worker DRM fdinfo / PID coverage"
    return {
        "io_kib": io_kib, "backend": args.backend,
        "status": status,
        "failure": error, "benchmark_returncode": proc.returncode,
        "benchmark_integrity_pass": benchmark_integrity_pass,
        "opencl_device_pci_identity_verified": False,
        "physical_vram_allocation_proven": False,
        "metrics": metrics if metrics is not None else "UNKNOWN",
        "pre": before, "during": frames[:80], "post": after,
        "monitor_samples": len(frames),
        "monitor_frames_truncated": len(frames) > 80,
        "drm_coverage": coverage,
        "drm_counters_complete_during_benchmark":
            coverage["acceptance"] == "COUNTERS_ONLY_NOT_BO_PIN_PROOF",
        "residency_measurement_acceptance": coverage["acceptance"],
        "rss_scope": "BENCHMARK_PARENT_AND_ISOLATED_GPU_WORKER",
        "gpu_name": "UNKNOWN_IF_SYSFS_HAS_NO_PRODUCT_NAME",
        "gpu_pressure_and_hot_cold_page_metrics": "UNKNOWN",
        "gpu_bo_identity_and_migration": "UNPROVEN",
        "safety_gate": "KERNEL_SWAP_BLOCKED",
        "ssd_data_io": "NONE",
    }


def build_once(args: argparse.Namespace) -> tuple[Path, Path]:
    build = args.build_dir.resolve()
    if args.skip_build:
        if args.backend == "opencl":
            raise ValueError("REAL_OPENCL_SKIP_BUILD_FORBIDDEN")
        if args.bench_bin is None or args.worker_bin is None:
            raise ValueError("--skip-build requires both --bench-bin and --worker-bin")
        bench, worker = args.bench_bin.resolve(), args.worker_bin.resolve()
    else:
        if args.bench_bin or args.worker_bin:
            raise ValueError("explicit binary paths require --skip-build")
        subprocess.run(
            ["cmake", "-S", str(REPO / "v080"), "-B", str(build),
             "-DCMAKE_BUILD_TYPE=Release",
             "-DNOPEN_OPENCL=" + ("ON" if args.backend == "opencl" else "OFF")],
            check=True, timeout=60)
        subprocess.run(
            ["cmake", "--build", str(build), "--parallel", "2", "--target",
             "nopen-v080-vram-direct-bench", "nopen-isolated-worker"],
            check=True, timeout=120)
        bench = build / "nopen-v080-vram-direct-bench"
        worker = build / "v070/v060/nopen-isolated-worker"
    if not (bench.is_file() and os.access(bench, os.X_OK) and
            worker.is_file() and os.access(worker, os.X_OK)):
        raise ValueError("missing executable bench or worker (no fallback)")
    return bench, worker


def make_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--backend", choices=("mock", "opencl"), default="mock")
    p.add_argument("--capacity-mib", type=int, default=256)
    p.add_argument("--io-kib", choices=("both", "4", "64"), default="both")
    p.add_argument("--operations", type=int, default=500)
    p.add_argument("--pci", required=True, help="Explicit AMDGPU PCI BDF")
    p.add_argument("--real-opencl-ack", default="", help="Manual risk approval only; never supplied by CI")
    p.add_argument("--dry-run", action="store_true", help="Policy check only; never launch benchmark")
    p.add_argument("--skip-build", action="store_true")
    p.add_argument("--bench-bin", type=Path)
    p.add_argument("--worker-bin", type=Path)
    p.add_argument("--build-dir", type=Path, default=REPO / "build-v080a1")
    p.add_argument("--pci-root", type=Path, default=Path("/sys/bus/pci/devices"))
    p.add_argument("--meminfo", type=Path, default=Path("/proc/meminfo"))
    p.add_argument("--swaps-file", type=Path, default=Path("/proc/swaps"))
    p.add_argument("--proc-root", type=Path, default=Path("/proc"))
    p.add_argument("--max-seconds", type=int, default=50)
    return p


def main(argv: list[str] | None = None, get_euid=os.geteuid) -> int:
    args = make_parser().parse_args(argv)
    if args.operations < 100 or args.operations > 1000 or not 1 <= args.max_seconds <= 120:
        print("IE04_A1_REFUSED invalid benchmark bounds", file=sys.stderr)
        return 2
    decision = policy(args, get_euid)
    if decision["reasons"]:
        print(json.dumps(decision, ensure_ascii=False, sort_keys=True))
        print("IE04_A1_REFUSED " + ",".join(decision["reasons"]), file=sys.stderr)
        return 2
    if args.dry_run:
        print(json.dumps(decision, ensure_ascii=False, sort_keys=True))
        print("IE04_A1_PREFLIGHT_PASS no_benchmark_started=YES"
              " kernel_swap=BLOCKED physical_pin=UNPROVEN")
        return 0
    def not_started(kib: int, reason: str) -> dict:
        return {
            "io_kib": kib, "backend": args.backend, "status": reason,
            "failure": reason, "metrics": "UNKNOWN",
            "benchmark_integrity_pass": False,
            "opencl_device_pci_identity_verified": False,
            "physical_vram_allocation_proven": False,
            "monitor_samples": 0,
            "residency_measurement_acceptance": "BLOCKED_NOT_STARTED",
            "drm_counters_complete_during_benchmark": False,
            "ssd_data_io": "NONE", "safety_gate": "KERNEL_SWAP_BLOCKED",
        }

    runs: list[dict] = []
    rechecks: list[dict] = []
    try:
        bench, worker = build_once(args)
        # F4: compilation can take minutes. The initial policy decision is
        # stale after building. Recheck ALL conditions BEFORE any run.
        after_build = policy(args, get_euid)
        rechecks.append({"stage": "AFTER_BUILD",
                         "reasons": after_build["reasons"]})
        if after_build["reasons"]:
            decision["reasons"] = after_build["reasons"]
            decision["status"] = "BLOCKED"
            decision["runs"] = [
                not_started(k, "NOT_STARTED_DUE_TO_PREFLIGHT_FAILURE")
                for k in decision["requested_io_kib"]
            ]
            decision["preflight_rechecks"] = rechecks
            print(json.dumps(decision, ensure_ascii=False, sort_keys=True))
            print("IE04_A1_BLOCKED after_build_preflight=REFUSED",
                  file=sys.stderr)
            return 3
        for index, kib in enumerate(decision["requested_io_kib"]):
            # F4: run2 might start much later than run1. Require fresh PCI,
            # swap, VRAM headroom, RAM, busy/game and approval every time.
            fresh = policy(args, get_euid)
            rechecks.append({"stage": f"BEFORE_{kib}K",
                             "reasons": fresh["reasons"]})
            if fresh["reasons"]:
                decision["reasons"] = fresh["reasons"]
                runs.extend(
                    not_started(k, "NOT_STARTED_DUE_TO_PREFLIGHT_FAILURE")
                    for k in decision["requested_io_kib"][index:]
                )
                break
            run = benchmark_once(args, bench, worker, kib)
            runs.append(run)
            if run["status"] != "PASS":
                # F2: never allocate a new 64KiB workload after a FAIL or
                # BLOCKED 4KiB run, including missing DRM evidence.
                runs.extend(
                    not_started(k, "NOT_STARTED_DUE_TO_PRIOR_FAILURE")
                    for k in decision["requested_io_kib"][index + 1:]
                )
                break
    except (ValueError, OSError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as exc:
        print(f"IE04_A1_REFUSED_OR_FAILED reason={exc}", file=sys.stderr)
        return 3
    decision["runs"] = runs
    decision["preflight_rechecks"] = rechecks
    decision["status"] = (
        "PASS" if len(runs) == len(decision["requested_io_kib"]) and
        all(r["status"] == "PASS" for r in runs)
        else "BLOCKED" if decision.get("reasons") or any(
            r["status"] in ("BLOCKED", "NOT_STARTED_DUE_TO_PREFLIGHT_FAILURE")
            for r in runs)
        else "FAIL"
    )
    decision["evidence_level"] = ("MOCK_ONLY" if args.backend == "mock"
                                 else "REAL_OPENCL_NO_SWAP_NOT_TESTED")
    decision["residency_acceptance"] = (
        "BLOCKED_MOCK_HAS_NO_PHYSICAL_GPU_EVIDENCE" if args.backend == "mock"
        else "COUNTERS_ONLY_NOT_BO_PIN_PROOF"
        if decision["status"] == "PASS" and all(
            r.get("residency_measurement_acceptance") ==
            "COUNTERS_ONLY_NOT_BO_PIN_PROOF" for r in runs)
        else "BLOCKED_DRM_COUNTERS_UNAVAILABLE"
    )
    decision["gpu_name"] = decision["preflight"]["gpu_name"]
    decision["ssd_data_io"] = "NONE"
    decision["kernel_swap"] = "NO"
    decision["kernel_swap_authorized"] = False
    print(json.dumps(decision, ensure_ascii=False, sort_keys=True))
    print(f"IE04_A1_{decision['status']} backend={args.backend}"
          " capacity_mib=256 ssd_data_io=NONE kernel_swap=NO"
          " gpu_pin=UNPROVEN")
    return 0 if decision["status"] == "PASS" and (
        args.backend == "mock" or
        decision["residency_acceptance"] ==
        "COUNTERS_ONLY_NOT_BO_PIN_PROOF"
    ) else 4


if __name__ == "__main__":
    raise SystemExit(main())
