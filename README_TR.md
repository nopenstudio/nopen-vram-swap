# Nopen VRAM SWAP

**Linux ve gelecekteki NopenOS için AMD GPU belleği araştırma projesi.**  
**Güncel geliştirme: v0.8.5 Reserved VRAM. Henüz kullanılabilir bir gerçek kernel SWAP ürünü değil.**

[English](README.md) · [Durum](docs/STATUS.md) · [Mimari](docs/ARCHITECTURE.md) · [Güvenlik](docs/SAFETY.md) · [Kurulum ve test](docs/DEVELOPMENT.md) · [Sürüm geçmişi](docs/RELEASES.md)

> **GÜVENLİK: KERNEL_SWAP_BLOCKED.** Eski NBD ve 4 GiB ublk üzerinden etkinleştirilen gerçek SWAP denemeleri ağır hatalara ve tam sistem donmasına yol açtı. Ana bilgisayarında arşivdeki SWAP başlatıcılarını çalıştırma. Mevcut **zram** yapılandırmasını koru.

## Gerçekten neler doğrulandı?

| Alan | Başarı | Kanıtlanmayan |
|---|---|---|
| OpenCL GPU deposu | RX 9060 XT'de 1 GiB alanın tamamına yazma/okuma PASS | Fiziksel VRAM sabitlemesi |
| v0.8.5 Holder | 1 GiB havuz 15 saniye tutuldu, başlangıç/son bütünlük PASS | Kapanış sonrası fiziksel bellek iadesi |
| VRAM/GTT sayacı | Gerçek AMDGPU sayaçları okundu | Oyunların 12 GiB görmesi |
| v0.8.4 | Olası reclaim döngüsü modellenip mock IPC denendi | Gerçek kilitlenmenin kök nedeni |
| v0.8.3 | İşçi çökmesi, zaman aşımı, bozuk mesaj testleri PASS | Gerçek kernel SWAP güvenliği |
| Issue #10 | 256 MiB ham ublk, 165/165 I/O işlemi PASS | Etkin SWAP testi |

**Son gözlemler:** 1 GiB Holder öncesinde toplam VRAM kullanımı yaklaşık **763 MiB**, havuz açıkken **1.944–1.957 MiB**, hemen sonrasında **1.931 MiB** idi. Ertesi günkü 12 salt-okunur ölçümde ise **399–426 MiB** görüldü. Yüksek kullanım devam etmiyor; ancak aradan saatler geçtiği ve yeniden başlatma/süreç geçmişi bilinmediği için belleğin **ne zaman ve nasıl** serbest kaldığı henüz kanıtlanmadı. [Tam rapor](docs/reports/2026-10-31-rx9060xt.md).

## Klasör yapısı

| Klasör | Rolü |
|---|---|
| `v085/` | **Güncel:** Reserved VRAM ön kontrolü, sayaç gözlemi ve GPU-only Holder |
| `v084/`, `v083/` | Bellek döngüsü ve GPU işçisi güvenilirlik testleri |
| `v080/`, `v070/`, `v060/` | GPU deneyleri, ublk, IPC ve gölge depolama |
| `v050/` … `v012/` | Tarihsel altyapı; bazı komutlar riskli olabilir |
| `src/` | Eski isteğe bağlı Vulkan araştırması |
| `legacy/` | Eski NBD arşivi, **çalıştırmak için değil** |
| `docs/` | Güncel mimari, güvenlik, rapor ve geliştirme belgeleri |

Önceki sürüm klasörlerini silmiyoruz: yeni sürümlerin testleri eski kaynaklara bağlı. Ancak GitHub ana sayfasında artık tek bir **güncel durum** gösteriliyor.

## Güvenli derleme ve test

Kök CMake, varsayılan olarak gerçek GPU belleği kullanmayan mock testler derler:

```fish
cmake -S . -B build -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^v08[345]_' --output-on-failure
```

Sadece salt-okunur VRAM/GTT kontrolü:

```fish
python3 v085/tools/reserved_vram_preflight.py --pool-mib 4096
python3 v085/tools/reserved_vram_observe.py --samples 5 --interval-seconds 1
```

`PREFLIGHT_PASS` ifadesi 4 GiB ayırdığımız anlamına gelmez. Gerçek OpenCL Holder ayrı, izin isteyen GPU-only deneyidir; ayrıntılar [v085 rehberinde](v085/README.md).

## Sıradaki araştırma

1. Holder kapandığında neden VRAM sayacı hemen eski seviyeye dönmüyor?
2. AMDGPU sürücüsünde gerçekten korunmuş, tahliye edilmeyen bir VRAM alanı oluşturabilir miyiz?
3. Kernel bellek baskısı sırasında Nopen I/O yolunun kesin ilerlemesini ve GPU resetinden veri kurtarmayı nasıl sağlarız?
4. Bu sorunlar çözülmeden **gerçek SWAP açılmayacak**.

[Güncel geliştirme](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5) · [Issue #10](https://github.com/nopenstudio/nopen-vram-swap/issues/10) · [PR #14](https://github.com/nopenstudio/nopen-vram-swap/pull/14)

NopenOS, Linux-zen tabanlı ama kendine özgü ve GNU kullanıcı alanına bağımlı olmayan bir işletim sistemi olarak tasarlanıyor. Mevcut Python ve Linux araçları yalnızca geliştirme/test içindir. **İlk kararlı sürüm henüz yayımlanmadı.**
