# Nopen VRAM SWAP

**Linux ve gelecekteki NopenOS için AMD GPU VRAM belleği araştırması. Güncel yolumuz OpenCL.**

> **Güncel kaynak kodu:** [`develop/v0.8.5`](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5)  
> **Hedef:** **v0.9.0, çökme sorunlarının giderilmesi. HENÜZ YAYINLANMADI.**  
> **Gerçek kernel SWAP güvenlik kararı:** `KERNEL_SWAP_BLOCKED`. Eski GPU swap başlatıcılarını günlük bilgisayarda çalıştırma.

[English](README.md) · **[Kritik iş emirleri](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/CRITICAL_WORK_ORDERS_v0.9.md)** · **[Issue #18: ana takip](https://github.com/nopenstudio/nopen-vram-swap/issues/18)** · [Durum](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/STATUS.md) · [Güvenlik](https://github.com/nopenstudio/nopen-vram-swap/blob/develop/v0.8.5/docs/SAFETY.md)

## Çökme Sorunu İçin Kritik İş Emirleri ve Uygulamalar

**On maddelik P0/P1 iş emri sırası** artık resmî: deadline/fail-closed → reclaim bağımsızlığı incelemesi → ACK verilmiş sayfa koruma mimarisi → VRAM/GTT residency → reclaim bağımsız I/O → worker/GPU kurtarma → kuyruklar → zram politikası → telemetri/dayanıklılık → bağımsız donanım kabulü.

[İş emri takip tablosu](https://github.com/nopenstudio/nopen-vram-swap/issues/18) her maddeyi ayrı GitHub Issue'ya bağlar. **İE-01'in ilk uygulaması [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17) taslağında.** Bu, henüz tüm swap arızalarının çözüldüğü anlamına gelmiyor. Sonraki iş emri **İE-02: reclaim bağımsızlığı incelemesi**.

**v0.9.0 bir sürüm hedefidir; henüz sürüm dosyası, etiketi veya yayın yoktur.** 28 Ekim swap WRITE timeout'u ve 31 Ekim 4 GiB gerçek GPU swap + Bannerlord sistem donması çözülmeden canlı swap açılmayacak.

## Depoda nereden başlamalı?

| Yer | Açıklama |
|---|---|
| [`develop/v0.8.5`](https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.8.5) | Güncel OpenCL, IPC/ublk deneyleri, testler ve teknik belgeler |
| [Issue #18](https://github.com/nopenstudio/nopen-vram-swap/issues/18) | v0.9.0 için tek resmî iş emri takibi |
| [PR #17](https://github.com/nopenstudio/nopen-vram-swap/pull/17) | Ayrı dalda ilk deadline düzenlemesi |
| `main` | **Kamuya açık karşılama sayfası ve eski kaynak arşivi**. Kök Vulkan prototipi artık tercih edilen backend değil |

Güvenli, sadece CPU/mock testleri için:

```sh
git clone --branch develop/v0.8.5 https://github.com/nopenstudio/nopen-vram-swap.git
cd nopen-vram-swap
cmake -S . -B build -DNOPEN_OPENCL=OFF
cmake --build build --parallel 2
ctest --test-dir build -R '^(repository_|v060_isolated_gpu_process_deadline_and_ipc_checksum|v060_issue10_trace_mock_only|v070_fatal_gpu_ipc_forensic_log|v083_|v084_|v085_)' --output-on-failure
```

Eski Vulkan, NBD ve deneysel swap kodları tarihsel kanıt için korunuyor; yeni ürün yolu değiller. **Bütün kritik işler bitmeden ve ayrı kurtarılabilir donanım kabulü geçilmeden v0.9.0 yayımlanmayacak.**
