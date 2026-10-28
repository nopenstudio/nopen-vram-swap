#!/usr/bin/env python3
"""No-game fixtures; ensure swap mode and FPS evidence not fabricated."""
from pathlib import Path
import csv
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from compare_game import rows,compute,frametimes
from game_probe import swap_snapshot
def fake(path,mode,used):
    cols=("mode","rss_sum_kib","mem_available_kib","device_vram_used_kib",
          "nopen_swap_active","nopen_swap_used_kib","process_swap_sum_kib")
    with path.open("w",newline="") as fp:
        w=csv.DictWriter(fp,fieldnames=cols);w.writeheader()
        for i in range(3):
            w.writerow({"mode":mode,"rss_sum_kib":4000+(i*10)+(100 if mode=="on" else 0),
                        "mem_available_kib":3000-i*10,
                        "device_vram_used_kib":1000+i*10,
                        "nopen_swap_active":1 if mode=="on" else 0,
                        "nopen_swap_used_kib":used+i*80 if mode=="on" else 0,
                        "process_swap_sum_kib":0})
with tempfile.TemporaryDirectory() as td:
    folder=Path(td)
    off=folder/"off.csv";on=folder/"on.csv"
    fake(off,"off",0);fake(on,"on",0)
    result=compute(rows(off,"off"),rows(on,"on"))
    assert result["kernel_gpu_swap_usage_observed"]
    assert result["nopen_swap_used_kib_growth"]==160
    assert not result["fps_speedup_proven"]
    assert not result["game_specific_swap_attribution_proven"]
    assert not result["net_host_ram_saving_proven"]
    x=folder/"frames.csv"
    x.write_text("10\n"*200)
    assert frametimes(x)["average_fps"]==100
    mh=folder/"mangohud.csv"
    # Replace with valid 200 per-frame MangoHud rows, not sampled averages.
    mh.write_text("os,cpu,gpu\nGaruda,Ryzen,Radeon\nfps,frametime,cpu_load\n"+
                  "100,10,20\n"*200)
    assert frametimes(mh)["frames"]==200
    assert frametimes(mh)["average_fps"]==100
    report=compute(rows(off,"off"),rows(on,"on"),x,mh)
    assert report["observed_avg_fps_delta"]==0
    assert report["fps_speedup_observed"] is False
    assert report["fps_speedup_proven"] is False
    fake(on,"off",100)
    try:rows(on,"on")
    except ValueError:pass
    else:raise AssertionError("accepted mislabeled Nopen-off capture as on")
print("GAME_COMPARE_TEST_PASS true_swap_use=YES no_false_fps_claim=YES "
      "mismatched_capture_rejected=YES")
