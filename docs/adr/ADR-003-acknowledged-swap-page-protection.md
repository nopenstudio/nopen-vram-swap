# ADR-003 | ACK verilmiş swap sayfalarının korunması

**Nopen VRAM SWAP | İş Emri İE-03 / P0 | 3 Kasım 2026**

[İş emri #21](https://github.com/nopenstudio/nopen-vram-swap/issues/21) · [Ana program #18](https://github.com/nopenstudio/nopen-vram-swap/issues/18) · [İE-02 kaynak analizi](../reports/2026-11-03-ie02-reclaim-source-audit.md)

Temel kaynak: develop/v0.8.5, commit 166bf2830c6468c58bef121504f7c5adfe927d9e. **v0.9.0 henüz yayınlanmadı. KERNEL_SWAP_BLOCKED.**

## 1. Mimari karar

**Seçilen güvenli profil: SSD_AUTHORITATIVE + GPU_CACHE_OPTIONAL.**

Linux'a verilecek başarılı swap WRITE cevabı yalnızca verinin **bağımsız, doğrulanabilir bir otoriter depoya** kaydedilmiş nesli için üretilebilir. GPU/VRAM sadece türev önbellektir; önceden ACK verilmiş bir kernel swap sayfasının tek geri okunabilir kopyası GPU'da bırakılamaz.

- GPU-only prototipi, GPU reseti / buffer kaybı halinde eski ACK sayfalarını geri üretemez. Deadline artırmak veya SIGKILL göndermek veri kurtarmaz; **kernel swap için RED**.
- Full CPU mirror 4 GiB kapasite için en az 4 GiB RAM + metadata ister. Sistemin RAM'ini artırmak yerine tüketir; **yalnızca 64 MiB sınırlı deneylerde** uygun.
- Bağımsız SSD shadow 4 GiB veriyi host RAM'i aynı miktarda tüketmeden tutabilir; ancak her ACK'de SSD write/commit gecikmesi vardır. Bu modun gerçek tarifi **SSD-backed swap with optional VRAM cache**; 40 GB/sn kernel swap olduğu iddia edilmez.
- GPU-only hafif, kayıp tolere edebilen **cooperative/process-local pager** ayrı bir gelecek ürün rotası olabilir. Linux çekirdeğinin önceden ACK verilmiş swap sayfaları için seçilen güvenlik profili değildir.

**Bu karar tasarımdır, uygulama değildir.** P0 veri kurtarma kodu İE-06 (#24), reclaim bağımsız I/O İE-05 (#23), fiziksel VRAM residency İE-04 (#22), canlı donanım kabulü İE-10 (#28) kapsamında açık kalır.

## 2. 4 GiB kapasite için kaynak / risk tablosu

| Seçenek | RAM maliyeti | SSD tüketimi | GPU kaybında önceki ACK sayfaları | Ürün kararı |
|---|---|---|---|---|
| GPU-only | metadata / IPC buffer | 0 | Geri getirilemez | RED |
| Full CPU mirror | **≥4 GiB RAM** + metadata | 0 | Mirror bozulmadıysa okunabilir; daemon crash ayrı | RED (RAM genişletme amacı) |
| **SSD authoritative + GPU cache** | sınırlı ön ayrılmış metadata/buffer | **≥4 GiB + journal/COW/boş alan** | SSD ve daemon I/O ayaktaysa okunabilir | **Seçilen tasarım** |
| Sıkıştırılmış RAM mirror | veriye bağlı, kötü durumda ≥4 GiB | 0 | Sıkıştırılamayan sayfalarda maliyet garanti edilemez | RED |
| SSD + bounded RAM cache | sınırlı, ölçülmüş RAM cache | ≥4 GiB + overhead | SSD authority ile okunabilir | Gelecek optimizasyon |
| Cooperative GPU pager | uygulamaya göre | 0 olabilir | Uygulamanın toleransına bağlı | Kernel swap dışında araştırma |

**Önemli bedel:** SSD commit gerektiren yazma yolu NVMe, journal ve filesystem limitleriyle sınırlıdır. GPU cache'in faydası ölçülmeden SSD/zram taban çizgisinden hızlı olduğu varsayılmamalıdır. COW extent'leri için geçici olarak fazladan disk alanı gereklidir; tek başına 4 GiB disk dosyası bütün overwrite/compaction durumlarını garanti etmez.

## 3. Mevcut kaynakta bulunan ve bulunmayan garantiler

| Kaynak dosya / satır | Doğrudan görülen | Eksik |
|---|---|---|
| v070/src/nopen_shadow.c:200,239–285 | O_CREAT/O_EXCL/O_DIRECT/O_DSYNC dosyası; preallocation, sıfır doldurma, fdatasync | Crash sonrası **yeniden açma + metaveri replay** yok |
| v070/src/nopen_shadow.c:43–59,189–191,294–296 | Checksums, written_bits, GPU page_versions **RAM içinde** | Kalıcı sayfa haritası / journal commit / boot epoch yok |
| v070/src/nopen_shadow.c:366–448 | SSD pwrite başarılıysa meta güncellenir; GPU sync veya async gelir | In-place overwrite, eski ACK neslini torn write'ta korumaz |
| v070/src/nopen_shadow.c:449–514 | GPU checksum kontrolü, SSD fallback | Kalıcı generation/transaction kimliği ve gerçek driver liveness yok |
| v070/src/nopen_shadow.c:95–114,394–433 | Canlı daemon'da async GPU upload için version ve validity | Restart sonrası replay yok |
| v070/src/nopen_shadow.c:541–575 | Worker kaybı simülasyonu ve aynı daemon'da açık SSD | Ublk daemon/supervisor crash recovery uygulanmadı |
| v060/src/nopen_recovery.c:54–111,147–220 | 64 MiB'e kadar CPU mirror; checksum fallback | 4 GiB için RAM ekonomisi kötü |
| v070/experimental/ublk/nopen_ublk_swap_target.cpp:95–104,216–269 | Volatile block target, seçilebilir GPU/shadow, blocking request completion | Kullanıcı süreç yeniden başlatma/reissue sözleşmesi ve durable FLUSH yok |
| v050/src/nopen_blk_backend.c:100–164; v060/src/nopen_isolated_io.c:196–249 | GPU metadata/checksum, IPC hatasında fail-closed | Kaybolan VRAM sayfaları için bağımsız authoritative kopya yok |

**Bugünkü shadow başarısı sınırlı:** GPU worker ölür, daemon ve disk açık kalırsa kurtarma için doğrulanan SSD kopyası vardır. **Daemon ölürse** RAM'deki checksums, written bitleri ve nesiller kaybolur. O_EXCL ile yeni dosya oluşturma eski dosyayı yeniden sahiplenmez. Bu kod **same-boot daemon restart güvenliği sağlamıyor**. O_DSYNC kullanımı dosya meta nesillerini kendi başına persist etmez.

## 4. Page state / ACK sözleşmesi

### Transaction kimliği

Her istek: (boot_epoch, device_epoch, monotonic_request_id, offset, length, payload_digest). Tek başına ublk queue tag kalıcı benzersiz kimlik sayılmaz. Desteklenen blok: **4 KiB hizalı, 4–64 KiB**; 1–16 sayfalı request tek atomik commit/ACK birimidir. Çakışan aralıklar sıralanır. Her sayfanın monoton artan generation değeri bulunur.

### Yazma durum makinesi

~~~text
UNMAPPED_ZERO / COMMITTED(n)
       |
       | WRITE arrived, identity and range validated
       v
PREPARED(new immutable COW extent)       No ACK
       |
       | Complete independent DATA I/O + durability barrier
       v
DATA_DURABLE                              No ACK
       |
       | Durable atomic manifest/journal COMMIT
       | Record: request ID, boot epoch, extent, generation, checksum
       v
MANIFEST_COMMITTED                       WRITE ACK permitted
       |
       +--> ACKED (reply may be lost; stored commit persists)
       |
       +--> async best-effort GPU cache upload
~~~

**Tasarım invariants:**

1. **ACK ancak DATA + MANIFEST durable commit sonrasında** verilir. GPU yazısı veya preload başarısı ACK koşulu değildir. Her ACK için independently recoverable veri mevcuttur.
2. Yarım kalan / EIO ile sonuçlanan COW yazma **eski committed nesli yerinde bırakır**. Başarısızlık halinde ACK yoktur. Şu anki in-place O_DSYNC overwrite buna tek başına yetmez.
3. Multi-page istek **bütün sayfaların tek commit nesli** ile yayınlanır, kısmi success yok. Reclaim sırasında 64 KiB veya çoklu request ordering ayrıca kontrol edilecek.
4. Commit durable ama ACK yanıtı kaybolduysa aynı request id + offset + digest ile tekrarı **idempotent** kabul edilir; generation tekrar yükseltilmez. Aynı kimlik farklı veriyle gelirse hata.
5. Önceki generation, yeni manifest commit tamamen doğrulanana kadar serbest bırakılmaz. COW garbage collector committed okuyucuların kullandığı extents'e dokunmaz.
6. Read, **committed authoritative map** ve hash/generation doğrulaması ile yapılır. Çok sayfalı okumanın bütün sayfaları doğrulanmadan Linux I/O tamponu değiştirilmez.
7. Unwritten sayfaların sıfır olması meşrudur; ancak eski ACK sayfası checksum kaybında **sessiz sıfır üretmek yasak**. Verifiable authoritative copy yoksa EIO ve kritik arıza.
8. GPU cache key = (boot_epoch, gpu_epoch, page_id, committed_generation). Geç/yanlış GPU response veya GPU resetinde cache geçersizleşir, hiçbir zaman otorite olmaz.
9. Gelecek DISCARD işlemleri committed tombstone gerektirir; mevcut unsupported DISCARD otomatik güvenli kabul edilmez.
10. FLUSH daha önce kabul edilen durable transaction sınırını beklemeli; v070/ublk'de mevcut boş FLUSH=success davranışı, aktif kernel swap için ayrıca incelenmeli.

Bu sözleşmedeki **atomik disk journal uygulanmadı**. Aşağıdaki Python referans modeli transaction commit'ini yalnızca **bellekte atomik bir list append** ile canlandırır. Gerçek dosya sisteminin atomikliğini, fsync/barrier başarısını veya güç kaybı dayanıklılığını kanıtlamaz.

## 5. Same-boot recovery ve replay

1. Ublk dev/daemon crash ardından yeniden açma işlemi yalnızca **aynı boot epoch ve doğru device/session kimliğinde** yapılabilir. Host reboot sonrası eski swap verisi tekrar mount/swapon edilmez.
2. Doğrulanmamış manifest veya eksik encryption key ile auto-reattach **yasak**. Yeni daemon tüm committed log'u, sayfa checksum ve monoton generation'ı doğrulamalı.
3. Replay yalnızca **tam committed** nesiller üzerinden sayfa haritasını kurar. PREPARED ve yalnızca DATA_DURABLE aşamasında kalmış kayıtlar yok sayılır; önceden ACK verilmiş nesil korunur.
4. Ublk için mevcut kod recovery durumunu reddediyor (v070/experimental/ublk/nopen_ublk_swap_target.cpp:97). Linux UBLK_F_USER_RECOVERY, UBLK_F_USER_RECOVERY_REISSUE ve UBLK_F_USER_RECOVERY_FAIL_IO semantiği incelenip İE-06'da bir **karar ve implementasyon** gerektirir. REISSUE, aynı WRITE'ın tekrar gelebileceği anlamına gelir; idempotency olmadan güvenli değildir. [Kernel ublk belgesi](https://docs.kernel.org/block/ublk.html).
5. Restore boyunca GPU cache tamamen INVALID. Yeniden upload yalnızca yeni gpu_epoch ve committed_generation ile yapılabilir.
6. Başarılı boot/daemon kurtarma ile **gerçek sistem elektrik kesintisi/reboot** aynı şey değildir. Linux'un önceki kernel sayfalarının reboot sonrası restore edilmesi hedeflenmiyor. Eski swap data'sının **gizliliği** gerekir.
7. Kalıcı SSD shadow kişisel bellek içeriği taşıyabilir. Disk şifrelemesi veya aynı boot boyunca ayrı supervisor tarafından korunan ephemeral key şartlarının uygulanması gerekir. Anahtar yeni daemon için erişilemezse **fail closed**; güvenilmeyen eski dosya restore edilmez.

## 6. Arıza matrisi ve açık sahiplik

| Olay | Seçilen profilin hedef davranışı | Bugün kanıt / uygulama |
|---|---|---|
| GPU worker SIGKILL, daemon ayakta | Cache invalidate, SSD'den checksum doğrulanmış read | Mevcut v070 mock kısmen evet; yeni journal yok; İE-06 |
| GPU reset, host/disk ayakta | Tüm GPU cache invalid; SSD authority | Fiziksel kanıt yok; İE-04/05/06/10 |
| Daemon crash, kernel hâlâ çalışıyor | Quiesce, same-boot journal replay, ublk reattach | **Uygulanmadı**; İE-06 |
| COW data tamam, commit yok | Önceki committed nesil görünür; yeni ACK yok | Referans modelde evet, fiziksel disk testinde yok; İE-06 |
| Commit var, ACK kayboldu | Aynı request idempotent ACK; eski/yeniden veri atomik | Referans modelde evet; İE-06 |
| Geç GPU cevabı | Generation/epoch mismatch atılır | Referans modelde evet; İE-06/07 |
| SSD full/EIO/torn record | No ACK, önceki geçerli nesil korunur | Tam COW backend yok; İE-05/06 |
| SSD/VFS reclaim beklemesi | Forward progress kanıtlanmadan kernel swap yasak | **Açık P0**, İE-05 |
| Host reboot / power loss | Yeni boot yeni epoch; eski swap reattach yok, gizli veri korunmalı | Encryption/key yönetimi uygulanmadı; İE-06/10 |
| SSD fiziksel kayıp | Veri kaybı olabilir, güvenli kurtarma iddia edilmez | Kapsam dışı; kullanıcıya açıkça belirtilmeli |

## 7. Teslim edilen referans model ve test

- tools/ie03_ack_model.py: **design-only Python state machine**, memory-only journal, kimlik/nesil/ACK politikası; hiçbir GPU, ublk, swap, disk veya root erişimi yok.
- tests/test_ie03_ack_model.py: commit olmadan ACK reddi, torn/incomplete transaction, aynı boot replay, kayıp ACK için idempotency, 16 sayfa atomik transaction modellemesi, eski GPU response rejection, checksum fail-closed, immutable output ve cross-boot restore reddi.
- İE-06, **gerçek C storage, atomic COW extents, disk write ordering, fsync, durable metadata, ublk reattach, encryption key lifecycle, fault injection** kodunu sağlayacak. Bu modelin CI PASS olması İE-06'yı kapatmaz.

~~~sh
cmake -S . -B build-ie03 -DNOPEN_OPENCL=OFF
cmake --build build-ie03 --parallel 2
ctest --test-dir build-ie03 -R '^ie03_' --output-on-failure
~~~

**Mimari kabul koşulu:** Seçenek/maliyet matrisi, generation/ACK/replay sözleşmesi, arıza kapsamı, kaynak kanıtları ve mock regresyon testleri tamam. **Gerçek kurtarma algoritması henüz yok. KERNEL_SWAP_BLOCKED.**
