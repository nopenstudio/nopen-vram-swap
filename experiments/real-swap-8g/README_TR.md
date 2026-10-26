# Nopen Real SWAP 8 GiB — YÜKSEK RİSKLİ ARAŞTIRMA PROFİLİ

**Amaç:** v0.9.3'ün orijinal ve başarılı 2 GiB canary çalışmasını bozmadan, RX 9060 XT'nin 8 GiB VRAM'ini `/dev/nbd15` için gerçek Linux takas kapasitesi olarak test etmek. Bu profil üretim kullanımı için güvenli değildir; geçmişte Bannerlord sırasında NBD time-out ve sistem genelinde `SIGBUS` oluştu. Bu hata çözülmedi.

## Disk ile farkı

`swapon --show` yalnızca `/dev/zram0` + `/dev/nbd15` gösteriyorsa Nopen **SSD'ye takas yapmıyor**. NBD blok aygıtıdır ama arkasındaki sunucu `OpenCL` ile GPU VRAM'i kullanıyor. `8 GiB VRAM-SWAP`, ek 8 GiB fiziksel RAM değildir. Bu düzenekte 8 GiB VRAM'in oyuna ayrılamaması yeni bir GPU bellek darboğazına da yol açabilir.

Bu sürüm **8 GiB kapasite** oluşturur. İlk gerçek sayfa testi bilinçli olarak **512 MiB** ile sınırlıdır; 8 GiB'nin tamamını RAM baskısı altında doldurmak, 8 GiB tek kanallı fiziksel RAM'i olan sistemde `swapoff` başarısızlığı veya masaüstü kilitlenmesine yol açabilir. Bannerlord gerçek kullanımın nasıl etkilendiğini gösterecektir.

## Kurulum

```bash
cd ~/nopen-bannerlord-debug
git fetch origin experiments/8g-real-swap
git switch --track origin/experiments/8g-real-swap
SCRIPT=experiments/real-swap-8g/real-swap-8g.sh
bash "$SCRIPT" prepare
bash "$SCRIPT" status
```

`prepare`: arşivdeki orijinal v0.9.3'ü **ayrı klasöre kopyalar**, 8 GiB boyut kontrollerini birbirleriyle uyumlu şekilde günceller, `make test` çalıştırır. Hiçbir swap açmaz. Etkin önceki NBD aygıtı veya beklenmedik swap aygıtı varsa hazırlık reddedilir.

## Deneme sırası

Açık dosyalarını kaydet, Steam/Bannerlord'u kapat. Nopen 8 GiB ayıracak; bu GPU'nun 16 GiB VRAM'inin yarısıdır. Disk takası açık olmadığını `swapon --show` ile kontrol et. Ardından:

```bash
bash "$SCRIPT" start
swapon --show
bash "$SCRIPT" test
```

`start` önceki v0.9.3'ün risk onayını yine ister. Beklenen `/dev/nbd15` **8 GiB, öncelik 200** ve `/dev/zram0` yaklaşık **7.3 GiB, öncelik 100**. `test` yalnızca 512 MiB anonim bellek için gerçek SWAP-IN/OUT uygular. `TEST_EXIT=0` / `TEST_PASS` görülmeden oyuna geçme.

Ardından bağımsız terminalde:

```bash
bash "$SCRIPT" watch
```

10 dakikalık gözlem açıkken Bannerlord'u aç, kayıtlı dünyaya gir ve birkaç dakika oyna. Gözlem `metrics.csv` benzeri zaman serisi üretir; hiçbir aktif SWAP ayarını değiştirmez. **Sunucu koparsa oyunu kapatmak bile başarısız olabilir; servis veya `nbd` modülünü zorla kapatma.**

Oyun kapatıldıktan sonra, bellek uygunsa:

```bash
bash "$SCRIPT" stop
bash "$SCRIPT" status
bash "$SCRIPT" report
```

`stop`: önce `sudo swapoff /dev/nbd15` yapar, başarıyla ayrıldıktan sonra sunucuyu kapatır. `swapoff` hata verir veya takılırsa NBD sunucusunu öldürme, `modprobe -r nbd` çalıştırma. `report` çıktısı `~/nopen-vram-experiment/nbd-8g-experiment/` altında oluşur; bu arşivi sohbete yükle.

## Teknik sınırlar

- 32×256 MiB OpenCL buffer. RAM/VRAM tahsisi sürücünün bildirilen fiziksel yerleşimine ayrıca bağlıdır.
- NBD kapasitesi 8192 MiB; Linux çekirdeğinin `/dev/nbd15` üzerindeki `/sys/block/nbd15/size` değeri 16777216 (512-byte sektör) olmalıdır.
- Backend'in mevcut `>=4GiB` VRAM boşluk payı koruması ve GPU toplam bellek %75 sınırı muhafaza edilir; **8GiB ayrılmış olması oyuna kalan 8GiB'nin her koşulda yeterli olduğunu garanti etmez.**
- `MemorySwapMax=0` ve `mlock` kontrolleri backend için korunmuştur.
- 60 saniyelik NBD time-out aynen korunur; yükselterek sorunu gizlemiyoruz.
- Ayrı profil eski servis birim adını (`nopen-vram-v093.service`) kullanır, bu yüzden eski sürümün çalışıyor olması halinde başlatma reddedilir. Ayrı `~/nopen-vram-experiment/nbd-8g-experiment/backend-8g` dizinini kullanır.
- Başarılı 2GiB canary testleri 8GiB adres aralığının tümünü veya Bannerlord'daki kararlılığı kanıtlamaz.
- Bu profilin **kaynak derleme ve davranış testleri CI'da** yapılabilir; gerçek AMD GPU/NBD/SWAP yükü yalnızca kullanıcı donanımında ölçülebilir.