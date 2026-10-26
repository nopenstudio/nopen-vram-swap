> **v0.12.0-alpha.3:** Yeni `nopen-vram-demand`, bellek sayfasına doğrudan erişimde `userfaultfd` ile hata yakalayarak resident sayfa kotası dolunca VRAM'e sayfa çıkarır. [Kurulum ve gerçek GPU testi](README_ALPHA3_TR.md). Alpha.2 yönetilen sayfa API'si bu dalda da korunur.

---

# Nopen VRAM-SWAP v0.12.0-alpha.2 — yönetilen GPU sayfa havuzu

**Nihai hedef gerçek Linux SWAP'tir; bu sürüm henüz sistem SWAP'i değildir.** 26 Ekim kernel panic'ine yol açan NBD servisi bilerek kullanılmaz. Yeni program, kendi yönettiği bellek bölgesi için sınırlandırılmış RAM sayfalarını GPU belleğiyle değiştirir.

## Gerçek RX 9060 XT testinden gelen sonuç

v0.12.0-alpha.1, 64 MiB ayrık AMD OpenCL depolama alanında üç tur 256 adet 4 KiB sayfanın doğrulanması ve `userfaultfd` sayfa geri getirmesini başarıyla geçti: `faults=1024`, `restored=768`, `no_global_swap=YES`. Bu sistem genelinde RAM kazanımı demek değildir.

## alpha.2 yenilikleri

- `nopen_vram_memory` kütüphanesi: ZERO / RAM / GPU mantıksal sayfa durumları, RAM sayfası başına 4096 bayt.
- `--ram-pages` ile üst sınırı sabitlenen RAM resident havuzu; LRU'ya göre en eski sayfa GPU'ya taşınır.
- Gerçek OpenCL GPU tamponu sayfaları depolar; istenen sayfa yeniden GPU'dan okunur.
- GPU veri bütünlüğü için 64-bit hızlı checksum; yanlış sayfa geri döndürülmez.
- GPU yazma başarısızsa RAM'deki sayfanın son geçerli kopyası korunur. GPU okuma başarısızsa çalışan RAM sayfaları atılmaz.
- Yerel Release ve ASan/UBSan koşularında 5 test PASS. Gerçek AMD GPU'da yeni **managed** yol henüz test edilmedi.
- Önceki alpha.1 `nopen-vram-lab --demo` korunur, ancak **henüz yeni LRU yöneticisiyle entegre değildir**. Bu bağ, alpha.3'ün hedefidir.

**Kapsam:** tek iş parçacıklı/kooperatif API. Arbitrary malloc/Steam/Bannerlord belleğini izleyemez. Sistem genelinde `swapon`, `/dev/nbd`, root ve kernel modülü kullanmaz. ZRAM ayarlarını değiştirmez.

## Derleme — Garuda Linux

Eski alpha.1 çalışma klasöründen bu ayrı dala geç:

```bash
cd ~/nopen-v012-lab
git fetch origin develop/v0.12.0-alpha.2
git switch -c develop/v0.12.0-alpha.2 --track origin/develop/v0.12.0-alpha.2
cmake -S v012 -B build-v012-a2 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a2 --parallel
ctest --test-dir build-v012-a2 --output-on-failure
swapon --show
```

`swapon --show` üzerinde yalnızca **önceden kullandığın ZRAM** olmalı; `/dev/nbd15` hâlâ etkinse deneme başlatma.

## İlk yönetilen bellek testi

```bash
./build-v012-a2/nopen-vram-managed --backend mock --pool-mib 4 --logical-pages 256 --ram-pages 8 --rounds 2
```

Bu komut 1MiB mantıksal çalışma kümesini yalnızca **32KiB resident CPU belleğiyle** yönetmeye çalışır. Mock test GPU kullanmaz.

Gerçek kart için, **Bannerlord kapalıyken** ve sistem kararlı durumdayken:

```bash
timeout -s TERM -k 5s 120s ./build-v012-a2/nopen-vram-managed \
  --backend opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 --rounds 3
```

Burada 64MiB GPU tamponu önceden ayrılır. Mantıksal verinin 4MiB'si kullanılır; en fazla 128KiB RAM resident havuzu vardır (test kodunun, OpenCL sürücüsünün, sayfa tablolarının ve DMA sürücüsü tahsislerinin RAM'i hariç). Kopyalar RAM'e geri getirildikten sonra bit bit doğrulanır.

Beklenen çıktı:

```text
ROUND_PASS round=1 ...
ROUND_PASS round=2 ...
ROUND_PASS round=3 ...
MANAGED_PASS version=0.12.0-alpha.2 backend=opencl ... pageouts=... pageins=... no_global_swap=YES
```

`pageouts` ve `pageins` değerleri sıfırdan büyük olmalı, `corruptions`, `read_failures`, `write_failures` sıfır olmalı. Sadece bu kabul edilirse daha büyük ve eşzamanlı testleri konuşacağız.

> **Bannerlord üzerinde bu dalda gerçek SWAP açma!** Bu sürümde PID 1'i etkilemeyen denetimli bellek deposu geliştiriliyor. Sonraki kernel entegrasyonu için şartlar [gerçek SWAP yol haritası](docs/TRUE_SWAP_ROADMAP_TR.md) belgesinde.