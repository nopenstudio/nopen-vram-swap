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
