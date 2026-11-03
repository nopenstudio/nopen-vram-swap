# Çökme Sorunu İçin Kritik İş Emirleri ve Uygulamalar
**Nopen VRAM SWAP | Stabilizasyon programı | Hedef sürüm: v0.9.0**

**Release status: NOT RELEASED. No v0.9.0 tag or v090 implementation tree exists.**

> **KERNEL_SWAP_BLOCKED.** Bu doküman açık iş emirlerinin kaydıdır. Henüz v0.9.0 kodu, etiketi, sürüm yayını veya gerçek kernel swap için güvenlik onayı yoktur. Linux'taki mevcut zram korunur.

**Resmî ana takip:** [GitHub Issue #18](https://github.com/nopenstudio/nopen-vram-swap/issues/18)  
**Referans geliştirme tabanı:** `develop/v0.8.5`  
**İlk çözüm taslağı:** [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17), ayrı dalda ve henüz merge edilmedi.  
**Tarih:** 2026-11-03

## Amaç ve güvenlik ölçütü

31 Ekim'deki gerçek **4 GiB GPU-only ublk kernel swap + Bannerlord** sistem donmasını ve 28 Ekim'deki gerçek swap WRITE timeout / READ hatalarını mimari kökenleriyle çözmek. Bir istek timeout verdiğinde eski ACK verilmiş sayfalar kayboluyorsa program **güvenli değildir**, CI geçse bile kararlı ürün sayılmaz. Kök nedeni tam kanıtlanmamış olaylar varsayım olarak işaretlenir.

## Resmî iş emirleri

| İş | Öncelik | Kapsam | Durum | Kapanış kanıtı |
|---|---|---|---|---|
| [İE-01](https://github.com/nopenstudio/nopen-vram-swap/issues/19) | P0 | Deadline ve fail-closed semantiği | [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17) mock uygulama taslağı | Tüm I/O girişlerinde deadline doğru, kuyruk reddi worker'a zarar vermez; belirsiz I/O başarılı sayılmaz |
| [İE-02](https://github.com/nopenstudio/nopen-vram-swap/issues/20) | P0 | Reclaim bağımsızlığı incelemesi | [Analiz raporu / PR #31](https://github.com/nopenstudio/nopen-vram-swap/pull/31) (İE-05 uygulama bekliyor) | Kod/sistem kaynaklı tam wait-for grafiği, lock-GFP-allocation matrisi ve belirsizlikler |
| [İE-03](https://github.com/nopenstudio/nopen-vram-swap/issues/21) | P0 | ACK verilen sayfaların korunması tasarımı | [ADR-003 tasarım kararı](adr/ADR-003-acknowledged-swap-page-protection.md) (İE-06 uygulama bekliyor) | Önceden onaylanan veriler için sağlam ACK, authoritative backing ve veri kaybı modeli |
| [İE-04](https://github.com/nopenstudio/nopen-vram-swap/issues/22) | P0 | VRAM residency ve GTT kontrolü | [Salt okunur gözlemci ve rapor](reports/2026-11-03-ie04-amdgpu-residency-audit.md), **P0 açık** | AMDGPU bellek migrasyonu ölçülür; pin/rezervasyon veya eşdeğer güvenlik kanıtı |
| [İE-05](https://github.com/nopenstudio/nopen-vram-swap/issues/23) | P0 | Reclaim bağımsız I/O uygulaması | Açık | Reclaim sırasında ublk/IPC/GPU I/O ilerleyebilir; döngüsel bağımlılıklar yok |
| [İE-06](https://github.com/nopenstudio/nopen-vram-swap/issues/24) | P0 | Worker/GPU arızası kurtarması | Açık | Arıza sonrası ACK sayfalarının doğrulanmış şekilde kurtarılması |
| [İE-07](https://github.com/nopenstudio/nopen-vram-swap/issues/25) | P1 | ublk/IPC concurrency, backlog ve gecikme | Açık | Yük altında adil kuyruklama, doğru ordering ve p50/p95/p99 raporları |
| [İE-08](https://github.com/nopenstudio/nopen-vram-swap/issues/26) | P1 | zram önceliği ve swap politikası | Açık | Hedeflenen swap dağılımı, zram korunması, ölçülebilir kullanım ve aktivasyon kapısı |
| [İE-09](https://github.com/nopenstudio/nopen-vram-swap/issues/27) | P1 | Hata enjeksiyonu, telemetri ve dayanıklılık | Açık | İlk olay kanıtı, katman bazlı test matrisi ve kurtarılabilir ortamda uzun test |
| [İE-10](https://github.com/nopenstudio/nopen-vram-swap/issues/28) | P0 | Gerçek donanım kabulü ve v0.9 sürüm kararı | Açık | Tüm P0/P1 işler kapanmış; izole donanımda veri bütünlüğü ve stres kabulü geçmiş |

**Sıralama değişmez:** İE-01 → İE-02 → İE-03 → İE-04 → İE-05 → İE-06 → İE-07 → İE-08 → İE-09 → İE-10. İE-02 analizdir, İE-05 onun düzeltmesidir. İE-03 veri koruma kararıdır, İE-06 uygulama/kurtarmadır. Bu ayrım atlanmaz.

İşlerin uygulaması bağımlılıklar nedeniyle paralel araştırılabilir; ancak **iş emri kapanışları otomatik değildir**. Kod değişikliği, PR incelemesi, doğru katmanda regresyon ve kalan risk listesi gerekir. İE-01'de mock PASS sağlanması GPU-only swap güvenliğinin tamamlandığı anlamına gelmez.

## Üç kapı

### Kapı A | Mimari ve güvenlik (İE-02..06)
- Reclaim bağımsızlığı sadece varsayım değil, desteklenebilir ve test edilmiş.
- GPU/worker kaybı durumunda önceden ACK verilmiş sayfaların kurtarılması sağlanmış.
- VRAM residency ve GTT davranışı açıkça sınırlandırılmış.
- GPU-only tasarım kabul edilemeyecek veri kaybını önleyemiyorsa bağımsız backing seçilecek veya canlı swap reddedilecek.

### Kapı B | Entegrasyon (İE-07..09)
- Kuyruk ve IPC davranışı veri doğruluğunu bozmadan ölçülebilir.
- zram önceliği ve cihaz seçim kuralları deterministik ve gözlemlenebilir.
- Arıza kayıtları ve uzun süreli hata enjeksiyonu sonuçları bağımsız saklanır.

### Kapı C | Yayın (İE-10)
- P0 ve P1 kabul koşulları tamamlanır; ayrı kurtarılabilir AMDGPU sisteminde test edilir.
- Hata anında ilk tamamlanamayan katman raporlanır; önceki swap verisi korunur.
- İşler kapatılıp bağımsız incelemeden geçtikten sonra v0.9.0 **release candidate** kararı alınabilir. `v0.9.0` etiketi ancak gerçek yayın onayında oluşturulur.

## Kanıt düzeyleri

| Etiket | Neyi kanıtlar | Neyi kanıtlamaz |
|---|---|---|
| MOCK_ONLY | Mantık, IPC hata sözleşmesi ve sentetik stres | AMDGPU sürücüsünün ilerlemesi |
| REAL_OPENCL_NO_SWAP | VRAM ile gerçek veri aktarımı ve checksum | Kernel reclaim veya pinleme |
| RAW_UBLK_NO_SWAP | Gerçek blok isteklerinin I/O zinciri | Canlı swap kullanılabilirliği |
| LIVE_SWAP_LAB | İzole sistemde gerçek swap-in/out | Her GPU/reset/oyun koşulunun güvenliği |
| RELEASE_ACCEPTANCE | Açıkça tanımlanmış risklere karşı bütünleşik kabul | Daha önce sınanmamış donanımın garantisi |

## GitHub çalışma düzeni

- **`main`**: public karşılama sayfası; mevcut tarihsel root Vulkan prototipi bulunduğundan kaynak kodu açısından güncel değildir.
- **`develop/v0.8.5`**: gerçek OpenCL araştırma tabanı, önceki sürümlerin birleşik kaynakları ve mock CI.
- **`fix/issue-10-deadline-policy`**: İE-01 kaynak kodu taslağı, [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17).
- **`chore/v0.9-crash-work-orders`**: bu düzenleme ve belgeler, kod düzeltmesi yapılmaz.
- Her yeni iş emri **ayrı odaklı PR** ve sabit İE kimliği kullanır. Önerilen dal adı: `fix/ie-02-reclaim-audit`, `fix/ie-03-page-protection`.
- Geçmiş `v012`/`v020`…`v085`, `legacy` ve `src/nopen_vulkan_probe.c` kaynakları silinmez; bazıları yeni testlerin bağımlılığıdır. Vulkan ve eski NBD güncel yeni backend rotası değildir.
- `docs/ci-history/` yalnızca tarihsel kayıt; GitHub Actions tarafından çalıştırılmaz. Yeni workflow başına sürüm türetilmez, tek aktif mock CI tercihi korunur.
- `v090/` klasörü, sıfırdan ayrı bir sürüm kökü oluşturmak için **şimdi** açılmayacak. Geçişin mimarisi olgunlaşınca tasarlanacak.

## İE-02 analiz kanıtı (3 Kasım 2026)

- [Kaynak satırlarına bağlı reclaim risk raporu](reports/2026-11-03-ie02-reclaim-source-audit.md): GFP/lock/RAM matrisi, OpenCL ve SSD-shadow için koşullu bekleme döngüleri, doğrulanmamış kernel/driver kaynakları ve İE-05'e devredilecek uygulama gereksinimleri.
- [İE-02 PR #31](https://github.com/nopenstudio/nopen-vram-swap/pull/31): 33 kaynak işareti, 3 sıralama kontrolü, mock/statik regresyon. **Gerçek reclaim bağımsızlığı veya donmanın kök nedeni kanıtlanmadı.**
- İE-02 analiz olarak kapanabilir; fiziksel reclaim forward-progress P0 sorunu **İE-05** tamamlanana dek açık ve `KERNEL_SWAP_BLOCKED` korunur.

## İE-03 ACK sayfa koruma kararı (3 Kasım 2026)

- [ADR-003](adr/ADR-003-acknowledged-swap-page-protection.md), [PR #32](https://github.com/nopenstudio/nopen-vram-swap/pull/32): **SSD authoritative COW/journal, GPU optional cache** kararı; 4 GiB maliyet ve arıza matrisi, atomic commit/ACK, same-boot replay/generation sözleşmesi.
- [İE-03 Python referans modeli](../tools/ie03_ack_model.py): sadece bellekteki journal simülasyonu; GPU, disk, kernel swap veya gerçek kriptografi içermez. **İE-06 gerçek C/storage/ublk recovery uygulaması hâlâ P0 blocker.**
- Seçilen güvenli profilin yazma yolu **SSD commit hızına bağlıdır**; GPU-only kernel swap güvenli değildir. `KERNEL_SWAP_BLOCKED` korunur.

## İE-04 salt okunur VRAM/GTT kanıt altyapısı (3 Kasım 2026)

- [İE-04 raporu](reports/2026-11-03-ie04-amdgpu-residency-audit.md) ve [PR #33](https://github.com/nopenstudio/nopen-vram-swap/pull/33): global GPU sysfs ve seçili PID'nin DRM resident/total GTT/VRAM göstergeleri, PID reuse ve yetki/görünürlük sınırlamaları.
- Kaynak ve fixture testleri **physical pin, BO migration veya game memory budget kanıtı değildir**. Olası VRAM↓/GTT↑ korelasyonu gerçek BO taşınması olarak etiketlenmez.
- İE-04 **P0 açık kalır**: fiziksel per-BO/TTM migrasyon kanıtı, desteklenen non-evictable mekanizma veya cache-only güvenlik kriterleri, GPU pressure ve allocation failure testleri henüz yapılmadı. `KERNEL_SWAP_BLOCKED`.

## Olay ve test arşivi

- [Issue #7](https://github.com/nopenstudio/nopen-vram-swap/issues/7): 28 Ekim gerçek WRITE timeout ve sonraki kernel swap hataları.
- [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10): 31 Ekim 4 GiB gerçek kernel swap + Bannerlord kilitlenmesi, ilk arıza belirsiz.
- [İE-01 / PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17): bağımsız queue admission / dispatched IPC süreleri, mock CI. **Tam arıza kurtarma değil.**
- [PR #14](https://github.com/nopenstudio/nopen-vram-swap/pull/14): v085 VRAM Holder ve residency araştırmaları.

Bu programın kapsamı **crash stabilizasyonu ve sistem bütünlüğü**. Performans optimizasyonu, güvenlik kabulü yerine kullanılamaz. Açık bir P0 varsa v0.9.0 hazırlanmış olsa bile **yayın kapalıdır**.
