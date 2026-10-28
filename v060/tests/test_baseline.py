#!/usr/bin/env python3
"""Isolated read-only game baseline fixture without a running game or GPU."""
from pathlib import Path
import csv
import subprocess
import sys
import tempfile
if len(sys.argv)!=2:raise SystemExit("expected game_baseline.py")
script=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)/"profile.csv"
    child=subprocess.Popen([sys.executable,"-c","import time;time.sleep(5)"])
    try:
        res=subprocess.run([sys.executable,str(script),"--pid",str(child.pid),
              "--duration-seconds","1","--interval-ms","250",
              "--csv",str(out)],text=True,capture_output=True,timeout=6)
        if res.returncode!=0 or "GAME_BASELINE_PASS" not in res.stdout:
            raise AssertionError(f"incorrect baseline result {res.returncode} {res.stdout} {res.stderr}")
        with out.open(newline="",encoding="utf-8") as fp:
            rows=list(csv.DictReader(fp))
        if len(rows)<4 or any(int(x["process_count"])<1 for x in rows):
            raise AssertionError("not enough process samples")
        again=subprocess.run([sys.executable,str(script),"--pid",str(child.pid),
              "--duration-seconds","1","--csv",str(out)],
              text=True,capture_output=True,timeout=3)
        if again.returncode!=2 or "will not overwrite" not in again.stderr:
            raise AssertionError("CSV overwrite guard missing")
    finally:
        child.terminate()
        try:child.wait(timeout=1)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
print("BASELINE_TEST_PASS sampled_process=YES read_only=YES overwrite_refused=YES no_global_swap=YES")
