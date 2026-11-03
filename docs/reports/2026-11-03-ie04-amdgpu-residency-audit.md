# İE-04 | Fiziksel VRAM yerleşimi ve GTT migrasyonu incelemesi

**Nopen VRAM SWAP · P0 · İş Emri [#22](https://github.com/nopenstudio/nopen-vram-swap/issues/22) · 3 Kasım 2026**

**Temel:** `develop/v0.8.5` @ `7c4c4a25bb4455a083de9af8b4b82f7a8b159814`  
**Hedef:** v0.9.0 (henüz yayınlanmadı) · **`KERNEL_SWAP_BLOCKED`**

## 1. Ön bulgu ve mevcut kanıt

**OpenCL buffer'larının fiziksel VRAM'e sabitlenmesi kanıtlanmadı.** `v012/src/nopen_store.c:86–128` üzerinden `clCreateContext`, `clCreateCommandQueue`, `clCreateBuffer`, opsiyonel `clEnqueueFillBuffer` ve `clFinish` ile belleğin erişilebilir olduğu doğrulanabilir; bu API çağrılarının **AMDGPU TTM BO pin count'u, VRAM-only placement ve her koşulda evict edilemez olma garantisi** mevcut kaynakta yoktur.

Linux amdgpu bellek alanları ayrı: **VRAM = yerel GPU belleği**, **GTT = GPU erişimli sistem RAM'i**. AMDGPU sürücüsü `amdgpu_bo_pin(bo, domain)` ile kernel BO pinleyebilir; kernel belgelerine göre bu, sayfaları fiziksel bellekte sabit adreste tutan kernel içi işlevdir. **Nopen OpenCL üzerinden bu çağrıya sahip değildir.** Kernel içi fonksiyonun var olması, herhangi bir OpenCL buffer'ını sürücü dışından güvenli pinleyebileceğimiz anlamına gelmez.

Kaynaklar:
- [AMDGPU driver core, memory domains ve amdgpu_bo_pin](https://kernel.org/doc/html/v6.16/gpu/amdgpu/driver-core.html)
- [DRM client fdinfo resident/total/shared semantics](https://docs.kernel.org/gpu/drm-usage-stats.html)
- [AMDGPU modül parametreleri (vramlimit test parametresi)](https://kernel.org/doc/html/latest/gpu/amdgpu/module-parameters.html)

`amdgpu.vramlimit`, `gttsize` veya BO accounting okumaları **Nopen'a özel fiziksel 4 GiB rezervasyon değildir**. Modül parametresi değiştirmek ana makinede kabul edilebilir bir test değil.

**Eski gerçek donanım kanıtı:** [31 Ekim RX 9060 XT raporu](2026-10-31-rx9060xt.md) 1 GiB OpenCL tam checksum readback PASS, gözlemde global VRAM 763 → 1.957 MiB, GTT 75 → 81 MiB. Holder bitiminden hemen sonraki VRAM 1.931 MiB; ertesi gün 399–426 MiB gözlendi. **Aynı oturumda release süresi, BO migration, eviction ve fiziksel pinlama kanıtlanmadı.** 31 Ekim'deki 4 GiB kernel swap + Bannerlord host freeze'in kök nedeni hâlâ belirsiz.

## 2. Ölçüm katmanları

| Katman | Kaynak | Ölçülebilen | **Ölçülemeyen / yanlış iddia** |
|---|---|---|---|
| Global GPU | `/sys/bus/pci/devices/<BDF>/mem_info_vram_total`, `mem_info_vram_used`, `mem_info_gtt_used` | O anki global VRAM/GTT kullanım miktarı | Nopen'a münhasır kullanım; tek buffer'ın taşınma geçmişi |
| DRM istemci | `/proc/<pid>/fdinfo/<fd>` içindeki `drm-pdev`, `drm-client-id`, `drm-resident-vram/gtt` | Erişilebilen istemcinin bölgesel resident kullanım muhasebesi | Tek OpenCL BO kimliği; atomik nesne migrasyonu; fiziksel pin |
| DRM total | `drm-total-vram/gtt` | İstenen tüm buffer boyutları, backingi henüz kurulmamış olsa bile | Resident sayfa veya pin; `total == resident` varsayımı |
| Driver/TTM per BO | Araştırılacak kernel TTM/AMDGPU trace / doğrulanmış BO placement | Belirli BO için VRAM/GTT transition, fence, eviction nedeni | Güvenli biçimde bu kaynağa erişim henüz tasarlanmadı |
| Pin / rezervasyon | Yalnızca sürücü destekli açık sözleşme | BO'nun yalnız VRAM domain'inde sabit kalması (belirli koşullarda) | Kernel swap I/O ilerlemesi ve GPU reset sonrası veri kurtarma |

**DRM fdinfo açıklamasına göre `drm-total-*` requested storage, `drm-resident-*` instantiated backing, `drm-memory-*` ise AMDGPU'nun eski alias'ıdır.** Toplamlar ortak buffer'lar nedeniyle birden çok istemcide görünür. İstemci kayıtları görünmüyorsa GPU belleği yok anlamına gelmez.

## 3. Yeni gözlemci ve güvenlik sözleşmesi

`tools/ie04_residency_observe.py` dış süreç **başlatmaz**, root istemez, GPU bellek ayırmaz, debugfs/driver yazmaz, `swapon`/ublk/NBD kullanmaz. PCI adresi açıkça seçilir, proc starttime ile PID yeniden kullanımı kontrol edilir. `/proc/swaps` üzerinde ublk/NBD etkin görünüyorsa bütün ölçüm reddedilir.

Örnek, mevcut RX 9060 XT PCI adresi doğrulandıysa:

~~~sh
python3 tools/ie04_residency_observe.py --pci 0000:03:00.0 --samples 10 --interval-seconds 2
~~~

Mevcut **zaten çalışan** bir kullanıcının AMDGPU/DRM istemcisini ayrı gözlemek:

~~~sh
python3 tools/ie04_residency_observe.py --pci 0000:03:00.0 --pid 12345 --samples 10 --interval-seconds 2
~~~

`12345` yer tutucudur, PID rastgele kullanılmamalıdır. OpenCL worker bazı kombinasyonlarda AMD KFD üzerinden DRM fdinfo sunmayabilir. Bu durumda `NO_MATCHING_DRM_CLIENT` ölçüm **boşluğu**, sıfır kullanım kanıtı değildir.

**"Olası hareket" kriteri (ölçüm işareti):** Aynı `drm-client-id` için ardışık iki örnekte resident VRAM **1 MiB'den fazla azalır** ve resident GTT **1 MiB'den fazla artarsa** `CORRELATED_COUNTER_CHANGE_NOT_BO_MIGRATION_PROOF` kaydı üretilir. Bu durum gerçek bir BO'nun taşındığını bile kanıtlamaz; örneğin eşzamanlı tahsis/serbest bırakma aynı sonucu verebilir. İşaret oluşmaması da BO hiç taşınmadı veya pinli demek değildir. Global VRAM delta hiçbir zaman Nopen'a kesin bağlanmaz.

**Sayaç kaybı, başka GPU, yanlış PCI, hatalı sysfs, PID yeniden kullanımı ve swap gate** ayrıdır. Hiçbir JSON çıkışı `physical_vram_pinning_proven=true` üretmez.

## 4. Sürücü düzeyinde olası mimariler

### A. OpenCL soft holder / warmup
Mevcut kaynak. **İşe yaradığı kanıtlı:** logical OpenCL readback ve 1 GiB deney. **Eksik:** hard reservation, eviction engeli, game GPU budget. Ürün pinleme çözümü **olamaz**.

### B. Userspace DRM/GEM belirli domain placement
GEM/amdgpu BO tahsisinde tercih edilen domain VRAM olabilir. Ancak *preferred domain* ve başlangıçta VRAM accounting görülmesi, TTM/AMDGPU eviction veya GTT migrasyonunu kalıcı olarak engellemeye yeterli değil. Güvenli proof-of-concept yalnız ayrılmış, geri alınabilir **GPU-only test ortamında**, her BO için domain/migration evidence ile yürütülebilir. Desteklenmeyen kullanıcı alanı pinleme ioctl'u icat edilmeyecek.

### C. NopenOS'a özel kernel/AMDGPU entegrasyonu
Kernel sürücüsünün `amdgpu_bo_pin` veya destekli eşdeğeri ile VRAM-domain rezervasyonu, GPU buffer erişimi, diğer istemcilere görünür headroom ve reset sonrası release politikası **ayrı tasarım ve geniş test** gerektirir. Driver internal API'leri kararlı, genel userspace ABI değildir. **Pinleme yalnız residency'yi çözebilir; CPU↔GPU swap-in/out sırasında AMDGPU runtime/GFP/reclaim döngüsünü çözmez.** Kernel module/driver yaması İE-05/İE-10 güvenlik kapıları tamamlanmadan ana PC'ye kurulmayacak.

### D. Bağımsız SSD authority + VRAM cache
[ADR-003](../adr/ADR-003-acknowledged-swap-page-protection.md) kapsamında seçilen veri bütünlüğü mimarisi. Cache taşınır veya GPU reset olursa immutable SSD commit veri kaybını önleyebilir, **ama** VFS/ublk/I/O reclaim ilerleme garantisi için İE-05 ayrı. Sürücü pinleme olmasa dahi cache-only olarak tasarlanabilir; bu, VRAM'nin garanti edilmiş 4 GiB rezervasyon olduğu anlamına gelmez.

## 5. P0 kabul matrisi

| Kabul talebi (#22) | Güncel | Gereken sonraki kanıt |
|---|---|---|
| AMDGPU/DRM global ve istemci ölçümleri | **Araç ve sentetik test geliştirildi** | Gerçek PCI'de aynı-session read-only log (henüz toplanmadı) |
| GTT geçiş / eviction testi | **Yalnız korelasyon fixture'ı** | Driver düzeyinde tek BO kimliği + migration/eviction trace ve açık kanıt |
| Non-evictable VRAM pin veya eşdeğer güvenlik | **KANIT YOK** | Driver/API destekli domain, pin_count, lifetime/fail invariants; veya cache-only gereksinimini açıkça kaldırma |
| Başarısız tahsis | Preflight headroom ve mock guard **önceki v085** | Gerçek OpenCL/driver ENOMEM/eviction sonrası fail-closed ve reset |
| Oyun GPU bütçesi | `amdgpu.vramlimit` test limitidir; henüz bölme yok | İzole testte gerçek oyunla GPU pressure + headroom raporu |
| Gerçek kernel reclaim/deadlock | **Önceki freeze açıklanmadı** | İE-05 ve İE-10 ayrı kanıt. Live SWAP yasak |
| İE-04 tam kapanış | **HAYIR** | Yukarıdaki kanıtların tamamı veya açıkça onaylanan cache-only revize kapanış kriterleri |

**Bu PR yalnızca salt-okunur ölçüm/analiz altyapısını teslim eder. İE-04 P0 sorunu AÇIK kalır.**

## 6. Test planı ve sonuçların ayrımı

Otomatik fixture regresyonu:
- Bir DRM client'ın çoklu fd'leri bir kez sayılır, diğer GPU'nun fd'si atılır.
- `drm-total-vram` ve `drm-resident-vram` ayrı gösterilir.
- VRAM azalırken GTT artışı yalnız korelasyon sayılır.
- Missing DRM clients, PID reuse ve aktif kernel ublk/NBD swap fail/unknown ayrılır.
- Source CPU/mock testleri GPU kullanmaz. Test sonucundan fiziksel pin, allocation veya migration iddiası üretilemez.

~~~sh
cmake -S . -B build-ie04 -DNOPEN_OPENCL=OFF
cmake --build build-ie04 --parallel 2
ctest --test-dir build-ie04 -R '^ie04_' --output-on-failure
~~~

Fiziksel doğrulama için **önce sadece açık, salt-okunur global/DRM telemetry**, hiçbir swap veya yeni GPU tahsisi olmadan. Sonraki aşama (varsa) ayrı onay ve geri dönüş planı gerektiren GPU-only test laboratuvarıdır. Günlük makinede 4 GiB VRAM baskısı / GPU reset / modül parametresi / driver patch / `swapon` denenmez.

**Özet:** RAM ↔ VRAM ekonomi iddiası için fiziksel resident, evictability ve ileri çalışma garantileri ayrı kanıtlardır. Bugün yalnız ölçüm gözlemi ve statik API araştırması ilerledi. **KERNEL_SWAP_BLOCKED**.
