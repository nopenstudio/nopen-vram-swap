# Nopen VRAM SWAP v0.8.0-alpha.2 — Gerçek AMD GPU 1 GiB / OpenCL optimizasyon araştırması

**Sürüm durumu:** Alpha.2, 1 GiB OpenCL belleği kullanıcı alanında sınamak için
hazırlanmış **donanım testi adayıdır**. GitHub CI sanal makinesinin ayrık AMD GPU'su
olmadığı için gerçek Radeon 1 GiB PASS/HIZ ölçümü yalnızca kullanıcının
bilgisayarında yapılabilir. Kaynak kodu/CI başarıları tek başına fiziksel
GPU testinin başarılı olduğunu kanıtlamaz.

**Kritik güvenlik:** Fiziksel kernel SWAP pilotu hâlâ v0.7'deki 64 MiB
ile sınırlı. 1024 MiB veya 4096 MiB kernel SWAP etkinleştiren komut
BU SÜRÜMDE YOK. Linux zram korunur. [Issue #7]
(https://github.com/nopenstudio/nopen-vram-swap/issues/7) açık.

## Ölçüm sorusu: 40+ GB/sn gerçekten mümkün mü?

- RX 9060 XT 16 GiB kartın üretici tarafından belirtilen **320 GB/sn
  VRAM bellek bant genişliği**, GPU'nun kendi GDDR6 denetleyicisindeki
  aktarımı ifade eder; CPU'nun PCIe üzerinden VRAM'e ulaşabildiği hızı değil.
- PCIe 4.0 x8 teorik tek yön ~15,75 GB/sn; PCIe 4.0 x16 ~31,5 GB/sn;
  PCIe 5.0 x8 ~31,5 GB/sn; PCIe 5.0 x16 ~63 GB/sn (protokol/uygulama
  yükleri hariç). Bunlar pazarlanan kart özellikleriyle değil,
  **müzakere edilmiş gerçek bağlantı genişliği/sürümüyle** sınırlıdır.
- 40 GB/sn host↔GPU isteyen bir hedef, **PCIe 4.0 x16** üzerinde
  teorik olarak bile mümkün değildir. 15 GB/sn gözlemi bazen 4.0 x8
  bağlantısının sınırına yakın olabilir; ancak önce sysfs PCIe link
  değerleri görülmelidir. Bir yavaşlığın nedeni yalnızca optimizasyon
  olamaz; GPU/PCIe/SSD/IPC sınırı ayrı ayrı hesaplanmalıdır.
- Mevcut Nopen mimarisi: kernel→ublk→userspace→O_DSYNC SSD write-through
  → IPC → GPU OpenCL. 4 KiB SWAP sayfası gecikmesi için zorunlu
  adım sayısı büyüktür. Ham VRAM bellek hızı Nopen SWAP verimi değildir.

Kaynaklar:
- AMD RX 9060 XT: https://www.amd.com/en/products/graphics/desktops/radeon/9000-series/amd-radeon-rx-9060xt.html
- PCIe bant genişliği: https://pcisig.com/sites/default/files/files/PCIe_Specification_Webinar_Rev%206_FINAL_0.pdf
- AMD OpenCL Optimizasyon Rehberi: https://docs.amd.com/api/khub/documents/VeItSxvoJNzy28rl~JwEAQ/content
- Kernel ublk mimarisi: https://docs.kernel.org/block/ublk.html

## Alpha.2 kodu: iki karşılaştırılabilir GPU veri yolu

1. **direct**: Önceki yoldaki gibi blocking OpenCL
   clEnqueueWriteBuffer / clEnqueueReadBuffer.
2. **pinned**: Deneysel CL_MEM_ALLOC_HOST_PTR (sabit 4 MiB host staging),
   clEnqueueMapBuffer → memcpy → unmap → clEnqueueCopyBuffer → clFinish.
   Okumada GPU→pinned staging kopyalanır, map/read/memcpy/unmap/finish
   sırası izlenir. Böylece sürücü sık sayfa kilitleme/yapılandırma
   maliyetini azaltabilir; **map/unmap ek maliyeti nedeniyle 4 KiB'de
   yavaşlaması da olasıdır**. Yanlış/başarısız CL işlemleri sessizce
   direct moda düşmez. Bu yol sadece NOPEN_V080_IO_MODE=pinned veya
   testte açık seçim ile açılır; üretim için varsayılan DIRECT korunur.

Her iki yol aynı nopen_store_* API'sini kullanır; benchmark bir başka
uydurma GPU aktarım yöntemi kullanmaz.

## Kabul kapıları

| Kontrol | Kriter |
|---|---|
| Alpha.1 değişmezleri | 1 → 2 → 4 GiB mock+SSD sıralı CI ve eski regresyonlar |
| GPU-only kapasite | Gerçek AMD GPU'da tam 1024 MiB, **önce bütün alanı yaz, sonra ayrı geçişte tüm alanı oku** ve tüm baytları doğrula |
| GPU-only hız | 4 / 64 / 1024 / 4096 KiB için direct ve pinned modunda MiB/sn + p50/p95 |
| Gerçek Nopen yolu | Ayrı 64 MiB denemede gerçek GPU worker + IPC + O_DSYNC SSD shadow; 4/64 KiB yaz/oku p95, doğrulama, worker ölümü sonrası kurtarma |
| GPU/OS güvenliği | Normal kullanıcı, yalnızca ayrık AMD OpenCL, 1 GiB GPU kapasite sınırı, en az 4 GiB VRAM headroom, ≥1 GiB MemAvailable, ublk/NBD SWAP aktifken reddet |
| Kernel SWAP | **Yok.** 64 MiB üzeri pilot yok, zram değiştirilmez |

**Kabul kararı:** Benchmark yüzde artışları ölçer, fakat tek A/B çalışması
nedensel bir hızlanma kanıtı değildir. Pinned performansı 4 KiB/64 KiB
gecikmesini kötüleştiriyorsa bulk throughput artsa bile swap yolunda
kullanılmayabilir. Karar için birkaç tekrarlı kontrollü donanım deneyi
gereklidir. 40 GB/sn hedefinin mantıklı olup olmadığını PCIe bağlantısı
belirler. 1 GiB saf GPU kapasitesinin doğrulanması 1 GiB kernel swap
testini **yetkilendirmez**.

## Garuda Linux / fish üzerinde fiziksel 1 GiB test

Önce Bannerlord'u kaydet ve kapat. GitHub güncellemeleri ve test sırasında
sistem kilitlenmesi olasılığı tamamen sıfır değildir; önemli açık dosyalarını
kaydet. 1 GiB ayrık GPU tahsisi oyun/masaüstü VRAM alanını azaltır.

~~~fish
git clone --branch develop/v0.8.0-alpha.2 --single-branch \
    https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a2
cd ~/nopen-v080a2
swapon --show
bash v080/tools/run_alpha2_gpu.sh
~~~

**Bu komutta sudo kullanma.** Script:
1. Gerçek GPU/NBD swap etkinse veya 512MiB+ Wine Starter/Bannerlord varsa
   REDDEDER. Etkin fiziksel swapı asla kendisi durdurmaz.
2. OpenCL ile derler ve tüm mevcut regresyonları çalıştırır.
3. 1024 MiB direct ve pinned GPU yolunda iki ayrı *tam 1 GiB* tutarlılık
   ve 4KiB..4MiB hız ölçümü yapar (CPU staging toplam ~8 MiB).
4. 64 MiB **gerçek SSD gölge yedeği + OpenCL GPU işçisi** ile ayrı direct
   ve pinned 4 KiB/64 KiB deneylerini yapar; GPU işçisi sonlandırıldığında
   SSD kurtarmasını doğrular. Bu da kernel swap değildir.
5. Benzersiz rapor dizinine logları, PCIe link bilgisini ve
   comparison.json dosyasını yazar. Eski logların üzerine yazmaz.

Raporlar: direct.log, pinned.log, comparison.json, shadow-direct.log,
shadow-pinned.log, metadata.txt. Bu dosyaların çıktısına göre **bir
sonraki iyileştirmeyi seçmek** mümkün olur.

Bütün testleri geçmiş bu sürümün bulgusu şu değildir: "1 GiB swap artık
kullanıma hazır". Bu sadece gerçek donanım non-swap deneyine hazırlanmış
kod demektir.

## Bannerlord kapısı

1. Alpha.2 gerçek AMDGPU testleri PASS ve failover testi PASS.
2. 4KiB/64KiB gecikmesi/SSD write-through overhead kabul edilebilir;
   karşılaştırma güvenilir tekrarlarla doğrulanır.
3. Oyun sırasında bellek, VRAM ve FPS/p99 frametime ölçümü yapılır.
4. **v0.7'nin yalnızca 64 MiB canlı kernel pilotunun** tehlike ve yararları
   ayrı değerlendirilebilir; 1 GiB kernel swap ancak sonraki sürümlerin
   tamamlanmış güvenlik testlerinden sonra açılabilir. 8GB RAM'li sistemde
   1 GiB GPU-only benchmark, oyunun RAM'ini artırmaz.

## Bilinen açık riskler

- GPU'nun GDDR6 320GB/sn'si ile CPU PCIe bant genişliği ayrı ölçütlerdir.
- Nopen disk shadow tamponu/sistem I/O'su ve IPC seri; Write-through
  mimarisinde disk yazmadan GPU'ya ACK verilmez.
- Kernel reclaim sırasında userspace servisin işlem görebilme garantisi
  ve GPU reset sırasında kilitlenme riski henüz ortadan kaldırılmadı.
- Pinned staging host RAM'in bir bölümünü kilitleyebilir; oyun sırasında
  kapasite ve frametime karşılaştırılmadan varsayılan yapılamaz.
- GPU benchmarkının p50/p95 süreleri gerçek 1 GiB **sürekli bant genişliği**
  anlamına gelmez; rapor aktarım boyutlarına göre ayrıdır.
