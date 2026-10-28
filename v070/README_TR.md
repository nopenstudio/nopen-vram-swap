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

## 28 Ekim 2026 — v0.7 çöküş sonrası mimari onarım (geliştirildi)

**Kök kanıt:** RX 9060 XT'de ilk `ublkb23` hatası saat 21:25:14'te bir **WRITE timeout** idi; onu yüzlerce/binlerce swap READ/WRITE error izledi. 21:25:19'da KDE kullanıcı oturumu sonlandırılırken `nopen-v070-ublk` sunucusu da öldürüldü; bu ikinci hata zincirini ağırlaştırdı ama ilk yazma zaman aşımının nedeni değildir.

### Kodlanan iki düzeltme

1. **Oturumdan bağımsız blok sunucusu:** Fiziksel test yönetimi artık `systemd-run --system` ile `nopen-v070-ublk.service` oluşturur. Bu servis `system.slice` altında çalışır, KDE `user@1000.service` yaşam döngüsüne bağımlı değildir. `stop` işlemi önce swapoff, sonra ublk del ve son olarak sistem servisini sonlandırır. GitHub geçici Linux VM'inde gerçek ublk I/O ve sistem cgroup testi eklendi.
2. **GPU kaybında okunabilir sayfa garantisi için disk shadow:** `nopen_shadow` her başarılı sayfa yazmasından önce aynı içeriği bağımsız SSD/NVMe üzerindeki `O_DIRECT | O_DSYNC` dosyasına yazar. Dosya `O_EXCL` ile yepyeni oluşturulur, bütün sayfaları swap etkinleşmeden önce sıfır yazılarak fiziksel tahsis edilir. Ardından VRAM'e yazılır. GPU işçisi hata verir veya 2s timeout yaşarsa Nopen kalıcı disk yedeğine düşer, son doğrulanmış sayfaları buradan okur; devam eden yazmaları da yedeğe yapar. `CRC` hatası ya da backup diski hatasında `-EIO` döner. Fiziksel testte yedek dosyası zorunludur; yedeksiz `swapon` artık reddedilir.

**Bedeli ve sınırlama:** Bu yöntem artık *saf VRAM SWAP değil*, NVMe yazma-through + VRAM okuma önbelleğidir. NVMe I/O gecikmesi, ek disk alanı ve dosya sistemi bağımlılığı vardır. Yedek disk OOM/reclaim'de kernel I/O için de kilitlenirse test hâlâ sistemi dondurabilir; reset, cgroup OOM ve daemon ölümünden sonraki kernel SWAP güvenliği **tam kanıtlanmadı**. 2–3x oyun hızlanması iddia edilmiyor.

**Mock regresyon:** 500 ms GPU işçisi takılmasından sonra 1.024/1.024 sayfa backup üzerinden geri okundu ve yeni yazma/okumalar doğru devam etti. Gerçek `ublk` mock backend ile worker `SIGKILL` ve disk yedekli I/O testini CI'a ekledik. AMDGPU fiziksel reset testi hâlâ yapılmadı.

### Fiziksel test kuralı

`v070/tools/vram_swap_lab.sh start` komutu şu anda **BLOKE**. Önce CI'da canlı `ublk` worker-kill, sistem servisi ve kernel swap-off senaryoları başarıyla bitmeli; fiziksel Radeon altında SSD-shadow ve PSI baskı testleri ayrı kabul edilmeli. Blokajı kullanıcı test isteğine rağmen bilerek koruyoruz; aynı yedeksiz hata yolunu tekrar kullanmamak için. `status` ve `stop` korunmuştur. GitHub sorun kaydı: https://github.com/nopenstudio/nopen-vram-swap/issues/7

## RX 9060 XT için güvenli kurtarma kabul testi (yeni)

**Bu test gerçek AMD OpenCL kullanır ama kernel SWAP açmaz, GPU firmware'ini resetlemez, root/sudo istemez.** 64MiB veriyi bağımsız ext4/xfs NVMe dosyasında tutar, önce GPU yolundan yazıp okur, GPU işçi sürecini yalnızca yazılım düzeyinde sonlandırır ve bütün 16.384 sayfayı diskteki güvenilir kopyadan okur. Ölçülen gecikmeler SSD yazma maliyetini de içerir.

~~~bash
cd ~/nopen-v070
git pull --ff-only
cmake -S v070 -B build-v070 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v070 --parallel
ctest --test-dir build-v070 --output-on-failure
./build-v070/nopen-shadow-smoke \
  --worker "$PWD/build-v070/v060/nopen-isolated-worker" \
  --backend opencl --capacity-mib 64 \
  --operations 1000 --loss-after 500 \
  --shadow-file "$HOME/nopen-v070-hardware-shadow-64mib.bin" \
  | tee "$HOME/nopen-v070-hardware-shadow.log"
~~~

Başarı kaydı: `V070_SHADOW_HARDWARE_PASS all_pages_verified=YES read_after_worker_loss=YES write_after_worker_loss=YES`. Test başarılıysa test dosyasını siler; hata alırsa tanılama için bırakır. Dosya zaten mevcutsa `O_EXCL` ile reddedilir, mevcut dosyanın üzerine yazmaz.

**Not:** Eğer dosyanın tutulduğu bölüm `tmpfs` ise test reddedilir. Bu dosya gerçek NVMe/SSD üzerinde olmalıdır; RAM içindeki tmpfs ile test yapmak hatalı olur.

Bu testin sonucu gerçek Radeon I/O'nun ve güvenilir disk fallback'in birlikte çalıştığını gösterir. **Kernel direct reclaim altında güvenli system-wide SWAP veya Bannerlord FPS kazanımını tek başına kanıtlamaz.**

## 28 Ekim 2026 — Tam çekirdek SWAP-in + GPU işçisi ölümü regresyonu

Önceki v0.7 hatası gerçekte `ublkb23` cihazının kernel SWAP yazma zaman aşımıyla başlıyordu. Ayrı bağımsız sistem servisi ve disk yedeğiyle bunu test etmek için `v070/tools/test_shadow_swap_vm.sh` eklendi ve **geçici GitHub sanal makinesinde çalıştırıldı**.

- Gerçek Linux `/dev/ublkb27` aygıtı ve `swapon -p 200` etkinleştirildi; GPU backend açıkça **mock** seçildi ve veri için O_DIRECT SSD shadow kullanıldı.
- 64MiB cgroup RAM sınırında uygulama 96MiB veri yazdı; Linux `/proc/swaps` üzerinde Nopen cihazında **41.432 KiB** swap kullanımını raporladı (`V070_PAGING_FAILOVER_SWAPIN_READY`).
- Çalışma sırasında bağımsız GPU işçisi `SIGKILL` ile öldürüldü. Uygulama 96MiB/24.576 adet 4KiB bellek sayfasının tüm veri örüntüsünü **GPU işçisi öldükten sonra** yeniden doğruladı. DİKKAT: 24.576 sayfanın tümü swap'e taşınmadı; bu sayı uygulamanın kontrol ettiği toplam sayfalardır.
- SSD shadow üzerinden fallback doğrulandı; `swapoff` başarılı oldu ve kernel `ublk` aygıtı kaldırıldı. Başarı kayıtları: `V070_PAGING_FAILOVER_PASS` ve `V070_PAGING_FAILOVER_PASS_AFTER_READ`.
- Kanıt: https://github.com/nopenstudio/nopen-vram-swap/actions/runs/40751604294 — **38/38 CTest, 3/3 CI**.

**Sınır:** Gerçek kernel SWAP ve reclaim yolu çalıştırıldı, ancak GPU backend **mock** idi. Gerçek RX 9060 XT'de 64MiB OpenCL + SSD shadow işçi kaybı testi daha önce başarılıydı (16.384/16.384 sayfa). Bu iki başarı birlikte fiziksel GPU destekli kernel SWAP'in düşük bellek baskısında kilitlenmeyeceğini veya oyun FPS'sini artıracağını henüz kanıtlamaz. Fiziksel `start` engeli şu anda yürürlüktedir.

## RX 9060 XT için 64MiB fiziksel kernel-SWAP pilotu — yalnızca açık opt-in

**Mühendislik durumu:** Önceki 256MiB fiziksel OpenCL kernel swap denemesi kilitlendi (Issue #7). Artık gerçek Linux kernel SWAP'e yazılan sayfalar, GitHub geçici VM'de **41.432 KiB swap kullanılırken GPU işçisi SIGKILL edildiğinde** SSD yedeğinden yeniden okunabildi; 24.576 uygulama sayfasının tamamı doğrulandı. Ayrıca RX 9060 XT gerçek OpenCL + 64MiB SSD kurtarma deneyinden başarı alındı; ancak iki testin birlikte **fiziksel GPU altında çekirdek reclaim** güvenliğini kanıtlamadığını unutmayın.

**Bu nedenle genel `start` komutu HÂLÂ engelli.** Yalnızca 64MiB, ext4/xfs SSD yedeğiyle, iki aşamalı açık onay isteyen `start-pilot` geliştirildi. Pilot gerçek `swapon` yapar; ana bilgisayarı yine kilitleyebilir. 64MiB kapasite tek başına ölü kilidi önlemez.

~~~bash
cd ~/nopen-v070
git pull --ff-only
bash v070/tools/build_ublk.sh
ctest --test-dir build-v070 --output-on-failure
sudo env NOPEN_V070_PILOT_ACK=I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK \
  bash v070/tools/vram_swap_lab.sh start-pilot 64
bash v070/tools/vram_swap_lab.sh status
~~~

Başlatırken ekranda görünen risk uyarısına `EVET` yazmak gerekir. Sunucu KDE kullanıcı oturumundan bağımsız root `system.slice` servisinde çalışır; 64MiB GPU aygıtı + fiziksel SSD üzerinde baştan oluşturulmuş `O_DIRECT | O_DSYNC` shadow dosyası zorunludur. ZRAM devre dışı bırakılmaz. SSD shadow her yazıda çalıştığından bu saf VRAM SWAP'in performans testi değildir.

**Yalnızca pilotu başlatabildiyseniz**, gerçek SWAP sayfalarını harekete geçirmek için 96MiB cgroup RAM limiti altında 112MiB örüntülü bellek sınaması:

~~~bash
sudo env NOPEN_V070_PILOT_ACK=I_ACCEPT_64MIB_KERNEL_GPU_SWAP_RISK \
  bash v070/tools/pilot_pressure.sh \
  | tee "$HOME/nopen-v070-physical-pilot.log"
~~~

Beklenen kayıtlar: `V070_PILOT_PRESSURE_SNAPSHOT device_swap_used_kib=...` (pozitif) ve `V070_PILOT_PRESSURE_PASS_AFTER_READ verified_memory_pages=28672`. Bu 28.672 sayfanın hepsinin swap'e taşındığı anlamına gelmez. Kontrollü cgroup testi de GPU sürücüsünü veya kernel'i kilitleyebilir.

Deneme bitince, önce SWAP'i geri kapatın:

~~~bash
sudo bash v070/tools/vram_swap_lab.sh stop
~~~

`swapoff` başarısız olursa Nopen aygıtını silmeyin ve sunucuyu öldürmeyin; komut aygıtı kasıtlı olarak etkin tutar. Hata durumunda `/var/log/nopen-v070` günlükleri ve `journalctl -k` önce incelenmelidir. Bu pilotu geçen RX 9060 XT, yalnızca bir sonraki Bannerlord karşılaştırma adayımız olur; oyun FPS artışı, net RAM tasarrufu veya tam kernel kararlılığı otomatik olarak kanıtlanmış sayılmaz.
