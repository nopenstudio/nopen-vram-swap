# Nopen VRAM-SWAP — OpenCL Benchmark v0.10.0-alpha.2

**Hedef:** RX 9060 XT'nin OpenCL CPU ↔ VRAM aktarım performansını gerçek donanımda ölçmek. Bu paket GPU belleğini **SWAP olarak etkinleştirmez**; Linux NBD kullanmaz, `swapon`, `swapoff` veya `mlockall` çağırmaz. Önceki çökme riski nedeniyle bir oyun veya kritik çalışma sürerken çalıştırmayın.

## Test ortamı

- Sistem: MSI PRO B650M-P, Ryzen 5 7500F, RX 9060 XT, 1×8 GiB DDR5-4800.
- `lspci`: PCIe 5.0 ×16 (32 GT/s ×16) etkin; PCIe teorik tek yönlü yaklaşık 63 GB/sn.
- Sistem RAM'i teorik 38,4 GB/sn, **tek kanal**. Gerçek GPU DMA bant genişliği bundan ve diğer mimari sınırlardan düşük olabilir.
- Vulkan'ın CPU BAR yoluyla okuması ~24 MiB/sn ölçüldü; bu, OpenCL'nin hızını göstermez.

## Garuda / Arch bağımlılıkları

```bash
sudo pacman -S --needed cmake gcc opencl-headers ocl-icd clinfo
clinfo -l
```

**Önemli:** `ocl-icd` yalnızca yükleyicidir. Ayrı AMD OpenCL ICD sürücüsü / çalışma zamanı kurulu ve `clinfo -l` çıktısında RX 9060 XT listelenmiş olmalıdır. Mevcut Nopen OpenCL sürücü kurulumunu değiştirmeyin; `clinfo` GPU'yu göstermiyorsa çıktıyı gönderin. Ek sürücü kurulumunu, mevcut grafik ortamını bozmadan değerlendireceğiz.

## Derleme

Arşivi çıkardıktan sonra:

```bash
cd Nopen_OpenCL_Benchmark_v0.10.0-alpha.2/experiments/opencl-benchmark
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Aşama 0 — Algılama

```bash
./build/nopen-opencl-bench --list
```

GPU listelenmiyorsa SWAP ya da Vulkan sürücüsüne dokunmayın. Eski OpenCL çalışma zamanını teşhis edeceğiz.

## Aşama 1 — 256 MiB, güvenli kısa kıyas

```bash
timeout --signal=INT --kill-after=10s 180s ./build/nopen-opencl-bench \
  --mib 256 --test-mib 64 --chunk-mib 8 --seg-mib 256 --pages 512 --host both \
  2>&1 | tee opencl-256.log
```

Test hem **pageable** hem **pinned** host belleği için şunları raporlar:
- 64 MiB sıralı RAM→VRAM ve VRAM→RAM transfer, ayrı MiB/s ve aktarım süreleri.
- Tüm 256 MiB adres aralığına dağıtılmış 512 farklı 4 KiB sayfada rastgele yazma/okuma hızı.
- 4 KiB işlemlerinin p50, p95, p99 CPU-observed tamamlanma gecikmeleri.
- İki aktarım türünde de bayt düzeyinde doğrulama.
- `gpu_event_ms`, varsa OpenCL olayının device ölçümü (CPU uçtan uca süresinden ayrı).

**Not:** Bu aşama tüm 256 MiB'yi okuyup yazmaz. Sıralı kısım 64 MiB, rastgele bölüm 512×4 KiB örneklerdir.

## Aşama 2 — 2 GiB, daha geniş VRAM adresleme

Aşama 1 başarılıysa ve masaüstünde önemli iş yoksa:

```bash
timeout --signal=INT --kill-after=10s 180s ./build/nopen-opencl-bench \
  --mib 2048 --test-mib 64 --chunk-mib 8 --seg-mib 256 --pages 512 --host both \
  2>&1 | tee opencl-2048.log
```

2 GiB, 8×256 MiB OpenCL buffer olarak ayrılır. Aşama 2 hâlâ **örnekleme testidir**, tüm VRAM'i doğrulamaz.

## Aşama 3 — İsteğe bağlı tüm 2 GiB doğrulama

**Yalnızca aşama 1–2 hızlı ve hatasızsa.** Bu test gerçekten 2 GiB'yi yazar ve ardından tüm 2 GiB'yi okur, byte-byte karşılaştırır; yavaş sistemlerde uzun sürebilir:

```bash
timeout --signal=INT --kill-after=10s 300s ./build/nopen-opencl-bench \
  --mib 2048 --test-mib 64 --chunk-mib 8 --seg-mib 256 --pages 512 \
  --host pageable --full-verify --cycles 1 \
  2>&1 | tee opencl-full-2048.log
```

Timeout Linux kullanıcı sürecini kesmeyi dener; GPU sürücüsü veya çekirdek beklemesi yaşanırsa sistemin kilitlenmesini kesin önlemez. Swap etkinleştirmeyin, oyun açmayın.

## Raporlarda neye bakacağız?

1. `SELECTED` doğru AMD dGPU mu? `unified=0` mu?
2. `H2D_write`, `D2H_read`: özellikle asimetrik hız var mı?
3. `pinned`, `pageable` kıyası: pinned host RAM avantaj sağlıyor mu?
4. `D2H_4K_latency`: p95/p99 kaç µs? Küçük blok gecikmesi SWAP-IN için önemli.
5. `FULL_VERIFY PASS` sağlandı mı? (ayrı uzun test)

**Bu benchmark NBD kilitlenmesini veya swap güvenirliğini ölçmez.** OpenCL iyi çıkarsa sonraki adım izolasyonlu ve normal RAM baskısından ayrılmış NBD/çekirdek teşhisidir. Pinned belleğin gerçek fiziksel yerleşimi sürücüye bağlıdır; ayrıca AMDGPU VRAM kullanım sayacıyla doğrulanabilir.

## Kaynak / lisans

Nopen Studio. MIT lisansı. OpenCL C API. Gerçek donanım sonuçları kullanıcı tarafından üretilecektir; burada sonuç uydurulmamıştır.