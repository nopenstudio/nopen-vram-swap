# Nopen VRAM-SWAP — v0.12.0-alpha.5 | Taşınabilir çekirdek ve bellek ekonomisi

**Durum:** Kullanıcı alanında deneysel GPU destekli sayfalayıcı; **Linux sistem SWAP'i değil.** NopenOS henüz uygulanmadı, yalnızca tasarlanıyor. Bu sürüm Garuda Linux-zen üstünde geliştirilir. Eski NBD global SWAP betiklerini etkinleştirmeyin.

## Neden Alpha.5?

Alpha.4, RX 9060 XT'de 100.000 rastgele 4 KiB işlemi tamamladı (77.761 page-in, 78.753 page-out, 1.024/1.024 doğru sayfa, p99=120,71 µs, yaklaşık 105,7 MiB tepe süreç RSS). **Bu, net RAM kazancı anlamına gelmez.** Mantıksal çalışma kümesi yalnızca 4 MiB idi; 64 MiB GPU tamponu tahsis edildi ve OpenCL ile sürücü çalıştırma maliyetinin ayrıca ölçülmesi gerekiyor.

## Bu sürümde tamamlananlar

- Saf ISO C11 `nopen_page_core` kütüphanesi: sayfa durumları, 64-bit FNV1a bütünlük özeti, en eski resident sayfa seçimi. GNU fonksiyonu, Linux syscall, malloc, OpenCL bağımlılığı yoktur. `-ffreestanding -fno-builtin` derlemesi ve sıfır tanımsız sembol CI kontrolü vardır. **Bu, Linux çekirdek modülünün hazır olduğu anlamına gelmez.**
- Alpha.4 sayfalayıcısı ile alpha.2 kooperatif yöneticisinin checksum politikası aynı kaynak kodunda birleştirildi.
- `nopen-vram-demand --baseline --stress`: aynı deterministik rastgele sayfa iş yükünü **yalnızca normal CPU anonim RAM belleğinde** çalıştırır. VRAM ve UFFD başlatılmaz.
- GPU stres modu ile CPU baseline bağımsız proseslerde karşılaştırılabilir; `MEMORY_SNAPSHOT` ile `VmRSS`, `RssAnon`, `VmHWM` ölçülür. `getrusage` tepe RSS bilgisi de çıktıdadır.
- `compare_memory.sh`: başarı doğrulanmadan fark hesaplamaz; iki çalışmanın süreç tepe RSS farkını gösterir. **GPU ayırması RSS'e dahil değildir** ve toplam sistem bellek ekonomisini kanıtlamaz.

## Kurulum

```bash
cd ~
git clone --branch develop/v0.12.0-alpha.5 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v012a5
cd ~/nopen-v012a5
cmake -S v012 -B build-v012-a5 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a5 --parallel
ctest --test-dir build-v012-a5 --output-on-failure
swapon --show   # /dev/nbd15 ASLA aktif olmamalı
./build-v012-a5/nopen-vram-demand --probe
```

## Bellek ekonomisi: ilk donanım deneyi

**Bannerlord kapalıyken**, sudo kullanmadan, önce 4 MiB veriyle aynı iş yükünü karşılaştırın:

```bash
bash v012/tests/compare_memory.sh ./build-v012-a5/nopen-vram-demand \
  --backend opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 \
  --stress --operations 10000 --seed 387472 \
  --hot-pages 64 --hot-percent 70 --write-percent 50 \
  | tee ~/nopen-a5-economics.log
```

`BASELINE_PASS`, `STRESS_PASS`, `ECONOMICS_RESULT` aranmalı. `paged_minus_baseline_kib` pozitifse bu iş yükünde **GPU'lu proses daha fazla RSS tüketmiş** demektir. Negatifse bile yalnızca süreç RSS farkıdır; VRAM bütçesi, GPU driver kernel RAM tahsisleri, CPU/GPU performansı ve kullanılabilirlik testleri gerekir. Performans sayıları RAM bant genişliği değildir.

4 MiB'lik iş kümesinden net tasarruf beklemiyoruz. Daha büyük kapasite testleri için önce bu küçük kıyasın doğruluğunu kontrol edeceğiz. `--pool-mib` max 256 MiB stres güvenlik sınırını koruyor.

## GNU ve NopenOS bağımlılık sınırı

**Saf C politika modülü (`nopen_page_core`) yalnızca C11 temel tiplere bağlıdır.** Ancak `nopen-vram-demand` şu an Linux `/proc`, `mmap`, `userfaultfd` ve `libOpenCL.so.1` kullanıyor, glibc/ELF dinamik bağlayıcısı üzerinden çalışıyor. ROCm/OpenCL kütüphanelerini GNU'suz NopenOS'a taşımış değiliz. Şu anki NopenOS için kernel modülü yok, DRM/DMA entegrasyonu yok, gerçek SWAP yok.

Plan: bağımsız C politika mantığı → Linux/Garuda adaptörü → test edilmiş AMDGPU/DRM çekirdek bellek entegrasyon araştırması. GPU reseti ve reclaim deadlock sorunu çözülmeden çekirdek SWAP'i açılmayacak.