# Nopen VRAM SWAP v0.8.0-alpha.1 — 1 / 2 / 4 GiB

**Deneysel geliştirme sürümü. 4 GiB gerçek Linux SWAP desteği değildir.**
v0.7 genel start yasağı ve yalnızca 64 MiB fiziksel start-pilot kapısı korunur.
GitHub Issue #7: https://github.com/nopenstudio/nopen-vram-swap/issues/7

## Aşama hedefi ve dürüst kanıt

1. 1024 MiB mock + gerçek SSD yedeği testi başarılı olmadan 2048 MiB başlamaz.
2. 2048 MiB mock testi başarılı olmadan 4096 MiB başlamaz.
3. Her aşamada gerçek bir dosya O_DIRECT/O_DSYNC ile tamamen önceden ayrılır,
   sıfırlanır; 256 mantıksal 4 KiB sayfa **tam içeriğiyle** doğrulanır.
4. Her 256 MiB segment sınırını geçen 64 KiB bloklar ayrıca okunur/yazılır.
5. Mock GPU işçisi öldürülür; daha önce yazılan bütün örnekler otoriter SSD
   dosyasından okunarak tekrar doğrulanır. İşçi yokken yeni yazma da denenir.
6. **4 GiB'nin tamamındaki sayfalar tek tek içerik kontrolünden geçmez.**
   Adres alanı/segmentler örneklenir. GPU bu testlerde MOCK'tur.
   Gerçek AMDGPU belleği tahsis edilmez ve Linux kernel SWAP açılmaz.

## Alpha.1 optimizasyonları

| Öncesi | Değişiklik | Beklenti / sınır |
|---|---|---|
| 64 KiB SSD başlatma parçaları | 1 MiB hizalı tamponla ön-yazma, 64 KiB güvenli geri dönüş | 4 GiB için 65.536 yerine 4.096 O_DIRECT çağrısı; tamamen ön-sıfırlama korunur |
| Yazılmamış sayfada GPU IPC | Her sayfa için ayrı written-bit; hiç yazılmamış blokta sıfır döndür | SSD/GPU I/O atlanır, bit ancak başarılı kalıcı disk yazmasından sonra değişir |
| OpenCL profiling daima etkin | Varsayılan kapalı; NOPEN_OPENCL_PROFILE=1 ile açılır | Kuyruk in-order kalır; gerçek GPU üzerindeki performans kazanımı ayrı ölçülmeli |
| Sınırlı boyut kanıtı | 1024/2048/4096 MiB aşamalı, mock GPU+gerçek SSD | P50/P95 işlem gecikmeleri ve son 64-bit adres test edilir |

4 GiB için uint64 checksum dizisi ~8 MiB, GPU blok metadata ~16 MiB,
written-bit haritası ~128 KiB tutar. Bu **bilinen metadata miktarıdır**;
toplam RAM/RSS veya OpenCL sürücüsü tahsisi değildir.

## Neden 4 GiB fiziksel swap'i açmıyoruz?

- GPU çok hızlı olsa bile kernel ublk I/O isteklerini kullanıcı alanına iletir.
  Aynı anda bellek geri kazanımı olursa GPU çalışanı veya SSD yedeği
  üzerinden ilerleme kilitlenebilir.
- Her onaylı WRITE önce O_DSYNC SSD'ye, sonra VRAM'e yazılır. İki maliyet
  seridir; tek başına VRAM bant genişliği kazancı temsil etmez.
- Bannerlord aynı RX 9060 XT'yi kullandığında 4 GiB GPU rezervasyonu
  ekran kartına kalan VRAM'i azaltır. GTT/PCIe ve FPS/1% low izlenmelidir.
- v0.7 gerçek Bannerlord A/B ölçümünde GPU SWAP kullanımı doğrulansa da
  net sistem RAM kazancı veya FPS artışı doğrulanmadı.
- Eşzamanlı ublk kuyruklarını veya async write-back'i güvenli kaynak ve
  bozulma analizleri olmadan açmak ciddi veri kaybı riski getirir.

Referanslar:
- https://docs.kernel.org/block/ublk.html
- https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/clCreateCommandQueue.html
- https://btrfs.readthedocs.io/en/latest/Swapfile.html

## Kurulum / salt-okunur kapasite planlayıcısı

~~~bash
git clone --branch develop/v0.8.0-alpha.1 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a1
cd ~/nopen-v080a1
cmake -S v080 -B build-v080 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=OFF
cmake --build build-v080 --parallel
ctest --test-dir build-v080 --output-on-failure
python3 v080/tools/capacity_plan.py --stage-mib 1024
python3 v080/tools/capacity_plan.py --stage-mib 2048
python3 v080/tools/capacity_plan.py --stage-mib 4096
~~~

Planlayıcı fiziksel GPU VRAM toplam/kullanılan sayaçlarını, MemAvailable ve
seçilen shadow dizininin SSD boş alanını yalnızca okur. GPU havuzu için
en az 4 GiB veya VRAM'in %25'i baş boşluğu, SSD için kapasite + 512 MiB,
sistem için en az 1 GiB MemAvailable ister. Mevcut ublk/NBD kernel SWAP
aktifken reddeder. Feasible raporu **hiçbir kernel swap onayı değildir.**

Gerçek disk yazan ama asla SWAP açmayan mock aşama örneği:

~~~bash
./build-v080/nopen-v080-scale \
  "$PWD/build-v080/v070/v060/nopen-isolated-worker" \
  "$HOME/nopen-v080-alpha1-1gib-new.bin" 1024
~~~

Bu komut 1 GiB gerçek disk yazısı üretir. Başarılı test kendi dosyasını
siler; başarısızsa tanı için dosyayı tutar. Kullanıcı evindeki mevcut dosya
üzerine yazılmaz. Yerel 2/4 GiB testlerinin otomatik yapılması önerilmez;
CI'da mock aşamalar zorunlu sırayla ilerler.

## İleri kabul kapıları

- **Alpha.2:** 1 GiB gerçek OpenCL GPU ama KERNEL SWAP YOK.
- **Alpha.3:** 1 GiB kanıtından sonra 2 GiB gerçek GPU bellek dayanıklılığı.
- **Alpha.4:** 2 GiB kanıtından sonra 4 GiB gerçek GPU bellek ve Bannerlord
  performans değerlendirmesi; GPU rezervasyonunun oyunla rekabeti ölçülecek.
- **Kernel SWAP >64 MiB:** disposable VM, gerçek AMDGPU reclaim/worker kaybı,
  uzun test ve güvenli swapoff kanıtları gelmeden KESİNLİKLE açılmayacak.

Bu sürümün adı: **ölçekleme altyapısı + ölçümlü optimizasyon hazırlığı**.
