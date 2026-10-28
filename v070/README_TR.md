# ÖNEMLİ — v0.7 fiziksel kernel SWAP güvenlik engeli (28 Ekim 2026)

**RX 9060 XT gerçek sistem SWAP testinde kernel `Read-error on swap-device` hataları görüldü ve sistem kullanılamaz duruma geldi. Fiziksel `start` komutu sonraki testler için kodda geçici olarak engellendi.** `stop` ve `status` komutları korunmuştur. Sorun çözülene kadar tekrar `swapon` çalıştırmayın.

- Hata raporu: https://github.com/nopenstudio/nopen-vram-swap/issues/7
- **Yeni kanıt (28 Ekim 21:30):** İlk hata `21:25:14.462` tarihinde `timeout error ... dev ublkb23 ... (WRITE)`. Ardından onlarca bin bastırılmış yazma/okuma hatası, `mm/swapfile.c` uyarısı ve oturum kaybı geldi. `21:25:19` tarihinde systemd, kullanıcı oturumuyla birlikte `nopen-v070-ublk` sürecini de öldürdü; bu ilk yazma zaman aşımından **sonradır**, dolayısıyla ilk tetikleyiciyi kanıtlamaz. GPU firmware reseti bu filtrelenmiş günlüklerde kanıtlanmadı. Fiziksel SWAP testinin kabulü **BAŞARISIZ**; 35/35 mock CI bu sonucu geçersiz kılamaz.
- Test sonrasında zram kaldı (`/dev/zram0`, `USED 0B`); fiziksel Nopen aygıtı yeniden başlatma sonrasında etkin değil.

- Yeniden başlatmadan sonra salt okunur tanılama: `git pull --ff-only && bash v070/tools/collect_swap_failure.sh`
- Eski prototipin CI mock SWAP başarısı, gerçek AMDGPU altında swap-in veri kaybı güvenliğini kanıtlamadı.
- Aşağıdaki fiziksel test talimatları **tarihsel referanstır; geçerli sürümde `start` bilerek reddedilir.**

---

# Nopen VRAM SWAP v0.7.0-dev — Gerçek Linux GPU SWAP ve Bannerlord deneyi

**Durum: Deneysel GPU-backed kernel SWAP uygulaması. İlk kararlı sürüm v1.0.** Bu sürümde, v0.6 `ublk` + bağımsız `nopen-isolated-worker` gerçek OpenCL backend'ini seçili Linux SWAP cihazına bağlayabiliyor; geçmiş NBD sürücüsünü KULLANMIYOR.

## Kanıt durumu ve sınırlar

- GitHub geçici Linux VM üzerinde **gerçek `swapon` ve `swapoff`**, aktif swap kullanımı, cgroup bellek baskısı ve `/dev/ublkb24` aygıtı kaldırma test edildi. `V070_VM_SWAP_PASS` ve `V070_VM_PAGING_PASS`, cgroup'un çekirdeğe 37.986.304 bayt swap yaptırdığını doğruladı. Bu testte veri deposu **mock RAM backend**, GPU değil. Tüm sistem/oyun için gerçek VRAM hızlandırması kanıtlanmış değildir.
- Fiziksel RX 9060 XT / Linux-zen / Bannerlord çalışması için ayrı **gerçek OpenCL backend** v0.7'ye eklendi. 64, 256, 1024, 2048 ve 4096 MiB havuz kapasitesi seçilebilir. GPU'nun kapasitesinin %75'inden fazlasını ayıramaz; büyük GPU'da 4GiB güvenlik boşluğu bırakma kuralı korunur.
- `swapon` testinde mevcut zram kapatılmaz; Nopen önceliği 200, önceki zram önceliği 100 ise Nopen daha önce kullanılır. Bu, CPU/GPU bellek basıncını arttırabilir.
- `nopen-v070-ublk` yalnızca açık `NOPEN_V070_KERNEL_SWAP_TEST=EXPERIMENTAL_GPU_SWAP_CAN_HANG` onayıyla **root** olarak fiziksel sistemde çalışır; mock backend fiziksel hostta reddedilir. Yetki ve cihaz çakışması reddi, geri alma işlemi, 4KiB raw readback ve swap imzası doğrulamasından sonra `swapon` yapılır.
- Linux kernel `ublk` yalnızca blok I/O isteğini kullanıcı alanına aktarır; fiziksel GPU reseti altında kernel reclaim'in bağımsız ilerlemesi hâlâ kanıtlanmış değildir. Kernel D-state kilitlenmesinde kullanıcı alanı timeout/SIGKILL bilgisayarı kurtaramayabilir. Disk kaybından farklı olarak, VRAM swap içeriği reset/yeni sürücü context'i sonrası kaybolur.

## 1 — Tek seferlik derleme (normal kullanıcı)

Garuda/Arch üzerinde gerekli C/C++, CMake, git, liburing, ocl-icd, OpenCL header, autotools/libtool ve util-linux paketleri bulunmalıdır.

~~~bash
cd ~
git clone --branch develop/v0.7.0 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v070
cd ~/nopen-v070
bash v070/tools/build_ublk.sh
ctest --test-dir build-v070 --output-on-failure
~~~

`V070_BUILD_PASS` ve bütün CTest sonuçları başarılı olmadan SWAP açılmamalı. `build_ublk.sh` derleyicidir; swap açmaz.

## 2 — Gerçek RX 9060 XT üzerinde kernel SWAP açma

Önce oyunu kapatın, 256MiB pilot havuzla deneyin. Gerekirse 64MiB ilk smoke yapılabilir; oyun bellek etkisini görmek için daha sonra 1024MiB'ye çıkılabilir.

~~~bash
cd ~/nopen-v070
sudo bash v070/tools/vram_swap_lab.sh start 256
~~~

Komut gerçek riski açıklayacak, terminale `EVET` girilmesi istenecek. Bu işlem `modprobe ublk_drv`, 4KiB GPU'ya direct I/O testi, `mkswap`, `swapon -p 200` yapar. Yalnızca `/dev/ublkb23` üzerinde işlem yürütür; zram'ı değiştirmez, servis kurmaz, sistem açılışına eklenmez. **Ana sistem kilitlenebilir veya güç düğmesinden yeniden başlatma gerekebilir.** Bu deneyde açık dosyalar zarar görebilir; deney yapan kişi bu riski kabul etmelidir.

~~~bash
bash v070/tools/vram_swap_lab.sh status
cat /proc/swaps
~~~

`V070_SWAP_ACTIVE ... real_gpu=YES` satırı görülmeli. Ayrılmış GPU VRAM kapasitesi `mem_info_vram_used` sayaçlarında büyümeli, ancak bu sayaç GPU geneline aittir.

## 3 — Bannerlord karşılaştırması

**Aynı harita, oyun sürümü, çözünürlük, grafik ayarları ve benzer sahne** kullanılmalıdır. FPS / 1% low için MangoHud gibi ayrı kare süresi ölçümü gereklidir; `game_probe.py` FPS üretmez.

Önce Nopen kapalıyken Bannerlord'u açıp PID'yi bulun:

~~~bash
pgrep -af 'Bannerlord|TaleWorlds'
python3 v070/tools/game_probe.py --mode off --pid OYUN_PID \
  --duration-seconds 120 --csv "$HOME/bannerlord-off.csv"
~~~

Ardından oyunu kapatın, v0.7 GPU SWAP'i açın, aynı sahneyi tekrar oynayın ve yeni PID ile:

~~~bash
python3 v070/tools/game_probe.py --mode on --pid OYUN_PID \
  --duration-seconds 120 --csv "$HOME/bannerlord-on.csv"
python3 v070/tools/compare_game.py \
  --off "$HOME/bannerlord-off.csv" --on "$HOME/bannerlord-on.csv" \
  --json "$HOME/bannerlord-v070-comparison.json"
~~~

`nopen_swap_used_kib_growth` artıyorsa Linux'un Nopen aygıtına swap yaptığı gösterilir; bu **hangi oyun sayfalarının** taşındığını tek başına kanıtlamaz. RSS/PSS, GPU/driver belleği ve FPS ayrı değerlendirilmelidir. `nopen_swap_used_kib_peak=0` ise swap etkin olsa da bu ölçümde kullanılmamıştır; buna RAM genişletme testi BAŞARILI denmez.

## 4 — Kernel SWAP'i kapatma

~~~bash
sudo bash v070/tools/vram_swap_lab.sh stop
~~~

Bu işlem yalnızca `/dev/ublkb23` üzerinde `swapoff` yapıp başarılı olduktan sonra `ublk del` ile aygıtı kaldırır. Bellek baskısında `swapoff` uzun sürebilir; hata verirse **aktif swap aygıtını asla zorla silmez**. Yeniden başlatma hâlinde VRAM geçici bellektir; süreç/aygıt tekrar devreye girmez.

## Sonraki kabul şartları

1. Fiziksel RX 9060 XT'de `V070_SWAP_ACTIVE` ve kullanılabilir terminal durumu.
2. `swapon --show` üzerinde Nopen kullanımı sıfırdan büyük ve `game_probe` ile doğrulanmış.
3. Bannerlord aynı sahne için Nopen açık/kapalı RSS, swap, GPU VRAM ve kare süresi karşılaştırması.
4. Kernel kilitlenmesi / AMDGPU reseti veya düşük bellek altında güvenilir geri dönüşün ayrı incelenmesi.
5. Net sistem RAM ekonomisi ve FPS performansı ölçülmeden %200–300 hızlandırma iddia edilmez.

Kernel ublk belgeleri: https://docs.kernel.org/block/ublk.html
