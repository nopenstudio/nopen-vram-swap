# Nopen VRAM SWAP v0.8.1 — 4096 MiB GERÇEK GPU-ONLY kernel swap deneyi

**Hedef:** Bannerlord ile birlikte, sistemdeki gerçek Linux SWAP aygıtı
`/dev/ublkb23` üzerinden doğrudan AMD OpenCL VRAM sayfaları.

**DURUM:** DENEYSEL, ÇÖKME / VERİ KAYBI RİSKİ YÜKSEK.
Bu sürümün CI'da derlenmesi sistem belleği geri kazanımı
(reclaim), GPU reset, OOM ve swap-in için kararlılık kanıtı DEĞİLDİR.

Bu dal eski `develop/v0.8.1` oyun benchmarkını değiştirmez.
Eski `v070/tools/vram_swap_lab.sh start-4g` hâlâ SSD shadow
kullanır ve bu deneyin başlatıcısı **DEĞİLDİR**.

## GPU-only kaynak değişiklikleri
- `NOPEN_UBLK_STORAGE_MODE=vram-direct` ve ayrı GPU-only risk izni.
  `NOPEN_UBLK_SHADOW_FILE` gönderilmez, SSD shadow dosyası açılmaz.
- GPU işçisi RAM'de 4 GiB yerine GPU OpenCL üzerinde 16×256MiB
  `clCreateBuffer` oluşturur.
- İlk `swapon` öncesinde `clEnqueueFillBuffer + clFinish`
  ile her segmenti sıfırla (gerçek GPU yazması); VRAM fiziksel
  konumunun sabit kalma garantisi değildir.
- CPU'daki 4 GiB sayfa metaverileri ve GPU IPC çalışma tamponları
  `mlock` başarısızsa kernel swap başlamadan reddedilir.
  Bu GPU runtime'ın **tüm** bağımlılıklarını sabitlemez.
- GPU write/READ işlemleri checksum ile sınanır. İlk dört
  doğrudan sayfa okunup karşılaştırıldıktan sonra mkswap/swapon.
- Kernel SWAP önceliği 100: zram 100 ile **eşit**;
  mevcut zram devre dışı bırakılmaz. 4 GiB cihazın kernel
  tarafından gerçekten kullanılıp kullanılmadığı
  `/proc/swaps` USED değeriyle doğrulanmalı.
- Sistem servisindeki kullanıcı alanı işçisinin ilk hatasını
  `/var/log/nopen-v070/direct4g-*.log` içinde kaydet.
  Oyun sırasında GPU VRAM/GTT + MemAvailable + Nopen/zram
  swap kullanımı ayrıca 1Hz `direct4g-monitor-*.jsonl` olarak kaydedilir.

## Mevcut kısıtlamalar ve risk
- OpenCL GPU resetinde VRAM'deki canlı Linux SWAP sayfaları
  kaybolabilir. SSD fallback intentionally YOK; swap-in
  başarısız olursa oturum, prosesler ve kernel çökebilir.
- Kernel, Nopen işçisini 2 saniyelik IPC deadline içinde cevap
  vermeye zorlar. AMDGPU belleği geri kazanım sırasında kullanıcı
  alanında ilerleyemezse deadlock halen mümkün. RAM/GPU
  metadata mlock bu problemi kesin çözmez.
- Linux çökmesi yalnızca yeniden başlatma anlamına gelmez:
  kaydedilmemiş oyun verisi ve o sırada diske yazılan dosyalar
  kaybolabilir/bozulabilir. Btrfs kendi başına tam koruma değildir.
- Test tek bir günlük kullanılan masaüstü değil izole test cihazında
  daha güvenlidir. Ana bilgisayarda risk kullanıcıya aittir.
- GPU belleği 4 GiB gerçekten kullanılsın diye normal Bannerlord
  yükünün dışında `stress-ng` veya OOM testi çalıştırmayın.

## Önce güvenli salt-okunur ön test

```fish
git clone --branch experiment/v081-4g-vram-direct-swap --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v081-direct4g
cd ~/nopen-v081-direct4g
bash v080/tools/v081_direct4g_kernel_lab.sh preflight
bash v070/tools/build_ublk.sh
ctest --test-dir build-v070 --output-on-failure
```

Bu adımlar yalnızca sistem bilgilerini okur, derler, test eder;
kernel SWAP'ı etkinleştirmez.

## Ancak yedek alıp kaydedilmiş işleri kapattıktan sonra

Aşağıdaki `start` komutu **gerçek** kernel SWAP'ı açar,
GUI'nin kilitlenmesine ve zorla yeniden başlatmaya neden olabilir.

```fish
cd ~/nopen-v081-direct4g
sudo env NOPEN_V081_DIRECT4G_ACK=I_ACCEPT_REAL_4G_GPU_SWAP_MAY_FREEZE_AND_LOSE_UNSAVED_DATA \
  bash v080/tools/v081_direct4g_kernel_lab.sh start
```

Terminalde `EVET` diye onay istenir; başarısız kontrol
durumunda `swapon` öncesinde durur.
Çıktıda `V081_DIRECT4G_REAL_SWAP_ACTIVE` ifadesi ve
`swapon --show` sonucu olmadan GPU-only kernel SWAP
aktif kabul edilmemelidir.

Sonra Bannerlord'u aç. Oyun dünyasını yükle, FPS dalgalanması
veya bellek baskısı arttığında süreçleri zorla sonlandırma.
Kısa kontrol:

```fish
swapon --show
sudo tail -n 12 /var/log/nopen-v070/direct4g-monitor-*.jsonl
```

Oyun kapatıldıktan ve sistem yanıt verir durumdayken:

```fish
cd ~/nopen-v081-direct4g
sudo bash v080/tools/v081_direct4g_kernel_lab.sh stop
swapon --show
```

**Swapoff** canlı sayfalar varsa RAM'e geri taşımak için
uzun süre bekleyebilir veya başarısız olabilir. Başarısız
swapoff sonrası işçiyi öldürmeyin, cihazı zorla kaldırmayın.
Sistemin yalnızca zram ile kaldığını doğrulayın.

## Beklenen değerlendirme

Başarı dediğimiz şey sadece `swapon` ile 4 GiB görünmesi değil:
- `USED > 0` olan /dev/ublkb23 Linux kernel sayfalarının
  gerçekten bu aygıta konduğunu gösterir.
- `Read-error`, `Write-error`, `NOPEN_IPC_FATAL`,
  GPU worker hatası ve AMDGPU reset/ring timeout olmamalı.
- Bannerlord dünya yüklenmesi ve çıkış sonrasında sistem çalışmalı.
- `swapoff` ve servis temizliği gerçekten başarılı olmalı.
- 1Hz telemetri ve ublk logları saklanmalı, bunların yorumuyla
  v0.8.1 kabul kararı verilmeli.

**İlk oyun denemesinde otomatik yüksek RAM basıncı uygulanmaz.**
