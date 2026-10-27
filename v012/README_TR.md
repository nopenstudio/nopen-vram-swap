> **Güncel sürüm v0.12.0-alpha.5:** Linux uygulama düzeyinde GPU sayfalama devam ederken saf C11 sayfa politikası ve RAM/VRAM süreç RSS karşılaştırması eklendi. [Alpha.5 Türkçe rehberi](README_ALPHA5_TR.md). NopenOS henüz yalnızca plan aşamasında; gerçek kernel SWAP yok.

---

# Nopen Memory Fabric v0.12.0-alpha.4

**Güncel uygulama testi:** karışık 4 KiB rastgele okuma/yazma yükü, gerçek Linux `userfaultfd` eksik-sayfa olayları, OpenCL VRAM deposu, 4 KiB sayfa bütünlüğü, RAM resident kotası ve gecikme örneklemesi.

**Bu sistemin gerçek Linux SWAP'i değildir.** Diğer uygulamalara şeffaf RAM sağlamaz. Kernel panic'e neden olan eski NBD tabanlı SWAP kullanılmıyor.

[**v0.12.0-alpha.4 Türkçe kurulum / uzun süreli test**](README_ALPHA4_TR.md)

- `nopen-vram-demand --stress`: uygulama alanında kontrollü fault-driven random sayfalama.
- `nopen-vram-demand` (stress olmadan): alpha.3'ün doğrulanmış sayfa çıkartma/getirme testi.
- `nopen-vram-managed`: alpha.2'nin kooperatif LRU havuzu.
- `nopen-vram-test-store`, `nopen-vram-test-memory`: sahte GPU depoda sınır, hata, bütünlük testleri.
- `ctest`: yerel testler; gerçek AMD OpenCL arka ucu yalnızca GPU donanımında doğrulanabilir.

Güncel kod ve önceki prototipler: [Değişiklikler](CHANGELOG_ALPHA4.md) · [Gerçek SWAP hedefi](docs/TRUE_SWAP_ROADMAP_TR.md).
