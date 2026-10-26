# Nopen Bannerlord — NBD kök neden A/B teşhisi (2026-10-26)

## Neden?

v0.9.2 arşivindeki 22:50:19 olayında NBD bağlantısı öldü ve swap okuma/yazma hataları tüm oturuma yayıldı. Kodda son etkin işlem **phase=16 (NBD cevabını AF_UNIX socketpair'e gönderme)**; ölçülen en yavaş OpenCL veri transferi 2,409 ms. Son telemetride yaklaşık 66 MiB /dev/nbd15 swap kullanımına karşı yaklaşık 1,25 GiB MemAvailable var. Bu, **GPU kapasitesinin dolduğu** tezini desteklemez. NBD soket/bellek geri kazanım kilitlenmesi kuvvetli hipotezdir, henüz kesin değil.

Linux çekirdeğinin drivers/block/nbd.c kaynağı NBD üzerinden swap için kilitlenme uyarısı taşır. 2026 TCP NBD fs reclaim düzeltme tartışmaları yerel AF_UNIX socketpair kullanımı için bire bir ispat değildir.

## Güvenlik

- Hiçbir betik `swapon`, `swapoff`, `mkswap`, `NBD_DISCONNECT`, veya SSD swap açma işlemi yapmaz.
- Varsayılan olarak **yalnızca zram swap** kabul edilir; eski GPU NBD swap aktifse testi reddeder.
- `baseline` ve `opencl` modları herhangi bir NBD başlatmaz.
- `nbd-raw` **yalnızca daha önce ayrı şekilde başlatılmış, aktif swap içermeyen** v0.9.3 NBD raw cihazında O_DIRECT veri okur/yazar; bu test /dev/nbd15 içeriğini değiştirir ve veri kaybına neden olabilir. Kullanıcının açık seçimi olmalıdır. Cihazı gerçek swap olarak kullanmayın.
- Kernel/GPU kilitlenmesi yine mümkün; açık projeleri kaydedin. Timeout sistemin donmasını garantiyle önlemez.

## 0 — Önceden kalan durumu kontrol et

```bash
swapon --show
systemctl is-active nopen-vram-v092.service nopen-vram-v093.service || true
```

`/dev/nbd15` swap aktifse **Nopen sürecini durdurma**; önce swap sayfalarının güvenli geri alınması gerekir. Mevcut durum netleşmeden teste geçme.

## 1 — Normal Bannerlord (Nopen yok)

```bash
./scripts/bannerlord-probe.sh baseline 420
```

İkinci terminalde Steam/Bannerlord'u aç; kayıtlı oyuna `Sefere Devam Et` ile gir, dünya haritasını yükle, çalışma alanları arasında geç. Kasmayı gözlemle.

## 2 — OpenCL ile oyun eşzamanlı testi (NBD/SWAP YOK)

Oyun kapalıyken başlat, ardından oyuna gir:

```bash
./scripts/bannerlord-probe.sh opencl 420
```

Arkaplanda mevcut `~/nopen-opencl-test/build-opencl/nopen-opencl-bench` 256 MiB VRAM ayırıp 64 MiB aktarır ve bırakır, aralarında 8 saniye durur. En büyük farkın Steam/Bannerlord GPU çekişmesi mi olduğunu gözlemle. Başarısızsa `workload.log` incele.

## 3 — Yalnızca gerekirse NBD ham blok + Bannerlord (SWAP YOK)

Önceki iki test başarılıysa ve v0.9.3 NBD backend'ini daha önce `./start-v093.sh` ile **sadece raw-only** modda açtığına eminsen:

```bash
./scripts/bannerlord-probe.sh nbd-raw 300
```

128 rastgele 4 KiB yazar/okur (O_DIRECT), 10 saniye bekler. Aktif `/dev/nbd15` swap varsa kesin reddeder. Bu, v0.9.3 NBD sunucusunun oyunla birlikte canlı kaldığını gösterir fakat gerçek swap'taki bellek reclaim sorununun çözüldüğünü kanıtlamaz.

## 4 — Çökme/yeniden başlatma sonrasında

```bash
./scripts/collect-after-reboot.sh
```

Oluşan `~/nopen-vram-experiment/bannerlord-safe-tests/*.tar.gz` dosyasını sohbete yükle. Günlükler görüntülenemiyorsa `sudo journalctl -b -1 -k` ile erişilebiliyor mu kontrol et; betik sistem durumunu değiştirmez.

## Sonuç yorumlama

- Baseline başarısız: Steam/Proton/Bannerlord veya genel grafik sürücüsü de aday.
- Baseline başarılı, OpenCL eşzamanlı başarısız: GPU çalışma zamanı/oyun rekabeti aday.
- Bunlar başarılı, NBD raw başarısız: NBD sunucu, soket veya GPU aktarım bağlantısı aday.
- Hepsi başarılı, gerçek swap geçmişte başarısız: bellek baskısı/NBD reclaim kilitlenmesi güçlenir; yine kök neden kanıtı için iş parçacığı yığınları gerekir.

**Önemli:** A/B sonuçları hipotezi daraltır; tüm olasılıkları dışlamaz. v0.9.3 gerçek swap kullanımı, eski v0.9.2 toplu SIGBUS olayı ve v0.9.3 oyun donması nedeniyle henüz güvenli değil.