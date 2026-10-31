# Nopen VRAM SWAP

**AMD GPU belleğini Linux ve gelecekteki NopenOS için yardımcı bellek katmanı olarak kullanmayı araştıran proje.**

> **Güncel deneysel geliştirme: [v0.8.5 Reserved VRAM](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5).** Henüz güvenli, kullanılabilir bir gerçek VRAM SWAP ürünü yok. Güvenlik kararı: **`KERNEL_SWAP_BLOCKED`**.

[English](README.md) · [Güncel kod](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5) · [Son durum](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/STATUS.md) · [Mimari](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/ARCHITECTURE.md) · [Güvenlik](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/SAFETY.md) · [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10)

## Nereden başlamalı?

GitHub'ın varsayılan **`main` dalı eski Vulkan araştırmasını ve NBD arşivlerini koruyor**. Kök CMake dosyası güncel v0.8.5 derlemesi değildir. Güncel gelişmeleri görmek ve güvenli testleri çalıştırmak için:

```fish
git clone --branch develop/v0.8.5 https://github.com/nopenstudio/nopen-vram-swap.git
cd nopen-vram-swap
cmake -S . -B build -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^(v083_|v084_|v085_|repository_)' --output-on-failure
```

Bu derleme **GPU belleği tahsis etmez ve Linux SWAP açmaz**. Gerçek SWAP deneylerini başlatan eski NBD/`ublk` araçlarını ana bilgisayarında çalıştırma; mevcut zram yapılandırmasını koru.

## Şimdiye kadar ne oldu?

- **Başarılı:** RX 9060 XT'de 1 GiB OpenCL veri bütünlüğü ve 15 saniyelik GPU-only Holder doğrulandı.
- **Araştırılıyor:** Holder kapanınca VRAM sayacı hemen başlangıç seviyesine dönmüyor; neden ve ne zaman düştüğü bilinmiyor.
- **Henüz yok:** 4 GiB fiziksel VRAM koruması, oyunlara gerçek 12 GiB sınırı, güvenli kernel SWAP ve GPU sıfırlamasından veri kurtarma.
- **Önemli geçmiş hata:** 4 GiB gerçek GPU-backed SWAP + Bannerlord denemesi sistemi tamamen dondurdu. Eski NBD denemeleri de ağır hatalar oluşturdu.

**İlk kararlı v1.0 sürümü yayımlanmadı.** NopenOS, Linux-zen tabanlı kendine özgü ve GNU kullanıcı alanına bağımlı olmayan sistem olarak planlanıyor. Mevcut Python ve Linux test araçları yalnızca geliştirme içindir.
