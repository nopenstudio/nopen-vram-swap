#!/usr/bin/env python3
"""Safety regression for implemented IE-01/02 P0 runtime hardening.

The subprocesses only call old shell entry points which must exit 77
BEFORE invoking privileged commands. Does not spawn a GPU worker or ublk.
"""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def blocked(script: str, action: str, args: list[str]) -> None:
    p = subprocess.run(["bash", str(ROOT / script), action, *args],
                       capture_output=True, text=True, timeout=4, check=False)
    assert p.returncode == 77, (script, action, p.returncode, p.stderr)
    assert ("BLOCKED" in p.stderr), (script, action, p.stderr)

for action, args in [
    ("start", []), ("start-pilot", ["64"]), ("start-4g", ["4096"])
]:
    blocked("v070/tools/vram_swap_lab.sh", action, args)
blocked("v080/tools/v081_direct4g_kernel_lab.sh", "start", [])

worker = read("v060/src/nopen_isolated_worker.c")
a=worker.index("buffer=malloc(NOPEN_BLK_MAX_BYTES);")
b=worker.index("memset(buffer,0,NOPEN_BLK_MAX_BYTES);")
c=worker.index("if(mlock(buffer,NOPEN_BLK_MAX_BYTES)==0)")
d=worker.index("nopen_ipc_msg msg={NOPEN_IPC_MAGIC,NOPEN_IPC_READY")
assert 0 <= a < b < c < d
assert "if(send_all(fd,&msg,sizeof(msg))!=0 || open_rc!=0)" in worker
assert "NOPEN_WORKER_NOT_READY" in worker
assert "NOPEN_SWAP_SERVER_STRICT" in worker

block=read("v050/src/nopen_blk_backend.c")
assert block.index("memset(b->staging,0,NOPEN_BLK_MAX_BYTES)") < block.index(
    "if(mlock(b->meta,"
) < block.index("if(nopen_store_open(&b->store")
assert "NOPEN_DIRECT_METADATA_MLOCK_REFUSED" in block
assert "NOPEN_SWAP_SERVER_STRICT" in block

parent=read("v060/src/nopen_isolated_io.c")
assert parent.index("memset(r->staging,0,NOPEN_BLK_MAX_BYTES)") < parent.index(
    "posix_spawn(&pid,"
)
assert "admission_timeout_ms" in parent
assert "pthread_mutex_trylock(&r->lock)" in parent

ublk=read("v070/experimental/ublk/nopen_ublk_swap_target.cpp")
assert "if(physical_lab)" in ublk and "NOPEN_LIVE_KERNEL_SWAP_BLOCKED" in ublk

# No user command here is allowed to activate actual Linux swap.
print("IE01_IE02_RUNTIME_GATES_PASS"
      " live_shell_start=BLOCKED physical_ublk=BLOCKED"
      " worker_ready_after_prefault=YES strict_mlock_before_gpu=YES"
      " ipc_admission_bounded=YES gpu_driver_reclaim_proven=NO")
