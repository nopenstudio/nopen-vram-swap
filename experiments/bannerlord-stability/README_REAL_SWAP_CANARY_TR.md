# Gerçek VRAM-SWAP canary — v0.9.3, 512 MiB, tek döngü

Bu deney **gerçek Linux SWAP** kullanır. Ham blok veya saf OpenCL benchmark değildir. v0.9.3 arşivindeki `nopen-vram-nbd` sunucusu 2 GiB VRAM tahsis eder, /dev/nbd15'e bağlanır ve kullanıcının iki aşamalı onayıyla `swapon` etkinleştirilir. `test` komutu 512 MiB anonim belleği `MADV_PAGEOUT` ile dışarı taşımaya, ardından tekrar okuyup doğrulamaya çalışır; gerçek `pswpin`/`pswpout` ve `/sys/block/nbd15/stat` sayaçlarıyla sonucu doğrular.

**ÖNEMLİ RİSK:** NBD Linux çekirdeğinin genel SWAP aygıtıdır. Cgroup sınırı yalnızca test işleminin belleğini kısıtlar; NBD/SWAP arızası diğer programları da SIGBUS ile çökertip bütün masaüstünü kilitleyebilir. Bu daha önce yaşandı. Ekran kartı belleği RAM değildir. `timeout`, cgroup, izleme ve ZRAM, bağlantı kopunca NBD'ye taşınmış kayıp sayfaları kurtarmaz. Yapılmamış veri yedeklerini alın, tüm belgeleri kaydedin, Steam/Bannerlord dahil gereksiz uygulamaları kapatın. Sürücüler tekrar çökebilir. **Oyun içinde gerçek SWAP testine doğrudan geçmeyin.**

## Neden önce 512 MiB?

25 Ekim 22:50 çöküşünde GPU işlemleri 2,409 ms'den uzun değilken NBD yanıt gönderiminde takılma ve ardından 60 saniyelik kernel timeout yaşandı. Sistem RAM'i 1,3 GiB kullanılabilir düzeyine inmişti. Yeni test daha az bellek baskısıyla SWAP-OUT ve SWAP-IN yolunu doğrulamayı hedefler; arızayı engellediği kanıtlanmış bir yama değildir.

## Kurulum (ZIP veya Brave indirmesi gerekmez)

```bash
cd ~/nopen-bannerlord-debug
git fetch origin experiments/bannerlord-stability
git pull --ff-only origin experiments/bannerlord-stability

SCRIPT=experiments/bannerlord-stability/scripts/real-swap-canary.sh
bash "$SCRIPT" prepare
bash "$SCRIPT" status
```

Kodu ve kernel modülünü hazırlar; **SWAP açmaz**. Öncesinde `swapon --show` komutunda yalnızca `/dev/zram0` öncelik 100 olmalı. Bu runner, GitHub dalındaki `legacy/archives/Nopen_VRAM_NBD_v0.9.3_NBD_Stall_Diagnostics.zip` kaynaklarını açar ve özgün `make test` doğrulamalarını çalıştırır.

## Aşama A — Gerçek takası başlat (yüksek risk)

```bash
bash "$SCRIPT" start
```

Sunucu açılır, v0.9.3 betiği ekrandan **`EVET SIGBUS RISKI`** onayını ister. Onay verilirse `/dev/nbd15` üzerine `mkswap` imzası yazılır ve **öncelik 200** ile `swapon` edilir. /dev/zram0 korunur. NVMe'ye swap dosyası yazılmaz. Bağlanan `/dev/nbd15` üzerine başka dosya sistemi veya I/O yönlendirmeyin.

## Aşama B — Tam 512 MiB'de bir sayfa döngüsü

```bash
bash "$SCRIPT" test
```

Kontrollü belleğe ayrılan alan 512 MiB; test işlemine cgroup `MemoryMax=384M` ve `MemorySwapMax=768M`. Beklenen günlükler: `pageout_confirmed`, `pagein_counters`, `cycle_pass` ve `TEST_PASS`. `TEST_INCONCLUSIVE` veya `FAIL` çıkarsa geçildi sayılmaz. Sunucu hâlâ bağlıyken GPU sayfaları sistemin diğer süreçlerine de atanabilir. Günlük `~/nopen-vram-experiment/nbd-beta-093-canary/canary-logs` altına kaydedilir.

## Aşama C — Önce SWAP'i kapat, sonra sunucuyu

```bash
bash "$SCRIPT" stop
bash "$SCRIPT" status
```

Bu aşama NBD takasını **önce `swapoff` ile RAM'e geri alır**, sonra servisi kapatır. `swapoff` hatayla çıkarsa veya takılırsa **`kill -9` çalıştırmayın, systemctl stop ile NBD sunucusunu kapatmayın, modprobe -r nbd çalıştırmayın**. Kernel beklemelerini izleyin; böyle bir durumda tüm oturum zaten risk altında olabilir. Test sonucunu ve günlükleri paylaşın.

## Hata, donma veya yeniden başlatma ardından

```bash
bash "$SCRIPT" report
```

Özgün v0.9.3 teşhis arşivini oluşturur; paket yolunu yazdırır. `canary-logs/` ile beraber incelenmelidir. Günlükler kullanıcıların süreç adlarını ve bazı bellek adreslerini gösterebilir: herkese açık paylaşmadan önce kontrol edin.

Bu canary **v0.9.3'ün NBD çekirdek kilitlenmesini çözdüğünü iddia etmez**. Başarılı olursa sıradaki aşama, aynı altyapıya kalıcı güvenilirlik iyileştirmeleri uygulamak ve gerçek sayfa hatalarını Bannerlord üzerinden sınırlı şekilde ölçmektir.