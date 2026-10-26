# Nopen VRAM-SWAP v0.12.0-alpha.2

**Nihai hedef değişmedi: NopenOS'ta gerçek Linux takası.** Şu anda yalnızca uygulama kapsamındaki RAM/GPU sayfa yöneticisini geliştiriyoruz. Daha önceki OpenCL+NBD deneyleri kernel panic'e yol açtığından global SWAP yolu kapalı kalacak.

## Yeni sürüm

64 MiB OpenCL havuzuna karşı 4 KiB mantıksal sayfalar yönetiliyor. RAM'de aynı anda kaç sayfanın kalacağını `--ram-pages` belirliyor. RAM kotası dolunca en eski sayfa GPU'ya yazılıyor; sonraki erişimde doğrulanıp RAM'e alınıyor. Başarısız yazma RAM'deki son kopyayı silmiyor; başarılı okunamayan GPU sayfası yerine sahte sıfır sayfa döndürülmüyor.

```bash
cd ~/nopen-v012-lab
git fetch origin develop/v0.12.0-alpha.2
git switch -c develop/v0.12.0-alpha.2 --track origin/develop/v0.12.0-alpha.2
cmake -S v012 -B build-v012-a2 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a2 --parallel
ctest --test-dir build-v012-a2 --output-on-failure
./build-v012-a2/nopen-vram-managed --backend mock --pool-mib 4 --logical-pages 256 --ram-pages 8 --rounds 2
```

Gerçek GPU testini oyun kapalıyken gerçekleştirmek için:

```bash
timeout -s TERM -k 5s 120s ./build-v012-a2/nopen-vram-managed \
  --backend opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 --rounds 3
```

Bu **sistem SWAP'i değildir** ve Bannerlord'a ek RAM vermez; bu sürümde `userfaultfd` ile yeni LRU yöneticisi henüz birbirine bağlanmadı. Önce kullanıcı alanında tam sayfa yaşam döngüsünü doğruluyoruz, ardından NopenOS çekirdeğinde DMA ve GPU resetine dayanıklı gerçek VM entegrasyonuna geçebileceğiz.

[Detaylı test rehberi](v012/README_TR.md) · [Gerçek takas kabul kapıları](v012/docs/TRUE_SWAP_ROADMAP_TR.md)