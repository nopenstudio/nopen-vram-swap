# İE-02 | Reclaim bağımsızlığı, kaynak kodu ve bekleme döngüsü denetimi

**Tarih:** 2026-11-03  
**İş emri:** [#20](https://github.com/nopenstudio/nopen-vram-swap/issues/20)  
**Geliştirme tabanı:** \`develop/v0.8.5\` @ \`d66d229375d09b83764473b02725fc6d1bb38d1e\`  
**Bağlı iş emirleri:** İE-01 #19 (IPC deadline, [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17)), İE-03 #21 (ACK sayfası koruması), İE-05 #23 (uygulama), İE-06 #24 (kurtarma).  
**Sürüm:** v0.9.0 hedef, **henüz yayınlanmadı**. **Güvenlik: KERNEL_SWAP_BLOCKED**.

## Karar özeti

**Reclaim bağımsızlığı henüz sağlanmış/kanıtlanmış değil.** Kaynakta Linux kernel swap isteği ile userspace I/O callback'i, IPC, GPU worker, blok metadata mutex'i ve OpenCL sürücüsünün blocking çağrıları birbirine bağlı. Eğer GPU sürücüsü veya dosya sistemi bu isteği tamamlarken aynı bekleyen reclaim yoluna muhtaç olursa döngü oluşabilir. **Bu son şart gerçek cihazdaki bir stack trace ile doğrulanmadı**; daha önceki host donmasının kanıtlanmış kök nedeni değildir.

Bu iş emri bir **analiz ve doğrulanabilir bağımlılık envanteridir**, İE-05 kapsamındaki implementasyon değildir. Analizin statik/mok testlerinin geçmesi; memory-pressure liveness, GTT migrasyon kontrolü, GPU reset kurtarması veya Linux SWAP kullanılabilirliği anlamına gelmez.

## Olaylardan kanıt ayrımı

- **28 Ekim / [Issue #7](https://github.com/nopenstudio/nopen-vram-swap/issues/7):** Gerçek \`ublkb23\` WRITE timeout zaman damgası **21:25:14.462**, ardından WRITE/READ I/O hataları ve oturumun dağılması kaydedildi. Hata öncesinde AMDGPU resetinin meydana geldiğine dair kanıt yok. GPU servisinin GUI oturumu kapatılırken öldürülmesi bir diğer tehlikeydi, ancak **ilk WRITE timeout'un nedeni olamaz**.
- **31 Ekim / [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10):** 256 MiB gerçek ham ublk **swap etkin değilken** 165 istek PASS. 4 GiB gerçek ublk **swap + Bannerlord** sonrasında masaüstü dondu. En son swap sayaçları 0 KiB gösteriyordu, fakat donmaya kadar kesintisiz kayıt yok. İlk kernel/worker/GPU arızası yakalanamadı. Oyunun çökmesi ile kernel-swap reclaim deadlock'u aynı olay olarak kanıtlanmadı.
- **[PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17)** ayrı bir dalda IPC kuyruğu ve gönderilmiş istek deadline ayrımını başlattı. Bu denetim **birleşmiş geliştirme tabanını** inceler; PR #17 henüz \`develop/v0.8.5\` içine alınmadı.

## 1. Reclaim → ublk → worker veri yolu

\`\`\`mermaid
flowchart TD
    K["Linux reclaim / swap-in request"] --> U["ublk kernel queue"]
    U --> A["libublksrv on_io callback"]
    A --> I["Nopen isolated IPC (parent mutex)"]
    I --> B["Worker: block metadata mutex"]
    B --> C["OpenCL CL_TRUE read/write"]
    C --> D["AMDGPU driver + GPU scheduling"]
    D -. "KOŞULLU: host allocation/reclaim gerekirse" .-> K
\`\`\`

**Düz çizgiler kaynak kodu ve Linux ublk sözleşmesiyle uyumlu çağrı/bekleme bağımlılıklarıdır. Kesik çizgi varsayımdır.** ublk \`handle_io_async\` callback'inin gerçek libublksrv I/O thread'lerini ne kadar bloke ettiği ayrı şekilde incelenmelidir. Bugünkü kaynakta \`ublksrv_complete_io\` ancak arka uç çağrısı döndükten sonra çağrılır; isminin \`async\` olması arka uç işlemini otomatik asenkron yapmaz.

### Kaynak-satır kanıtı ve GFP/kilit/kaynak matrisi

Aşağıdaki satırlar sabit denetim tabanına göredir; [otomatik envanter](../../tools/ie02_reclaim_audit.py) her çalıştırmada güncel satırları JSON'a da yazar.

| Katman / safha | Kaynak kanıtı | RAM, lock, GFP ve bloklayıcı yol | Sonuç |
|---|---|---|---|
| Kernel reclaim / swap-out ve swap-in | Linux [ublk dokümantasyonu](https://docs.kernel.org/block/ublk.html), Issue #7 | Kernel swap page-in tamamlanmadan ilgili sayfa geri alınamaz. ublk I/O tamamlaması userspace'e bağlı. **Kernel içi GFP/lock stack'leri henüz yok.** | Bağımlılık gerçek; döngü koşulu bilinmiyor |
| ublk servis başlangıcı | \`v070/experimental/ublk/nopen_ublk_swap_target.cpp:67–215\` | \`new(std::nothrow)\` başlangıçta; queue depth libublksrv'den alınıyor; 1 kuyruk/16 depth launcher \`v070/tools/vram_swap_lab.sh:233–248\`. libublksrv iç tahsis/stack bütçesi doğrulanmadı. | P0 araştırma açığı |
| ublk canlı I/O | \`v070/experimental/ublk/nopen_ublk_swap_target.cpp:225–269\` | \`on_io\` altında \`nopen_isolated_read/write\` veya \`nopen_shadow_read/write\`; **dönüşten sonra** \`ublksrv_complete_io\`. Kullanıcı alanı mutex ve GPU/disk beklemesi kernel I/O tamamlanmasına yansır. | Doğrudan kod kanıtı |
| IPC parent oluşturma | \`v060/src/nopen_isolated_io.c:133–193\` | \`calloc\`, \`malloc\`, \`socketpair\`, \`posix_spawn\` ilk açılışta. Parent tamponu ve thread stack'leri eksiksiz mlock/prefault kanıtı yok. | P0 rezervasyon açığı |
| IPC canlı I/O | \`v060/src/nopen_isolated_io.c:196–250\` | \`pthread_mutex_lock\` **deadline başlamadan önce**, \`poll\`/send/recv ancak kilitten sonra sınırlı; writer GPU worker'a cevap bekliyor. PR #17 bunu iyileştirir, ama sürücü içi liveness garanti etmez. | Kritik kaynak kanıtı |
| Worker hazır olma | \`v060/src/nopen_isolated_worker.c:78–95\` | \`nopen_blk_open\` ve \`READY\` gönderimi, ardından **ayrı** 64 KiB \`malloc\`/\`mlock\`. READY, tüm buffer'ların hazır olduğunu kanıtlamıyor. Strict mlock yalnızca belirli root/direct modunda ölümcül. | **Yeni kaynak bulgusu** |
| Worker OOM koruması | \`v060/src/nopen_isolated_worker.c:53–69\`; launcher \`v070/tools/vram_swap_lab.sh:233–240\` | \`oom_score_adj=-900\` best-effort; systemd root servisi GUI-session yaşam döngüsü problemini azaltır. Bu ikisi GPU kernel D-state veya reclaim kaynak döngüsünü ortadan kaldırmaz. | Kısmi lifecycle mitigasyonu |
| Blok metadata ve ilk hazırlık | \`v050/src/nopen_blk_backend.c:41–79\` | 4 GiB için metadata yaklaşık 16 MiB. \`calloc\`/host staging ardından \`nopen_store_open\` yapılıyor, mlock **ondan sonra**. OpenCL runtime heap ve sürücü host buffer'ları mlock kapsamı dışında. | P0 açığı |
| Blok canlı I/O | \`v050/src/nopen_blk_backend.c:100–163\` | \`b->lock\` tutulurken \`nopen_store_read_pages/write_pages\`; bunlar blocking OpenCL çağrısı yapabilir. Kilit içi GPU beklemesi açıkça var. | Doğrudan kod kanıtı |
| OpenCL oluşturma | \`v012/src/nopen_store.c:47–151\` | \`clCreateContext\`, queue, segment başına \`clCreateBuffer\`; başlangıçtaki \`clFinish\` fiziksel VRAM pinleme veya ileride hiçbir CPU page fault yaşanmayacağı garantisi değil. | P0 driver/residency açığı |
| OpenCL canlı I/O | \`v012/src/nopen_store.c:191–240\` | \`clEnqueueRead/WriteBuffer(... CL_TRUE ...)\`, alternatif pinned-host dalında map/copy/\`clFinish\`. OpenCL yürütmesi sırasında driver-internal allocation ve lock/GFP davranışı kaynak dışından bilinmiyor. | P0 driver gözlem açığı |
| SSD shadow oluşturma | \`v070/src/nopen_shadow.c:174–343\` | \`O_DIRECT|O_DSYNC\`, \`posix_fallocate\`, sıfır doldurma, \`fdatasync\`, metadata \`mlock\` belli modlarda. Dosya sistemi/blk-mq mempool ve GFP davranışı kanıtlanmadı. | Veri koruması var; liveness bağımsız değil |
| SSD shadow canlı yazma | \`v070/src/nopen_shadow.c:361–445\` | \`disk_lock\` altında \`full_pwrite\` (O_DSYNC). Metadata lock yazma boyunca bırakılıyor; fakat VFS/blok beklemesi devam ediyor. | Olası ikinci reclaim yolu |
| SSD shadow canlı okuma | \`v070/src/nopen_shadow.c:451–514\` | \`disk_lock\` + metadata lock, GPU \`nopen_isolated_read\` yolu denendikten sonra gerekirse \`full_pread\` fallback. GPU beklemesi / disk beklemesi swap-in yoluna yansıyabilir. | P0/P1 belirsizlik |
| Async GPU upload / durdurma | \`v070/src/nopen_shadow.c:83–137\` | GPU uploader ayrı mutex/condition kullanıyor; ama \`pthread_join\` için burada doğrudan üst sınır bulunmuyor. Bu teardown sorunu kernel reclaim döngüsüyle eşit değildir. | P1 lifecycle riski |
| Linux kernel GFP davranışı | [Linux GFP/NOIO/NOFS rehberi](https://www.kernel.org/doc/html/latest/core-api/mm-api.html), [FS/IO reclaim](https://kernel.org/doc/html/v6.10/core-api/gfp_mask-from-fs-io.html) | \`GFP_KERNEL\` direct reclaim'e girebilir; \`GFP_NOIO\` veya \`memalloc_noio_save\` **kernel kodu** için kullanılabilir. Bunları userspace'e yazarak OpenCL/AMDGPU güvenliği sağlanamaz. **Gerçek kernel/GPU stack'lerinde hangi maskeler kullanıldı bilinmiyor.** | Kernel/sürücü denetimi gerekli |

### Init-time / hot-path ayrımı

\`malloc\`, \`posix_spawn\`, \`clCreateBuffer\` çoğunlukla ilk hazırlıkta gerçekleşiyor. Bu, canlı I/O sırasında doğrudan bu çağrıların tekrarlandığı anlamına **gelmez**. Ancak OpenCL sürücüsünün ilk temas, sayfa fault, buffer migration, command submission ve teardown sırasında host bellek/driver kaynakları talep edip etmediği **kanıtlanmış değil**. \`mlock\` edilebilen birkaç userspace buffer, tüm kernel/driver allocation zincirini güvenceye almaz.

## 2. İki koşullu reclaim döngüsü

### A. GPU-only: OpenCL/driver allocation

\`\`\`text
kernel_reclaim -> ublk_completion -> ublk_callback ->
isolated_ipc -> worker_block_backend -> opencl_blocking_call ->
[driver_host_allocation] -> [kernel_reclaim]
\`\`\`

\`[]\` arasındaki **driver host allocation → aynı kernel reclaim** bağımlılığı **hipotez**. İE-05 ve kernel/driver incelemesi olmadan gerçek deadlock olarak etiketlenemez. Parent IPC'yi timeout ile kapatmak, GPU thread'inin kernel D-state'den çıkacağını veya ACK verilmiş swap sayfalarının okunacağını kanıtlamaz.

### B. SSD-shadow: dosya sistemi üzerinden olası reclaim

\`\`\`text
kernel_reclaim -> ublk_completion -> ublk_callback ->
shadow_disk_io -> vfs_block_layer -> [kernel_reclaim]
\`\`\`

\`O_DIRECT | O_DSYNC\` + preallocation, kullanıcı alanındaki lazy allocation'ı ve VFS riskini azaltmayı hedefler. Buna rağmen gerçek filesystem, Btrfs/ext4, block queue, writeback, mempool ve lock ordering incelemesi olmadan VFS'nin aynı reclaim döngüsünden tamamen bağımsız olduğu iddia edilemez. Bu hipotez de **gözlenen çökme nedeni değildir**.

### C. İdealize edilmiş bağımsız servis

\`\`\`text
kernel_reclaim -> ublk_completion -> ublk_callback ->
isolated_ipc -> independent_preallocated_backend -> COMPLETE
\`\`\`

Statik model bu grafikte döngü görmez, fakat \`independent_preallocated_backend\` **henüz uygulanmadı**; bu nedenle fiziksel güvenlik kanıtı değildir. İE-02 yalnızca hedef bağımlılık kontratını ortaya koyar.

## 3. Düzeltilmesi gerekenler ve iş emri sahipliği

| Bulgular / risk | Sınıf | Sorumlu iş emri ve kapama önerisi |
|---|---|---|
| Callback, IPC ve GPU transferi hot-path boyunca bloklayıcı | P0 | **İE-05**: reclaim altında tahsissiz, önceden hazır I/O ilerleme yolu; mümkünse kritik swap-in yolundan OpenCL runtime bağımlılığını çıkar veya kernel/sürücü destekli garanti kanıtla |
| OpenCL sürücü heap/TTM/GTT maliyetleri bilinmiyor | P0 | **İE-04/05**: AMDGPU/TTM buffer/migration, GFP trace ve söz konusu GPU için forward-progress analizi |
| Worker \`READY\` mesajı bütün tamponları hazır olduğunu garanti etmiyor | P0 | **İE-05**: READY'yi bütün mandatory allocation/mlock/fault warmup başarılarından sonraya taşı; başarısızlığı swap etkinleşmeden reddet |
| IPC mutex beklemesi varsayılan 2s budget'ın dışında | P1 | **İE-01** PR #17: queue admission ve dispatched request budget'larını ayır, son-to-son limitleri ölç |
| SSD shadow read/write VFS I/O'sunun ilerlemesi doğrulanmadı | P0 | **İE-03/05**: bağımsız depolama ve reclaim-safe I/O kanıtı; gerçek disk I/O yolunun kernel lock/GFP analizi |
| GPU reset veya worker kaybında GPU-only eski ACK sayfaları kaybolabilir | P0 | **İE-03/06**: authoritative backing / generation / recovery sözleşmesi ve gerçek doğrulama |
| Shadow read sırasında mutex ve GPU IPC beklemesi | P1 | **İE-07**: lock ordering + latency + failover path regresyonları |
| Async uploader \`pthread_join\` sınırsız bekleyebilir | P1 | **İE-06/07**: cancellation-safe yaşam döngüsü ve bağımsız gözlem |
| Tek ublk kuyruğu, ortak IPC mutex, worker seri | P1 | **İE-07**: concurrency, backpressure, wait-time p99 ve starvation testleri |
| Geliştirme tabanında tarihsel 4 GiB \`start) :;;\` yolu duruyor | P0 güvenlik kapısı | **İE-01/09**: PR #17'nin deny-by-default güvenlik kilidi dev dalına alınana kadar fiziksel swap başlatılmaz; diğer başlatıcılar ayrı taranır |

**Önemli:** POSIX \`mlock\` kullanıcı sanal belleğini sabitlemeye yarar, OpenCL buffer'larını fiziksel VRAM'de değiştirilemez biçimde tutmaz. GPU rezervasyon çözümü İE-04 sorunudur. Disk shadow GPU verisinin bağımsız kopyasını sağlayabilir, fakat bu tek başına swap I/O liveness garantisi değildir.

## 4. Eksik kanıtlar / gerçek testten önce blokaj

1. **Kernel call stacks / io_uring/ublksrv queue**: gerçek failure öncesi outstanding (qid, tag), kernel task wait channel, swap page-in/out zaman çizelgesi, lockdep; ayrı güvenli gözlemciyle saklanmalı. Sistem dışında kurtarılabilir test ortamı zorunlu.
2. **OpenCL/AMDGPU runtime**: aynı request tag için \`GPU_ENTER\`/return, driver job scheduler durumu, TTM migrate/evict çağrıları, DMA sync ve gerçek host allocation/lock/GFP noktaları. Sadece VRAM kullanım sayacı yetmez.
3. **Shadow VFS/blk**: filesystem ve block aygıtının herhangi bir Nopen swap dosyasına veya swap-in bekleyen memory reclaim'e bağımlı olmadığının kernel düzeyinde incelenmesi.
4. **RAM ve thread stack**: daemon, worker, libublksrv io_uring, pthread stack ve GPU runtime'ın hazır/prefault edilmiş, baskı altında ilerleyebilen gerekli tüm kaynaklarının gerçek envanteri; başarısız rezervasyonun \`swapon\` öncesi kapanması.
5. **Bağımsız hata kayıtları**: hard-reset sonrası ilk arızayı koruyan gözlemci, fiziksel GPU açık/kapalı A/B, heap allocation failures, kill sırasında D-state veya diğer mekanizmalar. Fiziksel teste ancak İE-03..06 güvenlik kapıları sonrası izin verilebilir.

## 5. Otomatik, güvenli doğrulama

Yeni \`tools/ie02_reclaim_audit.py\` kaynak dosyalarındaki **33+ anchor** ve üç sıralama sözleşmesini gerçek satır numaralarıyla tarar. Kaynak değişip bazı işaretler kaybolursa başarısız olur. İki **hipotetik** cycle ve bir **idealize** acyclic graph üretir; \`v084/tools/reclaim_cycle.py\` döngü tespitini tekrar kullanır. \`tests/test_ie02_reclaim_audit.py\` sahte kayıp lock ve yanlış READY/buffer sırasını enjekte ederek static audit'in **fail** olmasını sınar. Birleşmiş kod tabanındaki eski script \`start) :;;\` içeriyorsa bunu ayrı alarm bilgisi olarak raporlar, ama hiçbir komut çalıştırmaz.

\`\`\`sh
cmake -S . -B build-ie02 -DNOPEN_OPENCL=OFF
cmake --build build-ie02 --parallel 2
ctest --test-dir build-ie02 -R '^ie02_' --output-on-failure
python3 tools/ie02_reclaim_audit.py
\`\`\`

Bu komutlarda **GPU başlatılmaz; root, ublk, swap, sysfs yazımı, filesystem pressure ya da kernel modül müdahalesi yoktur.**

**Tamamlama kararı:** İE-02 yalnızca kaynak incelemesi, risk matrisi, bilinmeyenler ve statik regresyonların kalitesi kabul edilip PR birleştiğinde *analiz görevi* olarak kapanabilir. **P0 reclaim bağımsızlığı problemi İE-05'te açık kalır; \`KERNEL_SWAP_BLOCKED\` kaldırılmaz.**

### Resmî dokümantasyon kaynakları

- Linux ublk block driver: https://docs.kernel.org/block/ublk.html
- Linux memory management / GFP allocation flags: https://www.kernel.org/doc/html/latest/core-api/mm-api.html
- FS/IO reclaim dependency ve \`memalloc_noio_save\` kapsamı: https://kernel.org/doc/html/v6.10/core-api/gfp_mask-from-fs-io.html

**Not:** Bu kaynaklarda bir önlemin genel olarak var olması, Nopen'ın o önlemi kullanmış veya doğru uygulamış olduğu anlamına gelmez.
