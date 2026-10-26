# Nopen VRAM-SWAP v0.12.0-alpha.1 — Nopen Memory Fabric Lab

**Durum:** Gerçek 8 GiB OpenCL aktarımı daha önce RX 9060 XT üzerinde doğrulandı. **Bu yeni v0.12 mimarisinin Garuda üzerinde gerçek GPU + userfaultfd birleşik testi henüz yapılmadı.**

**Yeni mimari:** Programın açıkça seçtiği anonim bir `mmap` bölgesinin 4 KiB sayfaları, Linux `userfaultfd` ile uygulama düzeyinde yönetilir. Kooperatif çıkarmada sayfa OpenCL GPU tamponuna kopyalanır, `MADV_DONTNEED` ile yalnızca bu programın RAM'inden çıkarılır ve sonraki erişimde sayfa hatası dinleyen iş parçacığı GPU'dan geri getirir. **Çalışması Linux SWAP/NBD gerektirmez.**

- **Sistem genelinde SWAP değildir.** Bannerlord gibi değiştirilmemiş programlara otomatik RAM eklemez.
- Root, kernel modülü, `/dev/nbd15`, `swapon`, sistem servisleri, sysctl değişikliği, `mlockall` yoktur.
- OpenCL sadece AMD ayrık GPU'da başlar; GPU bulunamazsa RAM'e gizlice düşmez. `mock` açıkça seçilmedikçe kullanılmaz.
- **Kooperatif:** Sayfa çıkarılırken aynı bellek üzerinde eşzamanlı okuma/yazma yapılmamalı. Genel amaçlı eşzamanlı MMU/dirty-tracking bu alfa sürümünde yok.
- GPU hata verirse yalnızca demo sürecinin sonlanması mümkün; sistem takas sayfaları etkilenmez.
- Varsayılan kapasite 64 MiB, maksimum 8192 MiB. Oyunu çalıştırırken 8 GiB ayırmayın.

## Kurulum (Garuda)

```bash
sudo pacman -S --needed cmake gcc opencl-headers ocl-icd opencl-clhpp opencl-icd-loader
# Kurulu amdgpu OpenCL ICD'nizi koruyun; harici ICD olmadan gerçek GPU modu çalışmayabilir.
cd ~/nopen-vram-swap
# bu dal: develop/v0.12.0-alpha.1
cmake -S v012 -B build-v012 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012 --parallel
ctest --test-dir build-v012 --output-on-failure
```

> Arch/Garuda paket adları depoya göre değişebilir. Bir paket bulunamazsa tüm sistem yazılımını kaldırmaya/kernelsiz bırakmaya çalışmayın; `pacman -Ss 'opencl|ocl-icd'` ile karşılığını bulun. Var olan OpenCL testiniz çalıştığı için büyük olasılıkla derleyici ve ICD zaten kurulu.

Önce düşük riskli test:

```bash
swapon --show   # yalnızca mevcut zram; NBD açık olmamalı
./build-v012/nopen-vram-lab --self-audit
./build-v012/nopen-vram-lab --probe
./build-v012/nopen-vram-lab --raw --backend mock --mib 4 --pages 128 --rounds 2
./build-v012/nopen-vram-lab --demo --backend mock --mib 4 --pages 128 --rounds 2
```

`--probe` `UFFD_UNAVAILABLE` verirse normal kullanıcı `userfaultfd(UFFD_USER_MODE_ONLY)` kullanamıyor demektir. **Sysctl ayarlarını ve güvenlik kısıtlarını otomatik gevşetmeyin.** `--raw` testi yine çalışır; `--demo` gerçek sayfa hatası sınaması yapılamaz.

Sadece bunlar geçerse gerçek GPU testi, **oyun kapalıyken**:

```bash
./build-v012/nopen-vram-lab --raw --backend opencl --mib 64 --pages 256 --rounds 3
./build-v012/nopen-vram-lab --demo --backend opencl --mib 64 --pages 256 --rounds 3
```

`ROUND_PASS` her tekrarın doğrulamasıdır. `TEST_PASS ... mode=userfaultfd backend=opencl ... restored=...` ise kooperatif sayfa çıkarma / GPU sayfa geri getirmenin ilk gerçek kanıtıdır. `restored` sıfır olmamalıdır.

**Bu aşamada Bannerlord ile hiçbir gerçek NBD/SWAP deneyi yapmayın.** v0.12'nin hedefi önce ayrı uygulama alanında hatasız ileriye ilerleme sağlamaktır. Oyun ancak API entegrasyonu veya daha sonra güvenli sistem mimarisi kurulduktan sonra hedef olur.

## Geliştirici sınırları

`nopen-vram-lab --raw`: UFFD olmadan 4 KiB GPU deposu testidir. GPU transferinden önce/sonra veriyi doğrular. `--demo`: anonim mmap üzerinde userfaultfd eksik sayfa olaylarını yönetir. Demo aktif iken başlangıçta sayfalar demand fault ile sıfır olarak doldurulur, pattern yazılır, GPU'ya verilir, `madvise` ile RAM'den çıkarılır; yeniden erişimde OpenCL'den doğru sayfa `UFFDIO_COPY` ile dönmelidir.

Performans sayısı doğrulama dahil tüm test döngüsünün uygulama-tabanlı brüt I/O oranıdır; önceki bağımsız OpenCL sıralı transfer bant genişliğiyle kıyaslanmamalıdır. Herhangi bir UFFD `EPERM`, `ENOSYS`, `EINVAL` vb. hatası olması GPU'nun kusurlu olduğu anlamına gelmez.

Mimari ve gelecekte NopenOS entegrasyon şartları: `docs/ARCHITECTURE_TR.md`.