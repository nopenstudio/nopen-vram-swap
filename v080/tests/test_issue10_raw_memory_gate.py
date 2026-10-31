#!/usr/bin/env python3
"""Mock the *actual* read-only raw-memory shell gate without root/GPU/ublk.

The test extracts only raw_memory_gate() from the launcher and redirects its
/proc reads to fixtures. All state modifications remain confined to temp files.
"""
from pathlib import Path
import subprocess
import tempfile

script = (Path(__file__).resolve().parents[2]
          / "v080/tools/issue10_ublk_raw_only.sh").read_text()
start = script.index("raw_memory_gate() {")
end = script.index("\n}\n\ncase \"$action\"", start) + 2
function = script[start:end]
assert function.count("awk ") == 2
assert "available_mib < 3072" in function
assert "zram_used_mib > 1024" in function
assert 'raw_memory_gate\n' in script[end:]
assert script.count("  raw_memory_gate\n") == 1, "preflight gate expected"
assert script.count("\nraw_memory_gate\n") == 1, "run-time gate expected"

with tempfile.TemporaryDirectory(prefix="nopen-issue10-gate-") as td:
    root = Path(td)
    meminfo = root / "meminfo"
    swaps = root / "swaps"
    fake = (function.replace("/proc/meminfo", str(meminfo))
                    .replace("/proc/swaps", str(swaps))
                    .replace('python3 "$repo/v070/tools/find_bannerlord.py" --pid-only',
                             "false"))

    def check(mem_kib: int, used_kib: int, expected: int) -> None:
        meminfo.write_text(f"MemTotal: 8000000 kB\nMemAvailable: {mem_kib} kB\n")
        swaps.write_text("Filename Type Size Used Priority\n"
                         f"/dev/zram0 partition 7500000 {used_kib} 100\n")
        p = subprocess.run(["bash", "-c", fake + "\nraw_memory_gate"],
                           capture_output=True, text=True, timeout=3, check=False)
        assert p.returncode == expected, (mem_kib, used_kib, p.stderr, p.stdout)

    check(4157 * 1024, 0, 0)
    check(3072 * 1024, 1024 * 1024, 0)
    check(1367 * 1024, 3930884, 2)  # exact blocked host snapshot
    check(3071 * 1024, 0, 2)
    check(4096 * 1024, 1025 * 1024, 2)
    swaps.write_text("Filename Type Size Used Priority\n")
    p = subprocess.run(["bash", "-c", fake + "\nraw_memory_gate"],
                       capture_output=True, text=True, timeout=3, check=False)
    assert p.returncode == 2, "missing zram must fail closed"

    # Even with plenty of free RAM, a recognized game refuses physical raw I/O.
    meminfo.write_text("MemAvailable: 5000000 kB\n")
    swaps.write_text("Filename Type Size Used Priority\n"
                     "/dev/zram0 partition 7500000 0 100\n")
    game_present = fake.replace("if false >/dev/null 2>&1;then",
                                "if true >/dev/null 2>&1;then")
    assert game_present != fake, "mock game process condition not replaced"
    p = subprocess.run(["bash", "-c", game_present + "\nraw_memory_gate"],
                       capture_output=True, text=True, timeout=3, check=False)
    assert p.returncode == 2, "active Bannerlord must block raw ublk test"

print("ISSUE10_RAW_MEMORY_GATE_PASS normal=YES high_zram=BLOCKED "
      "low_mem=BLOCKED missing_zram=BLOCKED game_active=BLOCKED "
      "no_kernel_swap=YES")
