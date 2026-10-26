# Nopen VRAM-SWAP — 8 GiB deney profili (v0.9.3 tabanı)

**Deneysel ve kararsız.** RX 9060 XT üzerindeki 16 GiB VRAM'in 8 GiB'sini Nopen'a tahsis eder. Önceki gerçek SWAP testleri 2 GiB aygıtla başarılıydı; **8 GiB kapasite ile canlı Linux/GPU testi henüz yapılmadı**. Kaynak kodu/shell betikleri yerel sözdiziminden geçti, AMD GPU ortamında bu yeni profil doğrulanmadı. Burada 8 GiB sistem RAM'in üzerine ayrıca 8 GiB fiziksel RAM eklenmez. NVMe/SSD swapı oluşturulmaz; yalnızca `/dev/zram0` ve yüksek öncelikli `/dev/nbd15` kullanılır.

## RAM'den dolayı oyun neden kapanabilir?

SSD takası bulunmaması tek başına oyunun çöktüğünü kanıtlamaz. 7,3 GiB ZRAM mantıksal swap alanı gerçek sıkıştırılmış RAM'de tutulur. ZRAM dolarsa veya host bellek kullanımı çok artarsa Linux OOM-killer devreye girebilir. Daha önceki Bannerlord oturumunda NBD bağlantı hataları ve `SIGBUS` vardı; bu farklı ve ciddi bir problem. OOM kanıtı için `journalctl -k -b | grep -Ei 'out of memory|oom-kill|Killed process|memory cgroup'` kontrol edilebilir.

## Güvenlik ve kapsam

- ZRAM önceliği 100 kalır. NBD swap önceliği 200 olur. `swapon` **küresel** olduğu için cgroup sınırları NBD arızasına karşı masaüstünü korumaz.
- Eski Nopen backend ile aynı anda başlatılmaz. `start` açık Bannerlord işlemi algılarsa reddeder.
- GPU'da 8 GiB kapasite ayırdığı için oyuna ayrılabilen GPU VRAM azalır. İlk deneyleri oyun kapalıyken yap.
- 8 GiB takas doluyken `swapoff` başarısız veya çok uzun olabilir; backend'i **kesinlikle** swapoff tamamlanmadan durdurma/öldürme. Sistem yanıt vermezse veri kaybı ve yeniden başlatma riski vardır.
- Kaynak arşivi `legacy/archives/Nopen_VRAM_NBD_v0.9.3_NBD_Stall_Diagnostics.zip` dokunulmadan kalır. `build-profile.py` yalnızca ayrı `~/nopen-vram-experiment/nbd-8g-experiment/backend-8g` kopyası oluşturur.

## Hazırlık (SWAP açmaz)

```bash
cd ~/nopen-bannerlord-debug
git fetch origin experiments/real-swap-8g
git switch --track origin/experiments/real-swap-8g
BASH=experiments/real-swap-8g/real-swap-8g.sh
bash "$BASH" prepare
bash "$BASH" status
```

Varsayılan çalışma klasörü `~/nopen-vram-experiment/nbd-8g-experiment` olur. Başarıyla derlenmiş 8 GiB profilinin kaynaklarını buradan görebilirsin.

## 8 GiB kapasiteyi etkinleştirme — yüksek risk

**Önce bütün belgelerini kaydet.** Oyun ve Steam kapalı olmalı. İlk açılış anında en az 3,5 GiB `MemAvailable` ve RX 9060 XT üzerinde en az 12 GiB boş VRAM gerekiyor. Yetersiz VRAM veya başka swap varsa işlem reddedilir.

```bash
bash "$BASH" start
```

Manuel `EVET SIGBUS RISKI` onayı zorunlu. Başarı durumunda `swapon --show` `/dev/nbd15` için 8G, öncelik 200 gösterir.

## Adım 1 — Gerçek 512 MiB swap canary

```bash
bash "$BASH" test
```

Bu gerçek `MADV_PAGEOUT` ve sayfa hata yolunu kullanarak 512 MiB'yi doldurur, takasa çıkarır, geri okur ve doğrular. İzole testte `MemoryMax=384M` ve `MemorySwapMax=768M` vardır. Başarı şartı `TEST_PASS`.

## Adım 2 — Toplam 8 GiB gerçek SWAP trafiği

**Yalnızca Adım 1 geçerse:**

```bash
bash "$BASH" test-8g
```

16 ayrı 512 MiB bellek döngüsü = **8 GiB kümülatif işlenen ve doğrulanan sayfa verisi**. Bunlar eşzamanlı 8 GiB RAM yükü değildir! Her döngü `TEST_PASS` üretmeli; başarısızlıkta işlem durur. `pswpout` ve NBD `read/write` sayaçlarıyla gerçek aktarım ayrı gözlemlenir. `swapcached` yüzünden 8 GiB doğrulama, 8 GiB fiziksel `NBD READ` anlamına gelmez. 8 GiB aynı anda basmak sistem çapında OOM/NBD deadlock riskini artırdığı için bu profil bunu otomatik yapmaz.

## Adım 3 — Temiz kapatma

**Test sonrası:**

```bash
bash "$BASH" stop
bash "$BASH" status
```

`stop`, v0.9.3 resmi scriptiyle **önce swapoff**, sonra backend/monitor kapatma uygular. `swapoff` başarısızsa backend'i öldürme; hata raporu topla ve mevcut durumu incele.

## Bannerlord ile deney (yalnızca canary ve kümülatif testler geçerse)

Nopen hâlâ deneysel, bilinen NBD stall riski düzeltilmedi. Oyun testini yalnızca kritik dosyaları kaydettikten sonra, önce aynı GPU/VRAM kullanımını izleyecek biçimde düşün. Oyunla birlikte 8 GiB VRAM tahsisi VRAM taşmasına ve FPS düşüşüne yol açabilir.

```bash
bash "$BASH" start
bash "$BASH" watch
# Ayrı terminalde Bannerlord'u aç; dünya haritasına yükle, kaydettiğin oyuna devam et.
# Oyun kapandıktan ve swapoff için MemAvailable yeterli olduğundan emin olduktan sonra:
bash "$BASH" stop
bash "$BASH" report
```

Oyun kilitlenirse zorla servis durdurma; yeniden başlatmadan sonra günlükleri toplayalım. Gerekirse `systemctl`/NBD/takas katmanındaki bellek geri kazanımını daha doğru mimariyle değiştireceğiz; profil bunu henüz çözmüş değildir.

## Teşhis dosyaları

`~/nopen-vram-experiment/nbd-8g-experiment/reports/` ve `nopen-bannerlord-8g-report-*.tar.gz`. Kritik servis kayıtları `/var/log/nopen-vram-nbd-v093.log` ile `/var/log/nopen-vram-nbd-v093-telemetry.log` içinde olabilir.