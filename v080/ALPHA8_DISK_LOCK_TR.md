# Nopen VRAM SWAP | Alpha.8 — HDD/NVMe ve GPU metadata kilitlerini ayırma

## Kaynak ve gerçek RX 9060 XT ölçümleri

Alpha.7 gerçek OpenCL + 256 MiB NOCOW SSD shadow:
- `write_p50_us=825.94`, `write_p95_us=848.70`, 2000 yazma 1.683 s
- `enqueued=39`, `dropped=1961`, `gpu_completed=7`
- GPU upload tamamlanan 7 örnek: `gpu_upload_avg_us=880.29`, `gpu_upload_max_us=3762`

**Kesin kilit darboğazı:** Alpha.7 ayrı GPU kuyruk kilidi kullansa da
`nopen_shadow_write` 4 KiB `O_DIRECT|O_DSYNC` esnasında `s->lock`
metadata kilidini tutuyordu. GPU uploader, tamamlama/versiyon
kontrolünü kaydetmek için aynı metadata kilidini almak zorundaydı.
Dolayısıyla iş kuyruğu hızla 32/32 doluyor ve GPU cache kopyaları
atılıyordu.

## Alpha.8 kod değişikliği
- `disk_lock` tüm SSD okuma/yazmalarını, `aligned` staging buffer'ını,
  mevcut checksum snapshot'ını ve aynı sayfanın write/read sırasını
  korur.
- `s->lock` artık SSD `O_DSYNC` ve `pread` sırasında **serbest**.
  GPU worker kilidi bu esnada alıp kendi upload'ını tamamlayabilir.
- Kuyruk ayrı `upload_lock` ile korunur.
- Kilit sırası belgeli: `disk_lock -> s->lock -> upload_lock`.
  GPU worker, `upload_lock` tutarken `s->lock` almaz.
- Linux WRITE ACK ancak SSD write tamamlanınca verilir. GPU cache
  kaybında doğrulanmış SSD yedeğinden okuma sürer.
- `v080_alpha8_concurrent_shadow_io_and_gpu_worker` testinde iki
  farklı thread bir sayfaya 500 kez yazıp 600 kez okuyor; sayfa
  bütünlüğü ve GPU işçisi kapandıktan sonra son değer kontrol ediliyor.

## Test: yalnızca 256 MiB GPU + SSD; gerçek kernel SWAP açılmaz

```fish
git clone --branch develop/v0.8.0-alpha.8 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a8
cd ~/nopen-v080a8
cmake -S v080 -B build-v080a8 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v080a8 --parallel
ctest --test-dir build-v080a8 --output-on-failure
set worker (pwd)/build-v080a8/v070/v060/nopen-isolated-worker
./build-v080a8/nopen-v080-async-shadow-bench \
  --backend opencl --mode async --capacity-mib 256 \
  --operations 2000 --worker $worker \
  --shadow-file "$HOME/nopen-a8-256-async.bin"
```

Beklenen ölçümler:
`gpu_completed` artışı, `dropped` azalması,
`gpu_upload_avg_us` ve `gpu_upload_max_us` değişimi.

Bunların hiçbiri henüz **4 GiB Linux SWAP kararlılığı** anlamına
gelmez. `start-4g` veya zorlanmış bellek baskısını günlük bilgisayarda
yeniden çalıştırmak için güvenlik kanıtı değil. 4 GiB çekirdek SWAP
önceden aktif edildi ancak sistem yanıtsızlığı kök nedeninin kesin
kanıtı halen yok.

Önemli mimari sınır: O_DSYNC her sayfa için ~0.8 ms sürerken
GPU copy darboğazını kaldırsak bile Linux'un 4 KiB WRITE ACK
darboğazı NVMe SSD senkron yazımı olarak kalır. 40 GiB/s hedefi
bu blok-boyutu ve SSD write-through tasarımı ile kıyaslanamaz.
