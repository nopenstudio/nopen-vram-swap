# Nopen VRAM-SWAP — v0.12.0-alpha.1

**Yeni mimari: Nopen Memory Fabric Lab.** GPU VRAM'ini Linux'un sistem genelindeki SWAP mekanizmasına NBD ile bağlamak yerine, yalnızca uygulamanın açıkça ayırdığı bellek bölgesini OpenCL + `userfaultfd` ile sayfalıyoruz.

**Bu sürüm henüz Bannerlord'a fazladan RAM sağlamaz.** `swapon`, `/dev/nbd15`, `modprobe`, root servisi yoktur. Eski OpenCL+NBD denemeleri tüm sistemi çökerttiği için aktif SWAP olarak **kesinlikle yeniden kullanılmamalı**.

## İlk Garuda testleri

```bash
cmake -S v012 -B build-v012 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012 --parallel
ctest --test-dir build-v012 --output-on-failure
./build-v012/nopen-vram-lab --probe
./build-v012/nopen-vram-lab --raw --backend mock --mib 4 --pages 128 --rounds 2
./build-v012/nopen-vram-lab --demo --backend mock --mib 4 --pages 128 --rounds 2
```

Bunlar çalışınca **oyun kapalıyken** `--backend opencl --mib 64` ile gerçek ekran kartı testi yapılabilir. Başarılı benchmark çıktısı henüz sistem SWAP'i anlamına gelmez.

Ayrıntılı anlatım: [v0.12 Türkçe rehberi](v012/README_TR.md) · [NopenOS mimari planı](v012/docs/ARCHITECTURE_TR.md) · [Değişiklikler](v012/CHANGELOG.md).

Eski Vulkan deneyinin kaynakları `src/` dizininde korunuyor; `cmake -S .` komutu eski Vulkan araştırma sürümünü derler. **Yeni sürüm için `cmake -S v012` kullan.**
