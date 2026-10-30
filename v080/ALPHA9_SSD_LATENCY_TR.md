# Nopen VRAM SWAP v0.8.0-alpha.9: Gerçek RX 9060 XT 4KiB SSD gecikme ayrımı

## Alpha.8 donanım kanıtı (30 Ekim 2026)

```
V080_ASYNC_BENCH_PASS backend=opencl mode=async capacity_mib=256
operations=2000 verified_pages=1024
write_s=2.048 write_p50_us=1014.30 write_p95_us=1092.80
enqueued=2000 dropped=0 gpu_completed=1999
gpu_upload_avg_us=142.52 gpu_upload_max_us=2764 kernel_swap=NO
```

Alpha.7: enqueued=39, dropped=1961, gpu_completed=7; yazma
p50=825.94us, p95=848.70us, 2.000 yazma 1.683 s.

**Sonuç**: GPU kuyruğunun starvation sorunu çözülmüş görünüyor.
Alpha.8'de, 2.000 işin tamamı kuyruğa alınmış ve 1.999 kopya
tamamlanmış. Bir açık iş test bitişinde GPU worker kapatıldığında
kalabilir; veri SSD gölgesinde korunur. GPU işçisi başına ortalama
142.52us kopya süresi, kernel'e verilen 4KiB write ACK
p50=1014.30us süresinden çok kısa. Ancak bu sürelerin
farkını bütünüyle SSD'ye yüklemek doğru değildir:
kilitler, checksum, IPC ve ölçüm etkisi de dahildir.

Alpha.8 yazma süresi alpha.7'den yavaş ölçüldü:
p50 +%22.8; iki test aynı tekrar seti/SSD koşulları ile
çoklu deneme olarak yapılmadı, **tek örnekle performans
gerilemesinin kesin kaynağını belirleyemeyiz**.

## Alpha.9 eklenen ölçümler

- nopen_shadow_write içindeki gerçek O_DIRECT|O_DSYNC
  `full_pwrite` çağrısının süresi ayrı izlenir:
  `ssd_write_avg_us`, `ssd_write_max_us`, `ssd_write_samples`.
- Bench `--mode disk-only`: aynı 256MiB disk shadow ve 4KiB
  checksum/lock/ACK yolu, ama GPU worker açılışın hemen ardından
  simüle biçimde sonlandırılır. Bu, GPU cache işi olmadan
  SSD gecikmesinin güvenilir referansıdır. Fiziksel GPU reset
  yapılmaz.
- Bench `--mode async`: mevcut gerçek AMD GPU cache
  yoluyla aynı yazma iş yükü.
- CI'da SSD-only 750 yazma + sayfa bütünlüğü ve 54 test doğrulaması.
  *Kernel SWAP açılmaz*.

## RX 9060 XT üzerinde hedefli kıyas

```fish
git clone --branch develop/v0.8.0-alpha.9 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a9
cd ~/nopen-v080a9
bash v080/tools/compare_alpha9_ssd_gpu.sh
```

Sonuçta iki ayrı `V080_ASYNC_BENCH_PASS` satırı karşılaştırılır.
Hedef tek işlemde GPU vs SSD bant genişliği iddiası üretmek değil,
**gerçek kernel SWAP'ın veri yazma ACK yolunu** parçalamaktır.

## Saf VRAM bant genişliği için kritik karar

SSD O_DSYNC ACK öncesinde zorunluysa, büyük bloklarda 40+GB/s
GPU bant genişliğine rağmen kernel 4KiB swap-out gecikmesi
SSD dayanıklılık limitiyle sınırlanacaktır. Bu güvenlik özelliğini
çıkarıp GPU'ya ACK verirsek GPU reset, power loss veya worker
çöküşünde sistemin ihtiyaç duyduğu bazı swap sayfaları kalıcı
kopyaya alınmadan kaybolabilir. Bu yüzden alpha.9'da SSD
durability'den ödün verilmemiştir.

**Nopen 4096MiB gerçek kernel SWAP etkinleştirme geçmişte
başarılı oldu** (2026-10-30 18:08:35 kernel 'Adding 4194300k swap'),
ama daha sonra masaüstü yanıtsızlığının nedeni kesin olarak
anlaşılamadı. Alpha.9'daki bağımsız benchmark bu kararlılığı
kanıtlamaz ve başlatmayı tekrar önermiyoruz.
