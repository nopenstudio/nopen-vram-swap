# Nopen VRAM-SWAP v0.12.0-alpha.3 — sayfa hatasıyla çalışan VRAM önbelleği

**Hedef değişmedi:** Daha sonra NopenOS için gerçek, güvenilir Linux takası. **Bu sürüm sistem SWAP'i değildir;** kooperatif, tek uygulama sürecindeki sınırlı RAM havuzundan otomatik GPU VRAM sayfalamasını sınar.

## Doğruladığımız önceki sonuç

26 Ekim'deki RX 9060 XT alpha.2 testi: 1024 mantıksal 4 KiB sayfa, en fazla 32 resident sayfa (128 KiB), 3 tur, 6144 pageout, 3072 pagein, 0 checksum/I/O hatası, 0.3361 s. Bu uygulamanın *resident sayfa kotası*; diğer CPU RAM ve OpenCL sürücü tahsisleri buna dahil değildir.

## Alpha.3 yeniliği

Yeni `nopen-vram-demand` programı, uygulamanın kendi `mmap` bölgesinde 4 KiB sayfa hatalarını `userfaultfd(UFFD_USER_MODE_ONLY)` üzerinden alır. Her sayfa hatasında:

1. ZERO ise sıfır sayfa hazırlanır; GPU ise OpenCL'den sayfa okunup FNV-1a checksum doğrulanır **önce**, olası RAM tahliyesi **sonra** yapılır.
2. Resident sayfa kotası doluysa sayfa hatasının en eskisine sahip başka sayfa VRAM'e yazılır. GPU yazımı başarılı olmadan `MADV_DONTNEED` çağrılmaz.
3. `UFFDIO_COPY` istenen sayfayı uygulamaya geri yükler. Kullanıcı uygulaması sayfaya doğrudan `memcpy`/`memcmp` yapar; artık manuel `nopen_memory_read/write` çağırması gerekmez.
4. GPU read/write hatası ya da checksum uyuşmazlığı: **yalnızca deney programı** özel hata koduyla çıkar. Sistem SWAP'i veya NBD kullanılmaz.

**Önemli sınırlar:** Bir uygulama thread'i dışında arena'ya erişim yasaktır; pagefault handler thread'i ayrıdır. Victim seçiminde gerçek LRU değil, **son sayfa hatası yaşına** bakılır. Yazılan sayfaların kirli takibi henüz yoktur: tahliyede her sayfa yazılır. Kernel SWAP, kernel module, root servisi, Bannerlord'a şeffaf RAM, GPU reset sonrası garanti yoktur. `--ram-pages` bütçesi sadece yönetilen resident *arena* sayfalarıdır; OpenCL driver, sayfa tabloları, buffers ve program belleği bunun dışındadır.

## Garuda kurulum

Önce `swapon --show` üzerinde `/dev/nbd15` aktif **olmadığını** doğrula. Tüm komutları sıradan kullanıcıyla çalıştır, `sudo su` kullanma.

```bash
cd ~
git clone --branch develop/v0.12.0-alpha.3 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v012a3
cd ~/nopen-v012a3
cmake -S v012 -B build-v012-a3 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v012-a3 --parallel
ctest --test-dir build-v012-a3 --output-on-failure
./build-v012-a3/nopen-vram-demand --probe
```

`--probe` UFFD desteklenmiyorsa root ya da sysctl güvenlik ayarı değiştirme. Testlerde `SKIP` görünür ve GPU ile birlikte çalışma kanıtlanamaz.

### Küçük mock testi

```bash
./build-v012-a3/nopen-vram-demand \
  --backend mock --pool-mib 4 --logical-pages 128 --ram-pages 8 --rounds 2
```

### Ayrık AMD GPU testi — Bannerlord kapalıyken

```bash
timeout --signal=TERM --kill-after=5s 120s \
  ./build-v012-a3/nopen-vram-demand \
  --backend opencl --pool-mib 64 --logical-pages 512 --ram-pages 16 --rounds 3
```

Aranacak: `DEMAND_PASS`, `pageouts>0`, `pageins>0`, `peak_ram_pages<=16`, `no_global_swap=YES`. Çıktıdaki `verified_mib` toplam doğrulanan uygulama verisidir; GPU bandwidth sayısı değildir.

## Fail-closed test

`ctest` eklentisinde 3 farklı deney **ayrı süreçler** üzerinde çalışır: `--inject-read-failure` 73, `--inject-write-failure` 73, `--inject-corruption` 74 çıkış kodu verir; hiçbirinde sahte `DEMAND_PASS` oluşmamalı. Bu seçenekler yalnızca `--backend mock` ile açılabilir. Test betiği çalışmadan önce ve sonra `/proc/swaps` içeriğini karşılaştırır.

## Gerçek Linux SWAP için sonraki kapılar

- Desteklenen kernel MM / swap yoluna NopenOS entegrasyonu, kernel reclaim'in kendisinin GPU/servis tahsisine bağlı kalmaması.
- Önceden tahsis edilmiş DMA tamponları ve CPU kaynakları; GPU reset/hang karşısında kayıp sayfa politikasının kanıtlanması.
- Önce sanal makine ve sistem dışı testler; PID1 riskini tekrarlayan NBD swap yoluna geri dönülmemesi.
- Oyun ile GPU VRAM rekabeti ve VRAM rezervasyonunun dinamik bütçeyle yönetilmesi.

Kaynak kod: `v012/src/nopen_fault_managed.c`. Önceki `nopen-vram-managed` ve `nopen-vram-lab` testleri korunmuştur.