# Nopen VRAM SWAP v0.3.0-dev — sayfa getirme maliyeti ve faydalı prefetch

**Durum:** deneysel; ilk kararlı sürüm v1.0. **Gerçek Linux SWAP hâlâ yok.**
Dal: develop/v0.3.0, kaynak: develop/v0.2.0. NopenOS Linux-zen tabanlı ve GNU'suz hedef bir işletim sistemidir; mevcut Linux/OpenCL kullanıcı alanı prototipi NopenOS çekirdek modülü değildir.

## v0.2 gerçek donanım delilleri

Ryzen 5 7500F + RX 9060 XT + PCIe 5.0 x16:
- 64 MiB GPU havuzu; büyük blok H2D 20,031.25 MiB/s, D2H 22,716.70 MiB/s, beş geçişli.
- GPU genel VRAM tüketimi deney sırasında en fazla 233.55 MiB, GTT 6.07 MiB arttı. Bu değerler yalnızca GPU buffer'ının yerleşimini kanıtlamaz; diğer GPU tüketimleri ve sürücü önbellekleri de etkilidir.
- Süreç tepe RSS 233.12 MiB, başlangıca göre deney sonu GPU VRAM sayacı +132.71 MiB. Net RAM ekonomisi henüz doğrulanmamıştır.
- 4 KiB senkron aktarım v0.2 deneyinde yaklaşık 141–143 MiB/s; NVMe ile karşılaştırılabilir gerçek sayfalama performansı değildir.

## Eklenen v0.3 uygulaması

Cooperative nopen_memory_prefetch(manager, first_page, count) fonksiyonu 1–16 bitişik VRAM sayfasını tek çoklu-sayfa GPU aktarımıyla önceden getirir.

**Güvenlik:** Aralık tamamen GPU durumda olmalıdır. Tüm sayfalar 64 KiB'e kadar geçici tamponda checksum ile doğrulanır, ancak sonra CPU'da yer açılıp valid sayfalar commit edilir. GPU okuma başarısızsa veya checksum yanlışsa hiçbir canlı CPU sayfası tahliye edilmez. Eviction sırasında sonradan hata oluşursa kısmi ama bütünlüğü korunmuş prefetch mümkündür; tam işlem atomikliği garantisi verilmez. Prefetch opt-in, tek uygulama threadi, en fazla 16 sayfa, yeni 64 KiB staging tahsisi lazy olarak yapılır.

Yeni istatistikler: prefetch_batches, prefetch_pages, prefetch_hits, prefetch_wasted, prefetch_failures, demand_page_ins. Kullanılmadan tahliye edilen prefetch sayfası israf sayılır; hayalî cache hit olarak işlenmez.

## Yerel test (gerçek RX 9060 XT)

~~~bash
cd ~
git clone --branch develop/v0.3.0 --single-branch https://github.com/nopenstudio/nopen-vram-swap.git nopen-v030
cd nopen-v030
cmake -S v030 -B build-v030 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v030 --parallel
ctest --test-dir build-v030 --output-on-failure
~~~

Ardından üç ayrı süreçte aynı **sıralı 4MiB iş yükü** (güvenli 64MiB GPU havuzu):

~~~bash
for n in 0 4 16; do
    ./build-v030/nopen-pager-bench \
       --backend opencl --pool-mib 64 --logical-pages 1024 \
       --ram-pages 32 --prefetch-pages "$n" --passes 2 \
       | tee "$HOME/nopen-v030-prefetch-$n.log"
done
~~~

Bunlar 4KiB uygulama API sayfalarıdır, Linux userfaultfd eksik sayfa hatası testleri DEĞİLDİR. Gerçek sistem swap'i, otomatik Bannerlord memory tier, root/sudo, NBD veya swapon kullanılmaz. /dev/zram0 kalabilir. Kullanıcı işleminde herhangi bir veri uyuşmazlığı PAGER_PASS üretmez. Rastgele/hot-page çalışma kümesi optimizasyonu henüz tamamlanmadı; sıralı prefetch başarısı rastgele uygulama için garanti değildir.

## v0.3 çıkış kriterleri

1. Aynı iş yükünde single-page vs 4/16-page prefetch: sayfa başına maliyet, toplam süre ve doğrulanmış sayfa sayıları.
2. Sıralı, yerellik içeren ve rastgele yüklerde prefetche fayda/zarar ölçmek; israf artıyorsa adaptive prefetch'i durdurmak.
3. 4KiB gerçek demand fault gecikmesinde p50/p95/p99 ve CPU/GPU veri aktarımı; mevcut v0.2 batch performansını sahte swap kazanımı diye sunmamak.
4. RAM RSS + VRAM/GTT karşılaştırması; küçük iş yüklerinde pahalı OpenCL runtime maliyeti indirilmeli.
5. GPU transfer hatası, checksum bozulması ve iptal durumunda sadece test sürecinin etkilenmesi.

## v1.0 yol haritası — ilk kararlı sürüm

| Sürüm | Hedef | Çıkış kanıtı |
|---|---|---|
| v0.3 | Sayfa performansı + kontrollü prefetch + batched I/O | Bağımsız ve doğru sayfa bench, random-page ve p99 ölçümleri |
| v0.4 | Net RAM ekonomisi, daha büyük page arena, pinned staging | 1GiB kontrollü veri üzerinde driver+RSS dahil pozitif tasarruf |
| v0.5 | Kernel-aware Linux-zen bellek yöneticisi tasarımı | Ayrı test ortamında reclaim reentrancy / deadlock incelemesi |
| v0.6 | Fault containment, GPU reset, OOM, güç kaybı senaryoları | Sessiz veri kaybı=0, kernel panic=0, güvenli kapatma politikası |
| v0.7 | Bannerlord gerçek oyun performansı ve bellek basıncı | Net RAM tasarrufu, FPS/%1 low/frametime kıyasları ve uzun test |
| v0.8 | NopenOS GNU'suz bütünleşme | Platform bağımsız C11 policy + OS-specific driver ve güvenlik kontratı |
| v0.9 | Sürüm adayı, uzun süreli stres ve uyumluluk | Soak testi, hata kurtarma, rollback ve destek matrisi |
| v1.0 | İlk kararlı Nopen VRAM SWAP | Doğrulanmış gerçek bellek-tier işlevi, net tasarruf ve sistem düzeyi güvenlik |

Tarihler ve performans kazanımları garanti değildir. Eski NBD+OpenCL global swap Linux sistem kilitlenmesine neden olmuştu. Bu kaynaklar onun yeniden etkinleştirilmesine izin vermez. GPU driver's memory reclaim ve GPU I/O bağımlılığı deadlock yaratabildiği için kernel integration yalnızca ayrı yalıtılmış test düzeninde ele alınacaktır.
