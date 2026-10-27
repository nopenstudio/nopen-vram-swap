# Nopen Memory Fabric — v0.12.0-alpha.4

**Durum:** Sistem SWAP'i **DEĞİL**. Alpha.3'te RX 9060 XT üzerinde gerçek OpenCL + `userfaultfd` testi başarıyla geçti (`faults=2992`, `pageouts=2976`, `pageins=2480`, `peak_ram_pages=16`). Alpha.4 bunun üzerine **rastgele sayfa erişimi, yazma ağırlıklı yük, veri bütünlüğü ve gecikme dağılımı** ekler.

**Risk:** Eski `/dev/nbd15` global SWAP mimarisi SIGBUS ve PID1 kernel panic üretti. Alpha.4 gerçek Linux SWAP'i etkinleştirmez; sistem geneli takas yapmaz; `sudo`, `mkswap`, `swapon`, NBD ve çekirdek modülü gerektirmez. Başka uygulamaların belleğine müdahale etmez.

## Yeni

- `nopen-vram-demand --stress`: uygulama kapsamındaki anonim mmap sayfalarını Linux `userfaultfd` ile yönetir; tam dolu RAM kota havuzundan fault-age kurbanı seçilip AMD OpenCL GPU tamponuna kopyalanır, `MADV_DONTNEED` ile gerçek anonim sayfa bırakılır. **Gerçek RAM kotası** yalnızca takip edilen resident sayfalar içindir; prosesin tamamını sınırlamaz.
- Tekrarlanabilir karışık okuma/yazma erişimleri: `--seed`, `--hot-pages`, `--hot-percent`, `--write-percent`, `--operations`. Sıcak/soğuk sayfa dağılımını farklı koşullarda sınar. Tek uygulama iş parçacığıyla çalışır; eşzamanlı gerçek uygulama belleği takibi yoktur.
- Her rastgele erişimde beklenen **4 KiB** veri yeniden hesaplanıp doğrulanır. Yazma ile sayfa sürümü artırılır; test sonunda tüm mantıksal sayfalar *zorunlu olarak* tekrar okunur. Veri bozuksa `NOPEN_ISOLATED_FATAL`, başarı raporu **yok**.
- Gecikme ölçümü: tüm işlemlerin gerçek en yüksek süresi `op_max_us`; en fazla 65.536 örneğin p50/p95/p99 değerleri. Bunlar **4 KiB erişim + örüntü/doğrulama + gerekirse UFFD/GPU sayfa hatası** toplamını ölçer, yalnızca PCIe veya RAM erişim gecikmesi değildir. Belirli bir performans değeri garanti edilmez.
- `process_maxrss_kib` toplam süreç tepe resident belleğini gösterir; GPU rezervasyonu ve sürücü tahsisleriyle eşit değildir.
- `STRESS_PROGRESS` (10.000 işlemde bir) + `STRESS_PASS` son doğrulaması. En fazla 2.000.000 işlem, 16.384 mantıksal sayfa ve stres modunda **256 MiB GPU tahsis tavanı**; daha yüksek veya geçersiz değerler reddedilir.
- Mevcut alfa.1–3 testleri, okuma/yazma hata enjeksiyonları ve checksum reddi korunmuştur; global SWAP'i test etmez.

## Garuda Linux (gerçek RX 9060 XT) test adımları

```bash
cd ~
git clone --branch develop/v0.12.0-alpha.4 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v012a4
cd ~/nopen-v012a4
cmake -S v012 -B build-v012-a4 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a4 --parallel
ctest --test-dir build-v012-a4 --output-on-failure
swapon --show  # yalnızca mevcut ZRAM; /dev/nbd15 varsa TEST ETME
./build-v012-a4/nopen-vram-demand --probe
```

Ardından **Bannerlord kapalıyken**, masaüstünün ve diğer çalışmalarının kaydedildiğinden emin olarak önce 10.000 işlem sınaması:

```bash
timeout --signal=TERM --kill-after=5s 120s \
  ./build-v012-a4/nopen-vram-demand \
  --backend opencl --pool-mib 64 --logical-pages 512 --ram-pages 16 \
  --stress --operations 10000 --seed 20261027 \
  --hot-pages 32 --hot-percent 80 --write-percent 30 \
  | tee ~/nopen-a4-short.log
```

`STRESS_PASS ... no_global_swap=YES`, `peak_ram_pages=16` veya daha küçük değer, `pageouts>0`, `pageins>0`, `verified_final_pages=512` beklenir. Bunlar yoksa uzun teste geçmeyin.

Kısa test geçerse, ayrı bir stres koşusu (maksimum 2.000.000 işlemdir):

```bash
timeout --signal=TERM --kill-after=5s 1200s \
  ./build-v012-a4/nopen-vram-demand \
  --backend opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 \
  --stress --operations 100000 --seed 387472 \
  --hot-pages 64 --hot-percent 70 --write-percent 50 \
  | tee ~/nopen-a4-endurance.log
```

Burada **4 MiB mantıksal veri, en fazla 128 KiB yönetilen resident sayfa havuzu ve 64 MiB GPU tamponuyla** test edilir. `timeout` süre sınırıdır; başarı kriteri değildir. Test `timeout` ile sonlanırsa sonuç **başarılı sayılmaz**.

## Kabul kapıları

1. Kaynak derleme ve CTest başarılı; GitHub CI'da **UFFD ile gerçek mock sayfa hataları** zorunlu olarak çalıştırılır. UFFD eksikse CI başarısız olmalıdır.
2. Gerçek GPU `STRESS_PASS`, bit bit bütünlük, `pageouts/pageins`, kotaya uyum.
3. Hot/cold ve okuma-yazma karışımında birden fazla seed ile aynı güvenilirlik.
4. Ayrı hata enjeksiyonunda GPU okuma/yazma/bozulma durumu yalnızca test sürecini durdurur.
5. Bir sonraki aşamada GPU reset/hang toparlanması, pinlenmiş kaynaklar, ilerleme garantisi, NopenOS çekirdek VM entegrasyonu ve gerçek global SWAP ile **yeni** bir güvenlik tasarımı gerekli. Bunlar alpha.4'te henüz yoktur.

Önceki çöküş nedeniyle **NBD servisini yeniden etkinleştirmeyin**.