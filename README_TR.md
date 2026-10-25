# Nopen VRAM-SWAP v0.10.0-alpha.1

**Aktif GPU altyapısı: Vulkan. OpenCL artık aktif kaynak kodunda ve derleme bağımlılıklarında bulunmuyor.**

Bu sürüm henüz Linux'a SWAP veya ek RAM sağlamaz. Kullanıcı alanında Vulkan üzerinden ekran kartının `DEVICE_LOCAL | HOST_VISIBLE` bellek türlerini tespit eder, belleği eşler, 4 KiB sayfalara farklı veri desenleri yazar ve iki tur geri okuyarak karşılaştırır.

## Garuda Linux kurulum

```bash
sudo pacman -S --needed cmake gcc vulkan-headers vulkan-icd-loader vulkan-tools vulkan-radeon
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/nopen-vulkan-probe --list
./build/nopen-vulkan-probe --mib 64 --samples 256
```

**İlk denemeyi yalnızca 64 MiB ile yap.** GPU türü ve bellek bütçesi uygunsa sonradan 256 MiB ve en fazla 2048 MiB deneyebilirsin. Tam oyun yükü altında deneme yapma. `sudo`, `swapon`, `mkswap` ve NBD gerekmez.

## Yanlış bir iddiada bulunmuyoruz

- `DEVICE_LOCAL | HOST_VISIBLE`, doğrudan CPU erişilebilir VRAM için adaydır. Bazı GPU/sürücülerde bu tür bulunmayabilir.
- Vulkan bellek tahsisi RAM kadar hızlı değildir. Özellikle PCIe üzerinden CPU okuması yavaş olabilir.
- VK_EXT_memory_budget sürücünün bildirdiği tahmini bütçeyi verir; kesin fiziksel GDDR garantisi değildir.
- OpenCL'de doğruladığımız SWAP-OUT / SWAP-IN sonuçları, **yeni Vulkan backend'inin** henüz kanıtladığı sonuçlar değildir.
- NBD/SWAP kilitlenmesi çözülmedi. Bilinen çökme tehlikesi sebebiyle bu sürümde hiç etkinleştirilmez.

Tarihî OpenCL paketleri sadece kaynak geçmişini saklamak için indirme paketinde `legacy/archives/` altında yer alır. Eski sürümün swap betiklerini çalıştırma.

## Plan

`alpha.1` Vulkan bellek algılama ve bütünlük; `alpha.2` sayfa gecikme/baskı testleri; `beta.1` güvenli ham blok aygıt prototipi; daha sonra ancak güvenilir ilerleme sağlanırsa gerçek SWAP.

[Architecture](docs/ARCHITECTURE.md) · [Eski sürümler](docs/RELEASES.md) · [LICENSE](LICENSE)