# Nopen VRAM SWAP v0.4.0-dev: 1 GiB kontrollü RAM/VRAM ekonomisi

Bu sürüm deneysel. İlk kararlı sürüm v1.0. Bu bir sistem SWAP aygıtı, otomatik Linux RAM uzantısı veya Bannerlord belleği değildir. NopenOS henüz GNU'suz Linux-zen hedefi olarak tasarlanıyor.

## v0.3 gerçek sonuçları

Ryzen 5 7500F / RX 9060 XT: sıralı 4MiB (1024 sayfa x iki geçiş) talep üzerine sayfalama 0.126339 sn, 4-sayfa prefetch 0.097281 sn (1.299x), 16-sayfa prefetch 0.083448 sn (1.514x). Bütün 2048 sayfa doğrulandı, 0 boşa prefetch; 107MiB civarı süreç RSS maliyeti. Gerçek global RAM kazancı kanıtlanmadı.

## v0.4 mimarisi

- Fresh ZERO sayfaları için nopen_memory_import_pages: en fazla 64 adet 4KiB sayfayı 256KiB toplu GPU işlemiyle yazar ve yalnızca başarılı GPU dönüşünde metadata commit eder. Hata durumunda eski sayfa durumları korunur.
- CPU baseline tüm veriyi mmap anonim RAM'de fiziksel olarak doldurur; GPU backend aynı benzersiz sayfa desenlerini VRAM'e koyar ve sadece 32 resident RAM sayfasıyla geri getirir.
- GPU prefetch 16 sayfayı checksum doğrulamasından geçirir. Her sayfa CPU veya GPU yolunda byte-for-byte doğrulanır.
- Süreç RSS ölçülür. Bu GPU RAM'ini, kernel/driver tahsislerini, GTT veya cache etkisini kapsamaz. Çıktı full-system net RAM kazanımı iddiası taşımaz.
- Havuz 4/64/256/1024 MiB sınırında. GPU'da en az 4 GiB rezerv bırakılır. CPU baseline öncesi en az 2 GiB MemAvailable ek boşluk aranır. Root ve etkin NBD swap reddedilir; zram değiştirilmez.

## Kurulum

~~~bash
cd ~
git clone --single-branch --branch develop/v0.4.0 https://github.com/nopenstudio/nopen-vram-swap.git nopen-v040
cd ~/nopen-v040
cmake -S v040 -B build-v040 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v040 --parallel
ctest --test-dir build-v040 --output-on-failure
~~~

## Üç donanım deneyi (ayrı süreç ve veri doğrulaması)

~~~bash
python3 v040/tools/compare_economics.py --binary build-v040/nopen-economics --backend opencl --logical-mib 64 --ram-pages 32 --prefetch-pages 16 | tee ~/nopen-v040-64.log
python3 v040/tools/compare_economics.py --binary build-v040/nopen-economics --backend opencl --logical-mib 256 --ram-pages 32 --prefetch-pages 16 | tee ~/nopen-v040-256.log
python3 v040/tools/compare_economics.py --binary build-v040/nopen-economics --backend opencl --logical-mib 1024 --ram-pages 32 --prefetch-pages 16 | tee ~/nopen-v040-1024.log
~~~

8 GiB RAM'li bilgisayarda 1 GiB CPU baseline gerçek 1 GiB RAM doldurur. Büyük oyunlar ve GPU kullanan uygulamalar kapalı olmalı. Her deney 240 saniyeyle sınırlıdır. Başlangıçta 64 MiB başarılı olmadan 256/1024 testlerine geçmeyin.

## v0.4 bitiş kapıları

1. 64/256/1024MiB GPU/OpenCL testlerinde tüm sayfa bütünlüğü; ölçülen süreç RSS ekonomisi.
2. Ayrı AMDGPU VRAM/GTT ve host bellek sayaçları, driver allocations; gerçek tüm-sistem RAM ekonomisi (henüz kanıtlanmadı).
3. Rastgele/hot-page workload ve p99 sayfa gecikmesi, adaptive prefetch israfı.
4. Fault injection, GPU kaybında fail-closed, timeout sınırları ve bağımsız çekirdek reclaim güvenliği.
5. Bannerlord entegrasyonuna yalnızca test edilmiş güvenli bellek mekanizmasından sonra geçilecek.

v0.5–v0.9 deneysel geliştirmeler; ilk stabil v1.0 olacak.
