# Nopen v0.8.0-alpha.7 — Async GPU kuyruk açlığı düzeltmesi

## Gerçek RX 9060 XT sonuçlarından çıkan kesin bulgular (alpha.6)
- 256MiB sync, 2000 * 4KiB: p50=1456.35us, p95=1513.04us, toplam 2.884s.
- 256MiB async: p50=1030.18us, p95=1112.32us, 2.113s;
  **34 enqueue, 1966 drop, yalnızca 2 GPU copy completion**.
- 4096MiB async: p50=1032.62us, p95=1111.55us, 2.106s;
  **501 enqueue, 1499 drop, 469 GPU completion**.
- Gerçek 4GiB OpenCL havuzu ve Btrfs NOCOW SSD shadow başlatıldı,
  worker kaybından sonra 1024 sayfa doğrulandı, kernel swap kapalıydı.
- Dolayısıyla 4GiB kapasite tek başına problem değil. Bu sonuçlar
  **4GiB gerçek Linux kernel SWAP'ın güvenli çalıştığını kanıtlamaz**.

## Yeni düzeltilen somut yarışma

Alpha.6 `gpu_upload_worker` işçisi **aynı `s->lock` kilidini**,
her `nopen_shadow_write` çağrısının SSD'ye `O_DSYNC` yaparken
tuttuğu kilitle paylaşıyordu. Her 4KiB yazma ~1ms kilidi tuttuğu için
GPU işçisi yoğun trafik altında dequeue yapamıyor, 32-slot GPU
önbellek kuyruğu sürekli doluyordu. Bu kod yapısı gerçek kayıtlardaki
sıra açlığı için **somut bir tasarım sorunudur**; tek başına anormal
256MiB GPU tamamlama sayısının kesin kök nedeni olarak gösterilmez.

Alpha.7:
- GPU kuyruğuna ayrı `upload_lock` mutex'i kondu.
  Disk `O_DSYNC` işlemi GPU dequeuing işlemini artık kilitlemiyor.
- Sayfa checksum, versiyon ve GPU-valid işaretleri eski `s->lock`
  içinde tutularak SSD'de onaylanan en son veriyle eşleştirilmeye
  devam ediyor. Önceki yazmanın gecikmiş GPU kopyası güncel
  sayfa olarak okunamıyor.
- İşçinin GPU upload süresi toplamı ve maksimumu mikro saniye
  cinsinden raporlanıyor: `gpu_upload_avg_us`, `gpu_upload_max_us`.
- Bounded 32-slot queue ve `O_DIRECT|O_DSYNC` SSD authoritative
  mekanizması korunuyor. GPU başarısızsa diskten okunmaya devam.
- Canlı kernel SWAP açmadan mock testler, gerçek ublk derlemesi
  ve offline 4GiB testi CI'da doğrulanıyor.

## Gerçek GPU üzerinde hedefli tanı (kernel SWAP KAPALI)

Alpha.7 derlemesi:
```fish
git clone --branch develop/v0.8.0-alpha.7 --single-branch \
 https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a7
cd ~/nopen-v080a7
cmake -S v080 -B build-v080a7 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v080a7 --parallel
ctest --test-dir build-v080a7 --output-on-failure
```

Darboğazın ana göstergesi için yalnızca 256MiB asenkron deneyi:
```fish
set W (pwd)/build-v080a7/v070/v060/nopen-isolated-worker
./build-v080a7/nopen-v080-async-shadow-bench \
 --backend opencl --mode async --capacity-mib 256 --operations 2000 \
 --worker $W --shadow-file $HOME/nopen-a7-256-async.bin
```

`V080_ASYNC_BENCH_PASS` kaydı, `gpu_completed`, `dropped`,
`gpu_upload_avg_us`, `gpu_upload_max_us` ile alpha.6
sonuçlarını kıyaslar. 4GiB GPU-only testini her seferinde
tekrarlamak gerekmez.

## Kalan mimari sınırlama
- GPU cache kuyruğu doygunluğu düzeltilse bile SSD `O_DSYNC` ~1ms
  kernel SWAP write ACK'ın alt sınırı olarak kalır. Ana hedef saf VRAM
  bant genişliği ise bu **bambaşka bir mimari strateji** gerektirir;
  SSD yedeğinin veri kurtarma garantisinden vazgeçmeden onu
  ACK zincirinden çıkaramayız.
- Physical kernel reclaim altında AMDGPU forward progress kanıtlanmadı.
  `start-4g` hâlâ tehlikeli; **sırf mock PASS diye çalıştırma**.
