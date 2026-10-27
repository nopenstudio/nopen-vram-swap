# Nopen VRAM SWAP v0.2.0-dev — link throughput araştırması

**Status:** first implementation, hardware acceptance pending. Branch: develop/v0.2.0. Alpha.5 untouched.

Alpha.5'in 4 MiB ve 64 MiB gerçek GPU sayfalama deneyleri PASS oldu; ancak 64 MiB çalışma kümesinde süreç RSS'i yaklaşık 105 MiB, normal RAM baz çizgisi yaklaşık 66 MiB idi. Net RAM kazancı henüz sağlanmadı.

İlk v0.2.0 hedefi: 4 KiB tekil senkron page-in'i gerçek PCIe bant genişliğinden ayırmak. Bu laboratuvar iki ayrı yönde kuyruklanmış asenkron OpenCL aktarımlarını, 4–4096 KiB bloklar ile ölçer. Kaynak ve hedef host tamponu clFinish tamamlanana dek korunur. Her geçiş byte-for-byte veri kontrolünden geçmeden BENCH_PASS yazılmaz.

## Kullanım (Garuda Linux)
~~~bash
git clone --single-branch --branch develop/v0.2.0 https://github.com/nopenstudio/nopen-vram-swap.git nopen-v020
cd nopen-v020
cmake -S v020 -B build-v020 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v020 --parallel
ctest --test-dir build-v020 --output-on-failure
bash v020/tools/pcie_topology.sh
./build-v020/nopen-link-bench --backend opencl --pool-mib 64 --chunk-kib 256 --passes 3
~~~
`--backend mock` yalnızca yazılım regresyon testi içindir. GPU bulunamazsa mock'a sessiz geçiş **yok**. Havuz 4–256 MiB; host tarafında iki tam havuz tamponu gerekir. Bloklar: 4, 16, 64, 256, 1024, 4096 KiB. En çok 10 geçiş.

## Performans sınırları
RX 9060 XT GDDR6 bellek bant genişliği ~320 GB/s **kart içindir**, CPU→VRAM PCIe kopyası değildir. RX 9060 XT PCIe 5.0 x16 destekler; Ryzen 7500F de Gen5 destekler; B650M modelinin tam GPU slot hızı ve negotiated bağlantı değeri ölçülmelidir. PCIe 4.0 x16 teorik ~31,5 GB/s ve PCIe 5.0 x16 teorik ~63 GB/s **tek yön**, protokol/driver üst yükü hariç.

Bu sürüm gerçek Linux SWAP, kernel modülü, fault-latency iyileştirmesi veya RAM tasarrufu değildir. Kullanıcı alanı OpenCL/POSIX Linux test aracıdır, GNU'suz NopenOS runtime değildir.

## Sonraki karar kapıları
1. H2D ve D2H bant genişliği ile blok başı sabit maliyeti ayrı ölç.
2. API katmanında contiguous batch transfer, dirty-page batching, prefetch ekle.
3. Pinned host staging, uygun kuyruk derinliği ve RSS ölç.
4. Net RAM tasarrufunu ZRAM/zswap/NVMe baseline ile ispatla.
5. NopenOS için Linux-zen VM reclaim deadlock, GPU reset ve verinin yedeklenmesi şartlarını çöz.

Eski NBD+OpenCL global SWAP çözümü kernel panic oluşturduğu için bu laboratuvar NBD veya swapon kullanmaz; etkin NBD swap varsa başlamayı reddeder.

## Deney modları

`--blocking`: her parçayı tamamlayarak ilerler (4KiB senkron aktarım maliyeti). Varsayılan mod, tüm parçaları asenkron kuyruğa koyup `clFinish` ile bekler (`mode=queued`). `--inject-corruption` sadece açık mock backend ile beklenen fail-closed testi içindir; gerçek GPU üzerinde kabul edilmez.
