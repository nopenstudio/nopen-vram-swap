# Linux ublk → Nopen GPU VRAM: İzole çekirdek arayüzü deneyi

**DURUM: Taslak ublk bağdaştırıcısı. Ana makinede çalıştırılmamalı; hiçbir zaman otomatik global SWAP etkinleştirmez.**

Bu klasördeki `nopen_ublk_target.cpp`, Linux `ublk_drv` blk-mq aygıt callback'lerini gerçek Nopen OpenCL GPU blok depolama API'sine bağlar. `libublksrv` (pinned source 6d0d922614ec18e6f2b5d476af8d540a1d9c1de8; 2026-10-20) API'sini temel alır. C++20 derleme sözdizimi GitHub CI'da upstream başlıklarıyla kontrol edilir. **Çalışan bir kernel block device olarak test edilmiş veya linker sonrası doğrulanmış değildir.**

### Veri akışı

`Linux blk-mq (/dev/ublkb*) → ublk io_uring → libublksrv → nopen_blk_backend → OpenCL cl_mem (ayrık AMD GPU)`.

- `nopen_blk_backend` 4KiB hizalı, en fazla 16 sayfa/64KiB okuma-yazma transferlerini destekler ve gerçek `NOPEN_STORE_OPENCL`/açık mock'u kullanır.
- Hiç yazılmamış sayfalar deterministik sıfır okunur.
- Her GPU-backed sayfa checksum ile kontrol edilir; okuma tamamen doğrulanmadan kernel callback tamponuna veri kopyalanmaz.
- Başarısız GPU yazma durumunda sayfanın eski hâlinin sağlam kaldığı varsayılmaz: hedef bölgedeki tüm sayfalar `POISONED` olur, sonraki okumalar `-EIO` döner. Tam başarılı yeni yazmayla iyileşebilir.
- Akış mutex ile seri hâle getirilmiştir; her I/O tamamlanana kadar host staging geçerlidir.
- Kapasite: ilk blok aygıt deneyi yalnızca 64MiB; GPU bellek rezervi ve kontrol tamponları ayrıdır. Bir OpenCL context/process GPU'dan bağımsız yeniden başlarsa veri kaybolabilir.

### Bilinçli biçimde etkinleştirmediklerimiz

- `swapon`, `swapoff`, kernel reclaim callback, frontswap/zswap hook, NBD, PID1 belleği, initramfs modifikasyonu.
- Ana sistemde `ublk` blok aygıtının otomatik oluşturulması veya kernel module loading.
- ublk adapter'ını normal v0.5 CMake derlemesine bağlamak. **Gerçek block device kullanımı sadece ayrı VM/dedike test makinesinde ve libublksrv araçlarıyla mümkündür.**
- `UBLK_IO_OP_FLUSH` veri kalıcılığı vaat etmediğimiz için hata döndürür; bu disk veya dosya sistemi backend'i değildir. GPU reseti/sistem kapanışı sırasında veri kaybeder.
- `NOPEN_UBLK_ISOLATED_LAB=EXPERIMENTAL_BLOCK_IO_NOT_SWAP` çevre değişkeni ve DMI sanal makine kontrolü olmadığı sürece adaptör başlamayı reddeder. Bunlar güvenlik garantisi değil, hatalı ana bilgisayar çalıştırma riskini düşüren ek kapılardır.

### Neden hemen sistem swap'i yapmıyoruz?

`ublk` kernel blok katmanından I/O isteğini kullanıcı alanına gönderir. Eğer kernel direct reclaim bir sayfayı swap'e gönderirken GPU-backed ublk daemon'una muhtaç kalır ve OpenCL/AMDGPU sürücüsü aynı RAM'i geri kazanmak zorunda kalırsa döngüsel bekleme oluşabilir. Ayrılmış/pinlenmiş RAM, bağımsız ilerleme garantisi, bellek baskısında sınırlandırılmış işlem süresi, sürücü reseti ve kurtarma testleri kanıtlanmadan `swapon` yapmak **kesinlikle yasak**.

Yeniden kullanılan resmi Linux API belgeleri: https://docs.kernel.org/block/ublk.html ve https://docs.kernel.org/admin-guide/mm/userfaultfd.html

### Deney güvenliği ve bitiş kriterleri

1. GitHub mock/C11/ASan/OpenCL compile ve upstream ublk C++ sözdizimi kontrolleri başarıyla geçmeli.
2. **Ayrı GPU passthrough içeren sanal makinede veya ayrı fiziksel test bilgisayarında**, sadece veri dosyası/ham blok okuma-yazma yolunun I/O bütünlüğü test edilmeli. İlk aşamada hiçbir dosya sistemi mount edilmemeli ve swap açılmamalı.
3. Gerçek ublk create/destroy ve kernel timeout, daemon crash, SIGKILL, GPU reset, OOM testleri dış sistemin canlılığını etkilememeli; kilitlenmeler dışarıdan teşhis/kurtarma imkânıyla araştırılmalı.
4. Önceki 256MiB kazancın yanında sürücü/GTT dahil sistem genelindeki tasarruf, p99 fault gecikmesi ve gerçek uygulama ölçümleri doğrulanmalı.

Bu adımlar tamamlanmadıysa v0.5'in **doğrudan reclaim/SWAP entegrasyonu tamamlandı** demek doğru değildir. Sonraki v0.6 GPU reseti/hata kurtarma hedefleri bu bağımlılığı kapatır.
