#!/usr/bin/env python3
"""Static safety contract for issue #10 raw ublk pilot. Does not execute it."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
p = root / "v080/tools/issue10_ublk_raw_only.sh"
src = p.read_text(encoding="utf-8")
lines = [line.strip() for line in src.splitlines()
         if line.strip() and not line.lstrip().startswith("#")]
executable = "\n".join(line for line in lines if not line.startswith("echo "))
for pattern in (
    r"(?m)^\s*(?:sudo\s+)?swapon\b",
    r"(?m)^\s*(?:sudo\s+)?swapoff\b",
    r"(?m)^\s*mkswap\b",
    r"(?m)^\s*(?:mount|mkfs\.[^ ]+|zramctl)\b",
    r"(?m)\b(?:dd|cp)\s+[^\n]*\bof=/dev/(?!ublkb23)",
):
    assert re.search(pattern, executable) is None, f"forbidden action: {pattern}"
assert '[[ "$capacity" == 256 ]]' in src, "pilot capacity not hard-limited"
assert "NOPEN_ISSUE10_RAW_ACK" in src and "read -r approval </dev/tty" in src
assert 'python3 "$repo/v080/tools/v081_direct4g_preflight.py"' in src
assert 'python3 "$repo/v070/tools/find_bannerlord.py" --pid-only' in src
assert 'NOPEN_UBLK_STORAGE_MODE=vram-direct' in src
assert '--setenv=NOPEN_DIAG_IO_TRACE=1' in src
assert '--setenv=NOPEN_V070_CAPACITY_MIB=256' in src
assert 'of="$dev"' in src and 'if="$dev"' in src
assert 'oflag=direct' in src and 'iflag=direct' in src
assert 'grep -Fq \'NOPEN_DIAG_UBLK phase=BEGIN\'' in src
assert 'grep -Fq \'NOPEN_DIAG_UBLK phase=END\'' in src
assert 'grep -Fq \'NOPEN_DIAG_WORKER phase=GPU_RETURN\'' in src
assert 'if awk ' in src and "/proc/swaps" in src
assert "trap 'cleanup $?' EXIT" in src
assert '"$controller" del -n "$id"' in src
assert 'systemctl stop "$service"' in src
assert "ISSUE10_RAW_INTEGRITY_PASS" in src
print("ISSUE10_RAW_STATIC_PASS capacity=256 kernel_swap=NO "
      "no_shadow=YES explicit_risk_ack=YES trace=YES "
      "direct_io=YES cleanup=YES")
