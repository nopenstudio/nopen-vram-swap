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
