> **Güncel deneysel geliştirme v0.6.0-dev:** [GPU kaybında fail-closed, CPU mirror ve kontrollü OOM testleri](v060/README_TR.md). Gerçek Linux global SWAP henüz etkin değil; ilk kararlı sürüm v1.0 olacaktır.

---

> **Güncel deneysel geliştirme: v0.5.0-dev.** [Linux-zen VM bellek baskısı sensörü ve güvenli entegrasyon kapıları](v050/README_TR.md). İlk adım yalnızca salt okunur gözlem ve bağımsız C11 politikasıdır; çekirdekte SWAP etkin değildir. İlk kararlı sürüm v1.0 olacaktır.

---

> **Güncel deneysel geliştirme: v0.4.0-dev.** [64/256/1024 MiB gerçek RAM/VRAM karşılaştırması ve v1.0 güvenlik hedefleri](v040/README_TR.md). Sistem genelinde SWAP henüz yoktur; v1.0 ilk kararlı sürüm olacaktır.

---

> **Geliştirme dalı v0.3.0-dev:** [Sayfa getirimi, kontrollü prefetch ve v1.0 yol haritası](v030/README_TR.md). Bu hâlâ gerçek sistem SWAP'i değildir; ilk kararlı sürüm yalnızca v1.0 olacaktır.

---

> **Geliştirme:** [v0.2.0-dev GPU veri yolu ölçümü](v020/README_TR.md) — Alpha.5 son mimari alfa olarak korunur. Bu dal gerçek SWAP değildir; gerçek AMD GPU bant genişliği testi ve bellek ekonomisi kabulü henüz bekleniyor.

---

# Nopen VRAM-SWAP — v0.12.0-alpha.5

**Yeni özellik:** GNU/Linux bağımlılığı taşımayan saf C11 sayfa politikası + gerçek bellek ekonomisi kıyası (normal RAM ile OpenCL VRAM sayfalayıcısını aynı iş yükünde ölçme).

**Gerçek donanım dayanağı:** Alpha.4 RX 9060 XT, 100.000 rastgele işlemde 1.024/1.024 son sayfa doğrulaması; p99 120,71 µs; 78.753 çıkarma/77.761 geri alma başarılı.

**Sınır:** Bu sürüm Linux sistem SWAP'i değil. NopenOS henüz yapılmadı. Linux-zen / NopenOS çekirdek entegrasyonu gelecek araştırma; eski NBD SWAP'in kernel panic nedeniyle kullanılması güvenli değil.

[**Alpha.5 test rehberi**](v012/README_ALPHA5_TR.md) · [NopenOS taşınabilirlik tasarımı](v012/docs/PORTABILITY_AND_SWAP_TR.md) · [Gerçek SWAP hedefleri](v012/docs/TRUE_SWAP_ROADMAP_TR.md)

---

# Nopen VRAM-SWAP — v0.12.0-alpha.4

**Araştırma hedefi:** NopenOS için güvenli ve gerçekten Linux çekirdeğinin kullanabileceği GPU VRAM destekli SWAP. **Mevcut alpha.4 henüz sistem SWAP'i değildir.**

## Bugüne kadar doğrulananlar

- **v0.12.0-alpha.1:** AMD RX 9060 XT OpenCL + Linux `userfaultfd` ile GPU'dan eksik sayfa geri yükleme doğrulandı.
- **v0.12.0-alpha.2:** Gerçek GPU'da 1.024 mantıksal sayfa / 32 resident RAM sayfası kotası, 6.144 page-out ve 3.072 page-in, veri bütünlüğü başarılı.
- **v0.12.0-alpha.3:** Gerçek GPU + fault-driven sayfalayıcı, 512 mantıksal sayfa / 16 resident sayfa, 2.976 çıkarma ve 2.480 geri yükleme, veri bütünlüğü başarılı.
- **v0.12.0-alpha.4:** Rastgele karışık 4 KiB yazma/okuma, çoklu seed, sıcak/soğuk erişim, tam son doğrulama, p50/p95/p99/max işlem gecikmesi, proses tepe RAM ölçümü, toplam 2 milyon işlem sınırı; gerçek kartın alpha.4 stres sonuçları henüz gerekiyor.

**Önemli:** Önceki OpenCL+NBD üzerinde etkinleştirilen global SWAP, Linux PID1 çöküşüne kadar uzanan kernel panic oluşturdu. Eski NBD scriptlerini **yeniden çalıştırmayın**. Alpha.4'te `/dev/nbd15`, `mkswap`, `swapon`, yeni kernel modülü veya root servisi yoktur.

## Garuda kurulum ve deneme

```bash
git clone --branch develop/v0.12.0-alpha.4 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v012a4
cd nopen-v012a4
cmake -S v012 -B build-v012-a4 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a4 --parallel
ctest --test-dir build-v012-a4 --output-on-failure
swapon --show
./build-v012-a4/nopen-vram-demand --probe
```

`/dev/nbd15` aktifse testi başlatmayın. İlk gerçek AMD GPU rastgele stres testi için [Alpha.4 Türkçe rehberi](v012/README_ALPHA4_TR.md) içindeki **10.000 işlem/64 MiB** komutunu kullanın. Süre aşımı veya GPU hatası PASS sayılmaz.

## NopenOS yolu

Kullanıcı alanı belleği ≠ çekirdek global SWAP. Tam ürün için GPU reseti sonrası veri geri kazanımı, çekirdek VM reclaim/deadlock analizi, DMA/yedek kaynak garantileri, VM üzerinde güvenli ilk prototip, ardından ayrı test makinesi ve uzun süreli ölçümler gereklidir. [Gerçek Linux SWAP karar belgesi](v012/docs/TRUE_SWAP_ROADMAP_TR.md).

[Kaynak ve İngilizce README](README.md) · [Deneme geçmişi](docs/RELEASES.md).
