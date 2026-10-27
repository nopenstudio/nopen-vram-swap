> **2026-10-27 güncellemesi:** Bu dokümandaki aşama tablosu eski alfa.2 araştırma planından kalmıştır. Gerçek AMD donanımında alfa.1–4 kullanıcı alanı testleri geçti. Güncel alfa.5 [taşınabilirlik ve bellek ekonomisi notları](PORTABILITY_AND_SWAP_TR.md) içerisinde. **NopenOS henüz geliştirilmedi, global Linux SWAP yok.**

---

# Nopen VRAM-SWAP: GERÇEK LINUX SWAP hedefi — karar belgesi

**Sürüm:** 0.12.0-alpha.2 — araştırma ve mimari koşulları.

## Kabul kriteri: sistem takası hâlâ nihai hedeftir

Nihai ürün `userfaultfd` demolarıyla sınırlı olmayacak. Özellikle NopenOS üzerinde Linux çekirdeğinin anonim sayfaları GPU VRAM'e taşıdığı ve bir sayfa hatasında VRAM'den geri getirdiği **gerçek bir takas yolu** araştırılacak. Bunun gerçekleştiğini ancak Linux VM tarafından üretilen ve geri alınan sayfalara ait hata/bütünlük ve ileri-ilerleme testleri gösterebilir. `nopen-vram-managed` böyle bir sistem takas yolu **değildir**.

## NBD'nin yerine ne geliyor?

**Aşama A — bellek havuzu, süreç içi (v0.12.0-alpha.2):**
- OpenCL ile ayrık GPU'daki önceden tahsis edilmiş depo; 4KiB fiziksel işlem birimi.
- Mantıksal sayfa tablosu: ZERO, RAM, GPU.
- Sınırlı CPU resident sayfa havuzu, LRU seçimli kooperatif eviction.
- GPU okuması checksum ile doğrulanmadan RAM sayfası değiştirilmez.
- GPU yazımı başarısızsa CPU'daki son geçerli sayfa korunur.
- `madvise(MADV_DONTNEED)` yalnızca bu sürecin açıkça kontrol ettiği RAM sayfasını serbest bırakmak için kullanılır.
- **NBD ve Linux global SWAP açıkça yasak.**

**Aşama B — uygulama bağdaştırması (sonraki sürüm):**
- v0.12-alpha.1 `userfaultfd` kontrolü ve alpha.2 sayfa tablosunu birleştirmek.
- Kirli sayfa izleme, nesne/arenayı yaşam süresince koruma, eşzamanlı erişim kontrolü, iptal edilebilir GPU transferleri.
- Tek süreçte gerçek RSS kazanımı ve uzun süreli stres testleri.

**Aşama C — NopenOS çekirdek yolu (ayrı ve açıkça deneysel sürüm):**
- Çekirdek sürümüne bağlı bir `nopenmem` swap/folio deposu entegrasyonunun prototipi. Linux SWAP ABI'si özel bir modüle kararlı API sunmuyor; bunu tek başına bir `ioctl` ile yapmak mümkün değil.
- Güvenli yol, NopenOS'un kendi çekirdek kaynak ağacına, Linux VM/swap altyapısına ve amdgpu/DRM/PCIe DMA sürücüsüne uygun entegrasyonu incelemek. Alternatif olarak zswap tarzı swap-cache frontend üzerinden GPU-tier araştırılabilir; zswap'ın da backing-swap/failure semantiği çözülmelidir.
- **Linux reclaim sırasında user-space NBD servisine ihtiyaç duyulmayacak.** Page-in sayfası GPU'da iken GPU sürücüsü de reclaim beklerse benzer bir deadlock ortaya çıkabilir. Önceden ayrılmış DMA tanımlayıcıları, komut havuzları, locked staging buffer'lar ve acil bellek rezervleri ile bekleme zinciri incelenmeden bu modül aktif swap olarak denenmez.
- İlk kernel testi QEMU sanal makine / emüle GPU deposunda; sonra yalnızca veri yitirilse bile OS ayakta kalacak izole test sürecinde.

## Değiştirilemez fiziksel sınır: GPU reseti

VRAM, GPU sıfırlanması/sürücü çökmesinde kaybolabilir. Eğer sayfanın **tek kopyası** GPU'daysa, hiçbir kernel algoritması kayıp veriyi kendiliğinden geri oluşturamaz. Bu yüzden sistem genelinde kritik bellek için birinin gerçekleşmesi zorunlu:

1. **Güvenilir ayna/yedek:** depolanan her sayfayı geri kazanabilecek bağımsız güvenilir ortam. SSD/NVMe yalnızca arıza geri dönüşü için kullanılması düşünülebilir ama bu ek I/O ve kapasite bedeli getirir; aynayı RAM'de tutmak ise net RAM kazancını ortadan kaldırır.
2. **Süreçle sınırlı, kaybedilebilir takas:** yalnızca bilinçli olarak seçilmiş, başarısızlıkta öldürülebilen kullanıcı iş yüklerini VRAM'e taşı; PID1 ve çekirdek için kritikleri hariç tut. Bu, tam genel amaçlı RAM yerine sınırlı kullanım sunar.
3. **GPU sayfalarının reset sonrasında saklandığını donanım ve yazılım düzeyinde kanıtlamak:** hedef AMD kartta bu tür bir garanti henüz yok.

**Sonuç:** Yedeksiz GPU-only swap'a "PID1 asla çökmez" veya "bütün uygulamalar şeffaf devam eder" garantisi veremeyiz. 2026-10-26 panik geçmişimiz bunu göstermiştir.

## Aşamalı kabul kapıları

| Aşama | Önemli koşul | Mevcut durum |
|---|---|---|
| v0.12.0-alpha.1 | OpenCL+UFFD kooperatif sayfa geri getirme | RX 9060 XT: 768 sayfa geri dönüşü PASS |
| v0.12.0-alpha.2 | Mantıksal sayfa tablosu, sınırlı RAM, LRU, checksum, başarısızlık atomikliği | Mock testleri PASS; gerçek OpenCL managed testi bekleniyor |
| v0.12.0-alpha.3 | UFFD ile aynı yöneticiyi birleştir; uzun süreli ve eşzamanlı stres | Henüz başlamadı |
| Kernel prototipi | NopenOS `nopenmem` VM entegrasyonu ve bellek kurtarma bağımlılık analizi | Tasarım, kod yok |
| Gerçek sistem takası | Linux VM swap-out/swap-in; fault recovery; veri bütünlüğü; OOM/GPU-reset senaryoları | Henüz yok |
| Kararlı dağıtım | GPU ölürse PID1'in ve temel sistemin ayakta kalması; kontrollü fallback | Henüz yok |

## Performans

4KiB OpenCL aktarımında PCIe gecikmesi, RAM gecikmesinden çok yüksektir. Büyük ardışık DMA ile bant genişliği artırılabilir ama GPU'dan tek sayfa geri getirme gecikmesi ölçeklenmeyebilir. Basit `managed` testi gerçek GPU ile geçerse sonraki sürümde batch transfer, kuyruk derinliği, pinned staging ve farklı sayfa büyüklüklerini ayrı ayrı ölçeceğiz. "Tam hız" reklamı değil, ölçülmüş hız hedefliyoruz.

## Kaynaklar
- https://docs.kernel.org/admin-guide/mm/zswap.html
- https://docs.kernel.org/core-api/mm-api.html
- https://docs.kernel.org/admin-guide/mm/userfaultfd.html