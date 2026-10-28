# Nopen VRAM SWAP v0.6.0-dev — GPU kaybı, OOM sınırları ve kontrollü kurtarma

**İlk kararlı sürüm v1.0.** v0.6 deneysel; gerçek Linux global SWAP etkinleştirilmemiştir. Ana iş istasyonunda kernel ayarları değiştirilmez.

## v0.6 gerçek kodu

`nopen_recovery` modülü önceki checksum denetimli `nopen_blk_backend` motorunu sarar. İki çalışma biçimi vardır:

- **NO_MIRROR / FAILED_CLOSED:** GPU'dan okuma veya yazma hata dönerse tüm aygıtın durumunu FAILED_CLOSED'a geçirir. Sonraki bütün okuma/yazmalar `-EIO` olur. Hatalı veri uygulamaya hiç dönmez. Gerçek SWAP için henüz güvenilir bir kurtarma sağlamaz.
- **HOST_MIRROR / CPU_FALLBACK:** GPU ve tam boy bir CPU kopyası beraber tutulur. GPU I/O hatası veya açık aygıt-kaybı bildirimi sonrasında kernel'e bağlı olmadan CPU belleğinde devam edilebilir. Her RAM sayfasının checksum'u denetlenir; bozuk mirror da FAILED_CLOSED durumuna geçer. Güncellenen mirror daima son ACK edilmiş geçerli veridir.

**Önemli:** HOST_MIRROR, her VRAM sayfasının tam CPU RAM kopyasını zorunlu tuttuğundan gerçek RAM tasarrufunu ortadan kaldırır. Bu yalnızca kurtarma için doğruluk referansıdır. GPU reseti sonrasında yeniden gerçek VRAM'e taşıma/yeniden yükleme ve daemon çöktüğünde mirror'un yaşaması henüz yapılmadı.

### Sınırlar ve güvenlik

- Normal motor üzerinde 4–64 MiB havuz, 4 KiB hizalama ve en fazla 64 KiB I/O. Tüm public I/O durum geçişleri mutex altında seri çalışır.
- `open_bounded()` host CPU tahsisleri için **işlem başlamadan** açık üst sınır uygular. Yetersiz bütçede GPU açılmadan `-ENOMEM` döner. Bu hesap, OpenCL runtime veya kernel sürücü tahsislerini **kapsamaz**.
- Test kodu hem GPU I/O başarısızlığı hem açık `device_lost()` bildirimi ile veri sürekliliğini veya `-EIO` davranışını sınar. `nopen_recovery_inject_mirror_corruption()` yalnızca mock backend için, checksum'ın hatalı veriyi reddetmesini kanıtlamak içindir.
- Prototip tek proseslidir; proses öldürülürse içindeki RAM mirror da kaybolur. Kernel reclaim ve ublk tekrar başlatma güvenliği bağımsız olarak kanıtlanmış değildir.
- GPU reseti, DMA işleminin ne kadar sürede takılacağını ve OpenCL cleanup işleminin donup donmayacağını bu testler **simüle etmiyor**. Gerçek AMDGPU reset/driver kaybı denemesi yapılmadı.

### Linux ublk köprüsü

`v060/experimental/ublk/nopen_ublk_recovery_target.cpp` v0.5 Linux ublk callback'lerini artık kurtarma API'sine bağlar. Yalnızca test VM koşullarında çalışmasına izin veren geçiş koruması korunur. `NOPEN_UBLK_RECOVERY_MODE=full-cpu-mirror` açıkça seçilirse **tam CPU yedekli** (RAM tasarrufsuz) deney yapılabilir; aksi hâlde GPU kaybında I/O hata verir. Flush/disk dayanıklılığı vaat edilmez. Normal sistem SWAP'i olarak etkinleştirilemez. CI upstream `libublksrv` commit 6d0d922614ec18e6f2b5d476af8d540a1d9c1de8 üzerinden **yalnızca derler ve bağlar**, aygıt oluşturmaz.

Linux çekirdeği, ublk için `UBLK_F_USER_RECOVERY`, `UBLK_F_USER_RECOVERY_FAIL_IO` ve `UBLK_F_USER_RECOVERY_REISSUE` özelliklerini belgeler. **Prototip bu özellikleri etkinleştirmiyor**, çünkü daemon yeniden başlatıldığında VRAM ve CPU yedeğinin devamlılığını henüz sağlamıyoruz. Yeniden verilen bir yazma talebinin iki kez uygulanması gibi tehlikeler ayrı ele alınmalıdır. Referans: https://docs.kernel.org/block/ublk.html

## Test komutları (otomatik SWAP etkinleştirme yok)

~~~bash
cd ~
git clone --branch develop/v0.6.0 --single-branch https://github.com/nopenstudio/nopen-vram-swap.git nopen-v060
cd ~/nopen-v060
cmake -S v060 -B build-v060 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v060 --parallel
ctest --test-dir build-v060 --output-on-failure
~~~

İsteğe bağlı kontrollü gerçek AMD OpenCL laboratuvarı: **GPU'yu resetlemez; sadece GPU kaybı bildirimi yapar**.

~~~bash
./build-v060/nopen-recovery-lab --backend opencl --mode mirror \
    --operations 1000 --inject-loss-after 500
./build-v060/nopen-recovery-lab --backend opencl --mode failclosed \
    --operations 1000 --inject-loss-after 500
~~~

`RECOVERY_LAB_PASS` yalnızca kontrollü veri doğruluğu testidir. GPU reseti, kernel aygıtı ve global SWAP deneyinden tamamen farklıdır.

## v0.6 kabul kapıları (henüz tamamlanmadı)

1. Mock hata / device-lost / mirror corruption ve host-budget OOM testlerinin tümünün geçmesi.
2. RX 9060 XT gerçek OpenCL üzerinde iki kontrollü kayıp-bildirimi laboratuvarı.
3. Gerçek Linux ublk aygıtı için **ayrı test sistemi veya kurtarma planı olan yalıtılmış kurulum**: oluşturma, 4 KiB I/O, daemon ölümü, bloke I/O ve temiz silme.
4. AMDGPU reseti (gerçek reset, firmware timeout, GPU context kaybı) ve sürücü kaynaklı I/O askıda kalmasının p99/max/timeout testi.
5. İleri ilerleme ve deadlock incelemesi, dayanıklı veri yedeği, host mirror olmayan çözümde veri kaybına yönelik açık fail-close sözleşmesi.
6. NopenOS hedefindeki GNU'suz saf bellek politikası ayrı tutulur.

Bu şartlar sağlanmadan v0.6'yı kararlı gerçek SWAP sürümü, Bannerlord otomatik RAM uzantısı veya v1.0 diye duyuramayız.

## v0.6 ilerleme 2 — gerçek I/O zaman aşımı izleme ve Bannerlord ölçüm hazırlığı

`v060/tools/watchdog.py` Linux kullanıcı alanındaki GPU deneylerini **ayrı bir process group** içinde çalıştırıp 1–60 sn sınır uygular. Süre aşılırsa önce SIGTERM, ardından en fazla 2 sn grace sonrası SIGKILL yollar; sürecin gerçekten sona erdiğini doğrulamadan başarı sonucu üretmez. `RECOVERY_LAB_PASS` veya `STRESS_PASS` gibi doğru çocuk doğrulamasını ve `no_global_swap=YES` alanını görmeden `WATCHDOG_PASS` yazmaz. Yazılan log 1MiB ile sınırlandırılmıştır; büyük çıktı deadlock yaratmasın diye tempfile kullanılır. Testte gerçek bellek baskısı, GPU reseti veya kernel swap açılmaz.

**Sınır:** GPU sürücüsü çekirdekte kesilemez D durumunda bekliyorsa SIGKILL işlemi sonlandırmaya yetmeyebilir. Böyle durumda `WATCHDOG_UNREAPED` bildirimi verir ve güvende olduğunu iddia etmez. Watchdog gerçek kernel deadlock'unu çözdüğünün kanıtı değildir.

~~~bash
cd ~/nopen-v060
git pull --ff-only
cmake -S v060 -B build-v060 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v060 --parallel
ctest --test-dir build-v060 --output-on-failure
python3 v060/tools/watchdog.py --timeout-seconds 10 --grace-seconds 0.5 \
  --binary "$PWD/build-v060/nopen-recovery-lab" -- \
  --backend opencl --mode failclosed --operations 1000 --inject-loss-after 500
~~~

Bu komut tüm sistem SWAP'ine dokunmaz; yalnızca ayrı GPU laboratuvar alt işlemini denetler. CTest, başarılı sonlandırma, sahte başarı reddi ve takılmış test prosesinin zaman aşımıyla kapatılmasını sınar.

Bannerlord için de **oyunun Nopen kapalıyken süreç ağacı baz çizgisi** hazırladık: `v060/tools/game_baseline.py`. Kullanıcı oyunu kendisi açtıktan sonra PID verir; araç hiçbir oyuna kod enjekte etmez, `MemAvailable`, VmRSS/VmSwap, GPU genelindeki VRAM/GTT sayaçları ve alt süreç sayısını CSV'ye kaydeder. İstenirse 1–1800 sn ve 250–5000ms örnekleme. Bu toplam RSS paylaşılan sayfaları çift sayabilir ve fps/frametime ölçmez. FPS ve %1 low için ayrı bir frametime kaydı gerekecek. Gelecekte v0.7'de aynı sahnelerde Nopen etkin/kapalı karşılaştırmasına başlanacak.

~~~bash
python3 v060/tools/game_baseline.py --pid GAME_PID \
  --duration-seconds 60 --interval-ms 1000 --csv "$HOME/bannerlord-baseline.csv"
~~~

**GAME_PID gerçek oyun proses PID'sidir;** bu sadece çalışmakta olan oyunu okur, başlatmaz veya yönetmez. Çıktı dosyası varsa üzerine yazmaz.

## v0.6 son geliştirme paketi — geri yükleme ve çekirdek I/O doğrulaması

**İlk sürüm iskeletine eklenen gerçek uygulama:**

- `nopen_recovery_rebuild_gpu()` artık full CPU-mirror modunda GPU kaybından sonra yeni `nopen_blk_backend` ayırır, bütün sayfaları toplu yazıp tekrar okur ve karşılaştırır. Yeniden yüklenen 4KiB sayfaların tamamı doğru olmadan `GPU_ACTIVE` durumuna geçmez. Kısmi yeniden yükleme hatasında eski CPU kopyası korunur, GPU verisi güvenilmez olarak kullanılmaz. Sıralama mutex ile korunur. `NO_MIRROR` için geri yükleme reddedilir.
- `nopen-isolated-worker` bağımsız bir süreçtir; GPU context o süreçte açılır. `nopen_isolated_io` üst işlemi onu `posix_spawn()` ile başlatır. UNIX soketinden 4–64KiB blok istekleri ve karşılıklı checksum gönderilir. Bir I/O isteğinin tamamlama yanıtı 50–5000ms içinde gelmezse üst süreç `-ETIMEDOUT` döndürür, GPU işçisini sonlandırmayı dener ve sonraki bütün istekleri `-EIO` ile durdurur. Bu, gerçek AMDGPU sürücüsünün kernel D-state takılmasını düzelttiğinin garantisi değildir.
- `v060/experimental/ublk/nopen_ublk_recovery_target.cpp` varsayılan `NO_MIRROR` blok I/O yolunda artık bu bağımsız worker sürecini kullanır. `NOPEN_UBLK_WORKER_BIN` ile worker yolu açıkça belirtilir; OpenCL yoksa mock'a sessiz düşüş yoktur. İsteğe bağlı `NOPEN_UBLK_RECOVERY_MODE=full-cpu-mirror` test modunda tam CPU kopyası, mevcut in-process recovery motoru tarafından kullanılır; RAM kazanımı iddia edilmez.
- `nopen-test-rebuild` 1024 sayfanın yeniden oluşturulması, kısmi hata, CPU fallback, 4 eşzamanlı istemci ve bozuk mirror nedeniyle fail-closed davranışını denetler.
- `nopen-test-isolated-io` 300 x 16KiB yazma/okuma turu, checksum doğrulaması, aralık sınırları ve 100ms kontrollü işçi askısı altında timeout davranışını denetler.
- `nopen-isolated-smoke` gerçek AMD OpenCL veya açık mock backend için 4MiB/1024 sayfalık rastgele 4KiB okuma-yazma ve tam geri okuma laboratuvarıdır.

**Çalıştırılmış gerçek kernel testi (GitHub ephemeral VM, mock veri backend'i):** `ublk` aygıtı oluşturulup 4KiB direct I/O yapıldı, veri byte-by-byte doğrulandı, aygıt kaldırıldı. Aynı işlem hem tam CPU mirror hem exec-isolated worker modunda geçmiştir. CI çıktısında `UBLK_LIFECYCLE_PASS ... mirror_and_isolated_worker=YES ...` kaydı vardır. Linux kernel ublk sürücüsü gerçekten çalıştırıldı; GPU ise gerçek değil mock olarak seçildi. Bu nedenle fiziksel RX 9060 XT resetini, SWAP sırasındaki reclaim ilerlemesini veya varsayılan oyun belleği entegrasyonunu kanıtlamaz.

### Donanım için hazır olan iki gerçek OpenCL kabul deneyi

~~~bash
cd ~/nopen-v060
git pull --ff-only
cmake -S v060 -B build-v060 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v060 --parallel
ctest --test-dir build-v060 --output-on-failure
./build-v060/nopen-recovery-lab --backend opencl --mode mirror --operations 1000 --inject-loss-after 500 --rebuild-after-loss
./build-v060/nopen-isolated-smoke --worker "$PWD/build-v060/nopen-isolated-worker" --backend opencl --operations 1000 --timeout-ms 2000
~~~

Bu iki test sudo gerektirmez, GPU'yu **gerçekte resetlemez**, kernel SWAP veya `ublk` aygıtı oluşturmaz. Kullanıcının RX 9060 XT üzerinde çalıştırılıp kabul edilmesi ayrıdır.

### v0.6 mühendislik çıkış kararı

v0.6'nin amaçladığı hata durum makinesi, GPU veri kaybı bildirimi, CPU yedekten GPU'yu yeniden oluşturma, fail-closed, kontrollü OOM tahsis reddi, kullanıcı alanı I/O timeout'u ve ublk çekirdek I/O bağlantısının **prototip kodu tamamlandı**. Gerçek AMDGPU firmware/kernel reseti, tam sistem SWAP reclaim kilitlenmesizliği ve daemon yeniden başlatma sonrası dayanıklı veri yedeği henüz doğrulanmadı. Bu iddialar ürün v1.0 kabulünde zorunlu olacak; v0.6 kod kilometre taşıyla karıştırılmamalı.
