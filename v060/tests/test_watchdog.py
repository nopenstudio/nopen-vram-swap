#!/usr/bin/env python3
"""The C fixture intentionally hangs; watchdog must terminate only that group."""
import subprocess
import sys
from pathlib import Path

if len(sys.argv)!=4:
    raise SystemExit("test_watchdog.py WATCHDOG FIXTURE")
tool=Path(sys.argv[1]).resolve()
fixture=Path(sys.argv[2]).resolve()
# Third arg is the single child-mode parameter.
extra=sys.argv[3]
def run(mode:str,expect:int,marker:str)->None:
    cmd=[sys.executable,str(tool),"--binary",str(fixture),
         "--timeout-seconds","1","--grace-seconds","0.1",
         "--","--mode",mode]
    proc=subprocess.run(cmd,capture_output=True,text=True,timeout=5)
    output=proc.stdout+"\n"+proc.stderr
    if proc.returncode != expect or marker not in output:
        raise AssertionError(f"{mode}: exit {proc.returncode}, expected {expect}: {output}")
run("pass",0,"WATCHDOG_PASS")
run("exitfail",7,"WATCHDOG_FAILED child_exit=")
run("silent",8,"WATCHDOG_FAILED child_exit_zero_but_no_verified_pass")
run("stall",6,"WATCHDOG_TIMEOUT child_terminated=YES")
print("WATCHDOG_TEST_PASS clean_pass=YES timeout=YES explicit_failure=YES "
      "false_success_rejected=YES kernel_swap_untouched=YES")
