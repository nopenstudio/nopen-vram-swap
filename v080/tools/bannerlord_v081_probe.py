#!/usr/bin/env python3
"""Nopen v0.8.1: observe Bannerlord + SSD-free GPU page IO.

No kernel SWAP, root, SSD shadow, zram changes or modification to game.
Reports game existence/RSS, system VRAM pressure and 4K/64K latency.
FPS cannot be inferred from GPU I/O metrics. Use MangoHud separately.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from datetime import datetime
from statistics import median

# The v0.7 game probe already recognizes Proton's high-RSS "Starter"
# host, which v0.8.1 previously misclassified as a launcher-only process.
# Reuse the established classifier instead of maintaining two conflicting
# lists of process names.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "v070" / "tools"))
from find_bannerlord import entries as find_game_entries, find_game_pid

MIB = 1024 * 1024
PROC = Path("/proc")
GPU_DIR = Path("/sys/bus/pci/devices")


def active_gpu_swap() -> bool:
    try:
        lines = (PROC / "swaps").read_text().splitlines()[1:]
        return any(x.split()[0].startswith(("/dev/ublkb", "/dev/nbd"))
                   for x in lines if x.split())
    except OSError:
        return True


def bannerlord_process() -> dict | None:
    # Reuse the established v0.7 detection, including Proton's "Starter"
    # host only when the launcher path and >=512 MiB RSS are verified.
    try:
        pid = find_game_pid(PROC)
        found = next(item for item in find_game_entries(PROC) if item[0] == pid)
    except (OSError, ValueError, StopIteration):
        return None
    _, rss_kib, process = found
    return {"pid": pid, "comm": process, "rss_mib": round(rss_kib / 1024)}


def amd_gpu_vram() -> dict:
    matches = []
    for dev in GPU_DIR.iterdir():
        try:
            if (dev / "vendor").read_text().strip().lower() != "0x1002":
                continue
            if not (dev / "class").read_text().strip().lower().startswith("0x03"):
                continue
            total = int((dev / "mem_info_vram_total").read_text()) // MIB
            used = (int((dev / "mem_info_vram_used").read_text()) + MIB - 1) // MIB
            matches.append({"pci": dev.name, "total_mib": total, "used_mib": used})
        except (OSError, ValueError):
            continue
    if len(matches) != 1:
        raise RuntimeError("Expected exactly one AMD discrete GPU with VRAM counters")
    return matches[0]


def mem_available_mib() -> int:
    for line in (PROC / "meminfo").read_text().splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) // 1024
    raise RuntimeError("MemAvailable unavailable")


def check_budget(capacity_mib: int) -> dict:
    gpu = amd_gpu_vram()
    headroom = max(4096, (gpu["total_mib"] + 3) // 4)
    free_after = gpu["total_mib"] - gpu["used_mib"] - capacity_mib
    if free_after < headroom:
        raise RuntimeError(f"VRAM headroom insufficient: after GPU pool {free_after} MiB,"
                           f" required {headroom} MiB. Use --capacity-mib 256 or close GPU apps.")
    if mem_available_mib() < 1536:
        raise RuntimeError("Less than 1536 MiB MemAvailable. Refusing test under RAM pressure.")
    gpu["headroom_after_pool_mib"] = free_after
    return gpu


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mode", choices=("game", "baseline"), default="game")
    p.add_argument("--capacity-mib", type=int, choices=(256, 4096), default=256)
    p.add_argument("--rounds", type=int, choices=(1, 2, 3, 4, 5), default=3)
    p.add_argument("--bench", type=Path, required=True)
    p.add_argument("--worker", type=Path, required=True)
    args = p.parse_args()
    if os.geteuid() == 0 or active_gpu_swap():
        print("V081_REFUSED root or kernel GPU/NBD SWAP active", file=sys.stderr)
        return 2
    bench = args.bench.resolve()
    worker = args.worker.resolve()
    if not bench.is_file() or not os.access(bench, os.X_OK) or not worker.is_file() or not os.access(worker, os.X_OK):
        print("V081_REFUSED compiled benchmark/worker missing", file=sys.stderr)
        return 2
    game = bannerlord_process()
    if (args.mode == "game" and game is None) or (args.mode == "baseline" and game is not None):
        print(f"V081_REFUSED mode={args.mode} game_detected={game is not None}"
              " (use --mode baseline without game, --mode game while playing)",
              file=sys.stderr)
        return 2
    try:
        before = check_budget(args.capacity_mib)
    except (RuntimeError, OSError, ValueError) as exc:
        print(f"V081_REFUSED {exc}", file=sys.stderr)
        return 2
    out = Path.home() / ("nopen-v081-bannerlord-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    out.mkdir(mode=0o700, exist_ok=False)
    print(f"V081_START mode={args.mode} game_pid={game['pid'] if game else 'NONE'}"
          f" capacity_mib={args.capacity_mib} rounds={args.rounds}"
          " kernel_swap=NO ssd_shadow=NO zram_untouched=YES"
          f" report={out}", flush=True)
    records = []
    try:
        for kib in (4, 64):
            for trial in range(1, args.rounds + 1):
                current_game = bannerlord_process()
                if args.mode == "game" and (current_game is None or current_game["pid"] != game["pid"]):
                    raise RuntimeError("Bannerlord process exited/changed; aborting")
                if args.mode == "baseline" and current_game is not None:
                    raise RuntimeError("Game started during baseline; aborting")
                gpu = check_budget(args.capacity_mib)
                cmd = [str(bench), "--backend", "opencl",
                       "--capacity-mib", str(args.capacity_mib),
                       "--io-kib", str(kib), "--operations", "2000",
                       "--spread-across-vram", "--worker", str(worker)]
                started = time.monotonic()
                peak_vram_mib = gpu["used_mib"]
                min_mem_available_mib = mem_available_mib()
                with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                      text=True) as process:
                    while process.poll() is None:
                        if time.monotonic() - started > 45:
                            process.kill()
                            process.communicate()
                            raise RuntimeError(f"GPU worker benchmark timed out io_kib={kib}")
                        # These samples cover the GPU worker's LIFETIME,
                        # unlike only checking VRAM before and after it exits.
                        try:
                            peak_vram_mib = max(peak_vram_mib, amd_gpu_vram()["used_mib"])
                            min_mem_available_mib = min(min_mem_available_mib,
                                                        mem_available_mib())
                        except (OSError, ValueError):
                            pass
                        time.sleep(0.05)
                    stdout, stderr = process.communicate()
                    returncode = process.returncode
                output = stdout + "\n" + stderr
                (out / f"block-{kib}k-trial-{trial}.log").write_text(output)
                if returncode != 0:
                    raise RuntimeError(f"GPU integrity failure io_kib={kib} trial={trial}"
                                       f" rc={returncode}; see report logs")
                passing = re.findall(r"NOPEN_VRAM_DIRECT_PASS[^\n]*", output)
                if len(passing) != 1:
                    raise RuntimeError("GPU benchmark did not issue exactly one PASS line")
                fields = dict(re.findall(r"([a-z][a-z0-9_]*)=([^\s]+)", passing[0]))
                if fields.get("spread_across_vram") != "YES" or int(fields.get("segment_edge_checks", "0")) < 3:
                    raise RuntimeError("Benchmark did not verify full VRAM address span")
                after_game = bannerlord_process()
                if args.mode == "game" and (after_game is None or after_game["pid"] != game["pid"]):
                    raise RuntimeError("Bannerlord exited during GPU test")
                records.append({"io_kib": kib, "trial": trial,
                                "gpu_before": gpu, "gpu_after": amd_gpu_vram(),
                                "game_rss_mib": after_game["rss_mib"] if after_game else None,
                                "gpu_peak_used_mib": peak_vram_mib,
                                "os_min_mem_available_mib": min_mem_available_mib,
                                "wall_s": round(time.monotonic()-started, 3),
                                "results": fields})
                print(f"V081_TRIAL_PASS io_kib={kib} trial={trial}"
                      f" write_p50_us={fields.get('write_p50_us')}"
                      f" write_p95_us={fields.get('write_p95_us')}"
                      f" read_p95_us={fields.get('read_p95_us')}"
                      f" segment_edges={fields['segment_edge_checks']}"
                      f" gpu_peak_used_mib={peak_vram_mib}", flush=True)
        report = {"schema": "NOPEN_V081_BANNERLORD_GPU_COEXIST",
                  "status": "PASS", "mode": args.mode, "capacity_mib": args.capacity_mib,
                  "game_before": game, "gpu_before": before,
                  "game_after": bannerlord_process(), "records": records,
                  "kernel_swap_activated": False,
                  "ssd_shadow_used": False, "zram_changed": False,
                  "fps_measured": False,
                  "notes": "GPU/CPU contention and hardware game FPS need separate MangoHud readings."}
        (out / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
        print(f"V081_GAME_GPU_COEXIST_PASS mode={args.mode} trials={len(records)}"
              f" capacity_mib={args.capacity_mib}"
              " gpu_span_verified=YES segment_boundaries=YES"
              " kernel_swap=NO ssd_shadow=NO fps_measured=NO"
              f" report={out/'report.json'}")
        return 0
    except (RuntimeError, OSError, ValueError) as exc:
        (out / "failure.txt").write_text(str(exc) + "\n")
        print(f"V081_GAME_GPU_COEXIST_FAIL {exc} logs={out}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
