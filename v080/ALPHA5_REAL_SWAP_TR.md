# Nopen VRAM SWAP v0.8.0-alpha.5 | Gerçek 4 GiB Linux SWAP deneyi

**Amaç**: RX 9060 XT üzerindeki gerçek AMD OpenCL VRAM havuzunu
Linux kernel ublk cihazı `/dev/ublkb23` üzerinden **4096 MiB gerçek SWAP**
olarak `swapon` ile tanıtmak ve `free -h` çıktısında göstermek.
Bu, alpha.4 GPU-only kapasite testinden farklıdır.

## Durum

- Alpha.3 gerçek GPU 2GiB, alpha.4 gerçek GPU 4GiB çift tam-arena doğrulaması PASS.
- Daha önce RX 9060 XT üzerinde gerçek 64MiB kernel SWAP pilotu
  32936KiB kullanım, tüm test sayfalarının doğrulanması ve güvenli
  swapoff ile PASS. **Bunu yeniden yapmak gerekmiyor.**
- Daha önce 256MiB fiziksel swap denemesinde WRITE timeout ardından
  büyük I/O arızası ve ana oturum kaybı görüldü. [Issue #7](https://github.com/nopenstudio/nopen-vram-swap/issues/7)
  **hâlâ açık**. 64MiB başarısı 4096MiB reclaim güvenliğini kanıtlamaz.
- Alpha.5 yalnızca var olan **gerçek ublk + SSD write-through shadow**
  yolunu 4GiB kapasiteyle, ayrı güçlü onayla erişilebilir kılar.
  SSD shadow bütün onaylanmış yazmaların kalıcı otoritesidir; bu yüzden
  saf VRAM-SWAP bant genişliğini temsil etmez.
- **Bu sürüm için fiziksel 4GiB SWAP güvenlik kabulü yapılmamıştır.**
  Günlük iş yapılan makinede ciddi kilitlenme, veri kaybı ve elle
  yeniden başlatma riski vardır. Önce yedek alın; test için ayrılmış
  Linux kurulumunu tercih edin.

## A. Derleme + kullanıcı makinesi hazırlık

Oyun ve ağır GPU işleri kapalı olmalı, mevcut zram korunmalı.

```fish
git clone --branch develop/v0.8.0-alpha.5 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a5
cd ~/nopen-v080a5
bash v070/tools/build_ublk.sh
ctest --test-dir build-v070 --output-on-failure
python3 v080/tools/capacity_plan.py --stage-mib 4096
swapon --show
```

`build_ublk.sh` sadece derler, SWAP açmaz.
`capacity_plan.py` yalnızca kaynak bütçesini kontrol eder; kernel SWAP
güvenliğinin kanıtı değildir.

## B. Yüksek riskli gerçek SWAP etkinleştirme (KULLANICI ONAYI GEREKİR)

Bu adım **`/dev/ublkb23` üzerinde gerçek Linux kernel SWAP açar**.
Tüm önemli işleri kaydetmeden veya riskleri kabul etmeden çalıştırmayın.

```fish
cd ~/nopen-v080a5
sudo env NOPEN_V080_4G_ACK=I_ACCEPT_4GIB_REAL_KERNEL_SWAP_CAN_LOCKUP \
  bash v070/tools/vram_swap_lab.sh start-4g 4096
```

Program ikinci kez `EVET` onayı ister. Zram kapatılmaz; Nopen
önceliği 200, zram önceliği 100 olduğundan Linux yeni swap-out'ları
**öncelikle Nopen'a yazabilir.** Aktif GPU kullanıcı alanı servisinde
veya SSD depolamada zaman aşımı tüm sistemi kilitleyebilir.

**Başarılı açılış kanıtı**:
`V070_SWAP_ACTIVE mode=start-4g device=/dev/ublkb23 pool_mib=4096`.

```fish
free -h
swapon --show
cat /proc/swaps
```

`free -h` SWAP toplamı 4GiB kadar artmalı; `swapon --show` içinde
hem `/dev/zram0` hem `/dev/ublkb23` bulunmalı. Bu yalnızca
**çekirdek SWAP cihazının sisteme tanıtılması** kanıtıdır. Nopen'ın
`USED > 0` göstermesi için çekirdeğin gerçekten bu cihaza yazması
gerekir; sistemi kontrolsüz RAM baskısına sokmayın.

## C. Kapatma

```fish
cd ~/nopen-v080a5
sudo bash v070/tools/vram_swap_lab.sh stop
swapon --show
```

`V070_STOP_PASS` beklenir. `swapoff` başarısızsa servis ve cihazı
zorla öldürmeyin: aktif swap sayfaları kaybolabilir. Sistem donmuşsa
kapatmanın garanti çalışacağı varsayılamaz.

## Açık kapılar

1. Fiziksel GPU + Linux 4096MiB `swapon` açılış ve kapatma doğrulaması.
2. Gerçek SWAP `USED>0`, sayfa okuma/yazma, sistemde I/O hatası yok.
3. İşçi ölümü veya 2s timeout sırasında SSD shadow ile güvenilir ilerleme.
4. Tekrarlanabilir uzun süreli Bannerlord testi ve bellek baskısı.

4GiB havuzun GPU-only PASS olması, 4GiB gerçek Linux SWAP'ın
güvenli ve hızlı olduğunu kanıtlamaz. Kök neden çözülmeden sürüm
kararlı olarak yayımlanmamalı.
