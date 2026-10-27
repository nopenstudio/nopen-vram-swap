# NopenOS kararı: RAM sayfalayıcı araştırmasını gerçek SWAP'e taşımak

**NopenOS henüz oluşturulmadı.** Garuda Linux-zen, gelecekte NopenOS için seçilen Linux-zen çekirdeğine ait API'leri incelemek açısından faydalı, ancak çekirdek sürümleri/Kconfig/amdgpu kodları eşleşmeden ikili modül uyumluluğu garanti değildir.

## Bugünkü katmanlar

1. `nopen_page_core`: freestanding uyumlu saf ISO C11 sayfa durumu / checksum / victim seçme algoritmaları. Bu **bir Linux kernel ABI** değildir. Kernel portunda `stdint.h`, tipler, kilitler ve saklama ilkeleri yeniden değerlendirilecek.
2. `nopen_store`: `nopen_store_{open,read,write,close}` opaque store API; mock ve AMD OpenCL uygulamaları `nopen_store.c` içinde. Sürücü/OCL başlığı çekirdek policy'ye sızmıyor ama backend'ler hâlâ ayrı derleme modüllerine bölünmedi.
3. `nopen_fault_managed`: Linux/`userfaultfd` adapter + deney uygulaması; glibc ve OpenCL dinamik bağımlılıkları burada bulunur.
4. **Henüz yok:** NopenOS runtime, NopenOS kernel module, DRM/AMDGPU reclaim-safe backend, global Linux SWAP.

## Gerçek SWAP kabul kapıları

- Kendi belleğini NBD userspace daemon'u üzerinden geri kazanmayı gerektiren zincir yasak (eski kernel panic).
- GPU I/O ve Linux bellek kurtarma arasında ilerleme/deadlock garantisi tasarım ve deneyle ispatlanmalı.
- GPU reset sırasında VRAM'de bulunan **tek sayfa kopyasının yok olma** durumunda korunma politikası olmalı: güvenilir kalıcı yedek veya sadece kritik olmayan ve kaybedilebilir uygulamalar için açık sınır.
- Kernel testleri önce QEMU/mock ile, gerçek makinede sistem genelinde takas olmadan, en son ayrı test cihazında global SWAP.
- Ürün kapısı: gerçek net RAM tasarrufu, veri kaybı olmadığına dair güçlü kanıt, GPU memory budget politikası, ZRAM/zswap/NVMe kıyasları.

Yalnızca `--baseline` ile süreç RSS'ini kıyaslamak tam ürün kabul kriteri değildir. Bu ilk ekonomik filtrenin amacı temelsiz 'VRAM = ücretsiz RAM' iddiasını önlemektir.