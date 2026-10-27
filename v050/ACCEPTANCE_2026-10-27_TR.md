# Nopen VRAM SWAP v0.5.0-dev — Deneysel prototip kod kabul raporu

**27 Ekim 2026. Karar: PROTOTİP KODU TAMAMLANDI / SİSTEM SWAP GÜVENLİĞİ HENÜZ KANITLANMADI.** İlk kararlı sürüm **v1.0** olacaktır. Bu belge v0.5 dalı için kod dondurma ve sonraki kabul kapılarının sınırıdır. Genel kullanıma hazır yazılım sürümü değildir.

## Doğrudan RX 9060 XT kanıtları

- AMD gfx1200 / OpenCL, Ryzen 5 7500F, PCIe 5.0 x16, 8GiB RAM.
- Yerel v0.5 CTest: **24/24** geçti; 1.02 saniyede tamamlandı.
- Yeni `nopen-blk-check`: **1000 random blok I/O (510 write, 490 read)**; **1024/1024 4KiB sayfa son doğrulaması**; `elapsed_s=0.046826`; `process_maxrss_kib=110076`.
- Çıktı: `BLOCK_IO_PASS ... kernel_block_device_created=NO not_system_swap=YES`. Süre hem işlemleri hem 1024 sayfalık son veri kontrolünü içerir; blok başına saf I/O latency ölçümü değildir.
- v0.4 gerçek 256MiB baz sonucu: CPU 264496KiB RSS; OpenCL GPU 110104KiB RSS; 154392KiB (150.77MiB; %58.37) **süreç** RAM tasarrufu. 65536/65536 sayfa doğrulandı. OpenCL verify=4.296529s, normal RAM verify=0.362705s. Bu, tüm sistem RAM kazancının veya oyun hızlanmasının kanıtı DEĞİL.

## v0.5 kod teslimleri

1. Linux `/proc/meminfo` + PSI `/proc/pressure/memory` + AMDGPU sysfs + NBD aktif-swap red dedektörü; yalnızca okuma.
2. GNU'suz NopenOS için tasarlanmış, dış sembol içermeyen freestanding ISO C11 politika motoru; NORMAL/CONSERVATIVE/PAUSE/REFUSE.
3. Gerçek Linux `userfaultfd` pager'ını ayrı process olarak yürütüp pressure/VRAM takibi yapan; hata, süre sınırı, GPU kaybı olasılığında fail-closed çalışan watchdog.
4. En fazla 16 x 4KiB hizalı GPU blok I/O işlemi; sıfır başlangıç, checksum doğrulaması, başarısız kısmi yazmada POISONED state.
5. `ublk` blk-mq `libublksrv` callback kaynağı, deneysel/VM-only başlatma sınırı; gerçek AMD OpenCL blok backend'ine bağlı.
6. Upstream libublksrv commit `6d0d922614ec18e6f2b5d476af8d540a1d9c1de8` üzerinde CI'da C++20 syntax **VE TAM LINK** yapıldı; hiçbir blok aygıtı oluşturulmadı, çalıştırılmadı.
7. İş akışı: mock+regression, real OpenCL compile, ASan/UBSan, ublk syntax+full link, toplam **4/4 başarılı**; GitHub Actions run 40573591659 (v0.5 branch commit 20a2e437d142f5ee31f2a775017a6a3fd6f757ae).

## Kesinlikle tamamlanmış OLMAYAN şeyler

- Gerçek Linux kernel `/dev/ublkb*` aygıtının VM içerisinde başlatılması, read/write, stop/recover lifecycle, kernel io_uring ublk completion end-to-end; **CI sadece kodun bağlandığını doğruladı**.
- `swapon` veya global kernel swap, `frontswap`, kernel direct reclaim hook, transparente Bannerlord bellek sayfalaması. Hiçbiri çalıştırılmadı/sağlanmadı.
- Bellek baskısı altında `ublk` server'ın ve AMDGPU driver'ın ileri ilerleme (forward progress) ve deadlock-free olduğuna dair kanıt.
- GPU reseti, context/device loss, OOM, güç kesintisi, kullanıcı süreç ölümü sonrası güvenli geri getirme. Bunlar v0.6 ana hedefi.
- Tam GPU+GTT+driver/kernel dahil pozitif net tüm-sistem RAM ekonomisi; 1GiB gerçek donanım karşılaştırması; 4KiB p99.

## Teknik sürüm kararı

**v0.5.0-dev deneysel prototip kodu kapatılabilir.** Kullanıcı bilgisayarında güvenli ilk GPU data-plane testleri başarılı, çekirdek arayüz kaynak kodu upstream kütüphaneyle bağlanıyor. Bundan ötesine 'çalışan güvenli sistem SWAP' denemez. v0.6 aşamasında önce ayrı VM/ayrı GPU/dedike test sistemi ile `ublk` kernel lifecycle; ardından GPU reset, I/O cancellation, OOM, fallback ve deadlock incelemesi yapılmalı.

**Kullanıcı ana bilgisayarında hiçbir koşulda `swapon` / NBD / deneysel ublk kernel swap testleri yok.** İlk kararlı sürüm v1.0.

Kanıt: https://github.com/nopenstudio/nopen-vram-swap/actions/runs/40573591659
