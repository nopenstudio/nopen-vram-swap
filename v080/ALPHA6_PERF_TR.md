# v0.8.0-alpha.6: 4 GiB gerçek SWAP darboğaz düzeltmesi

## Neden?

Önceki 4 GiB alpha.5 testi gerçek Linux kernel SWAP olarak
`/dev/ublkb23` cihazını **18:08:35.408871** tarihinde başarıyla açtı
(`Adding 4194300k swap`). Fakat masaüstü kısa süre sonra yanıt
vermedi ve sistem yeniden başlatıldı. Günlükte bu olay için henüz
bir kernel WRITE timeout, AMDGPU reset veya OOM görülmedi.
Bu yüzden nedeni kesinleşmemiştir.

Kaynak koddan **kesinleşen mimari darboğaz**:

`kernel 4KiB WRITE -> tek mutex -> NVMe O_DIRECT/O_DSYNC (durable)
-> GPU OpenCL işçisine Unix-socket IPC -> yanıt bekle -> kernel ACK`

Bir 4KiB yazma, SSD kalıcı yazımı ve GPU işlemine sırayla bağımlıydı.
Alpha.5 ayrıca `swap priority 200` ile bütün yeni swapout
trafiğini zram (100) yerine Nopen'a yöneltebiliyordu.

## Alpha.6 yapılan gerçek değişiklikler

1. **Asenkron GPU kopyası, senkron güvenilir SSD yedeği**:
   32 adet 64KiB üst sınırına sahip GPU-upload işi için sabit boyutlu
   ~2MiB iş kuyruğu. Kernel WRITE onayı artık OpenCL yanıtını beklemiyor;
   fakat **SSD O_DSYNC yazması tamamlanmadan asla verilmez**.
2. **Doğruluk**: her 4KiB sayfa için sürüm numarası ve GPU-valid bitleri.
   Aynı sayfaya birden fazla yazma yapılırsa geç kalan eski GPU
   kopyası geçerli sayılmaz. GPU upload kuyruğu dolduğunda yalnızca
   isteğe bağlı GPU cache güncellemesi düşürülür; SSD yazımı asla atlanmaz.
   GPU worker kaybında daha önce onaylanmış sayfalar SSD'den okunur.
3. **Okuma yolu**: GPU boşken ve istenen bütün sayfalar GPU'da
   geçerliyken GPU'dan okunur. Aksi halde bütünlük denetimli SSD
   yedeğinden okunur. Böylece eski GPU verileri kernel'e verilmez.
4. **zram birlikte çalışma**: 4 GiB modunda Nopen SWAP önceliği
   artık 200 değil, **100**; mevcut zram 100 ise eşit öncelikli
   seçim yapılır. Bu toplam gecikmeyi azaltacağına ilişkin garanti değildir.
5. **Sınırlı telemetri**: her 8192 onaylı WRITE sonrası kuyruğa alınan,
   atlanan, tamamlanan GPU kopyaları ve arızalar; SSD 4GiB ilk
   yazım sırasında her 512 MiB ilerleme mesajı.
6. **Bütünlük testi**: 2400 ardışık/churn 4KiB yazma, arada okuma,
   gerçek SSD O_DIRECT ve 1024 sayfa GPU worker ölümü sonrası
   geri okuma. `V080_ASYNC_SHADOW_PASS` kontrolü.
   Ayrıca 4GiB kapasitede **kernel SWAP açmadan** GPU+disk
   arayüzünü benchmark eden program eklendi.

## Mevcut sınır

- SSD O_DSYNC **hâlâ her kernel WRITE için zorunlu**. Dolayısıyla
  bu mimari saf VRAM bant genişliğini sunmaz. Bu, güvenli veri
  kurtarmanın bedelidir; hızlanma rakamı sadece gerçek Radeon
  fiziksel testinden sonra ileri sürülebilir.
- GPU worker'a yüksek bellek baskısı altında daima forward-progress
  garantisi yoktur. Alpha.5 4GiB sonrası masaüstü yanıtsızlığının
  kesin kök nedeni kanıtlanmadığı için **canlı 4GiB kernel SWAP
  kararlı kabul edilmedi**. Sorun takibi: Issue #7.
- GPU-upload kuyruğu doyarsa GPU cache isabet oranı düşebilir;
  iş kuyruğunu kontrolsüz büyütmüyoruz.
- 4GiB bellek havuzu + 4GiB SSD shadow gerçek GPU'ya ayrılır ama
  sistem RAM'ine 4GiB fiziksel RAM eklenmiş olmaz.

## Önce bağımsız gerçek AMD 4GiB testi

Bu aşama mevcut kernel SWAP'ı değiştirmez, root istemez,
zram'ı kapatmaz. Bannerlord kapalı olsun.

```fish
git clone --branch develop/v0.8.0-alpha.6 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a6
cd ~/nopen-v080a6
bash v080/tools/run_alpha6_async_gpu_diagnostics.sh
```

Bu çalışma şu ölçümleri kaydeder:

- 256MiB gerçek GPU + SSD, 2000 x 4KiB **eski sync yol**.
- 256MiB gerçek GPU + SSD, 2000 x 4KiB **yeni async yol**.
- 4096MiB gerçek GPU + SSD, 2000 x 4KiB **yeni async yol**,
  ardından GPU worker kapatılıp bütün dokunulan sayfalar SSD'den
  doğrulanır.

Her testte `write_p50_us`, `write_p95_us`, gerçek işlem süresi,
`enqueued/dropped/gpu_completed` raporlanır.
Her test dosyası ayrı tutulur. 4GiB SSD shadow başlangıç
doldurması nedeniyle disk üzerinde gerçek yazım yükü vardır.

**Bu testleri geçmeden başka bir 4GiB canlı `swapon` denemesi
yapmayın.** Geçmesi de canlı swap altında kernel/AMDGPU ilerlemesi
için yeterli kanıt sayılmaz.

## Sonraki mühendislik işleri

1. Sync vs async p50/p95 ve queue-drop ile SSD-baskın/gecikme-baskın
   iş yüklerini ayırmak.
2. Gerçek kernel I/O isteği başlangıcı, disk ACK, OpenCL queue,
   GPU page-in ve `swapoff` için zaman çizgisi ve max/p95 gecikme.
3. Kurtarılabilir sayfalar için güvenli, CPU RAM'i tüketmeyen
   kernel forward-progress stratejisi; SSD zorunluluğunun performans
   bedeli ayrıca ele alınmalı.
4. Gerçek 4GiB kernel denemesine dönülecekse önce ayrı test ortamında
   bellek baskısı, GPU timeout ve işçi ölümü doğrulanmalı.
