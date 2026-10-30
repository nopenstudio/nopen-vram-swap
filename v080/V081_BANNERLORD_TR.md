# Nopen VRAM SWAP v0.8.1 | Bannerlord GPU birlikte çalışma deneyleri

## Sürüm amacı

Gerçek Linux SWAP güvenliği henüz kanıtlanmadan Nopen'ın **GPU-only
VRAM Direct** veri yolunun Bannerlord açıkken çalışmasını gözlemek.
**Bu sürüm oyun testlerini başlatır, gerçek kernel SWAP'ı açmaz.**

Sürüm temel alınan dal: `develop/v0.8.0-alpha.10`.

## Kodda yapılanlar

- `nopen-v080-vram-direct-bench --spread-across-vram` artık
  1024 test bloğunu **4096 MiB adres aralığının başından sonuna**
  dağıtır. Sadece ilk 4 MiB veya 64 MiB test edilmez.
- 256 MiB OpenCL segmentlerinin son ve ilk blokları ile VRAM'in
  en son bloğunda **yazma + geri okuma + byte-level bütünlük**
  kontrolü uygulanır. `segment_edge_checks` raporlanır.
- Bu, 4 GiB VRAM'in her baytının yazılıp okunduğu anlamına
  gelmez: tüm kapasiteye dağıtılmış örnekleme ve sınır kontrolüdür.
- `bannerlord_v081_probe.py` yalnızca gerçek Bannerlord
  sürecini bulur. Steam / Proton başlatıcısını oyun diye saymaz.
- GPU VRAM bütçesi, en az 4 GiB VRAM baş boşluğu ve 1536 MiB
  kullanılabilir sistem RAM'i korunur. Oyun giderse deney FAIL.
- GPU kopya 4 KiB ve 64 KiB p50/p95 gecikmeleri, oyun
  süreci RSS'i ve GPU VRAM kullanımı JSON rapora yazılır.
- SSD shadow, NVMe write-through, kernel SWAP aygıtı ve
  `/dev/zram0` değişmez. Derleme dosyaları ve deney raporlarının
  diske yazılması normaldir; SWAP veri sayfaları SSD'ye gitmez.
- FPS ölçülmüş gibi davranılmaz. İstenirse MangoHud CSV
  ile bağımsız oyun frametime/FPS karşılaştırması yapılmalıdır.

## Deney protokolü

**A. Bannerlord kapalı, aynı testin referansı**

```fish
git clone --branch develop/v0.8.1 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v081
cd ~/nopen-v081
bash v080/tools/run_v081_bannerlord.sh baseline 256
```

**B. Bannerlord'u aç, harita veya savaş sahnesinde çalışır durumda tut**

```fish
cd ~/nopen-v081
bash v080/tools/run_v081_bannerlord.sh game 256
```

A ve B'nin 4KiB/64KiB gecikmeleri aynı örnekleme düzeniyle
kıyaslanabilir. Sistemden `swapon` veya `swapoff` istenmez.

**C. İsteğe bağlı 4 GiB oyun eşliği**

256 MiB birlikte çalışma ve oyunun FPS/frametime değerleri
uygunsa, VRAM baş boşluğu güvenlik kontrolü ayrıca izin
verirse:

```fish
bash v080/tools/run_v081_bannerlord.sh game 4096
```

4 GiB rezervasyon oyunla GPU kaynak rekabeti yaratabilir;
kaydedilmemiş oyun ilerlemesi riske atılmamalı. Oyun GPU bellek
kullanımı kapıyı geçemiyorsa komut kendiliğinden reddedilir.

## Çıktılar ve karar ölçütü

Rapor klasörü `~/nopen-v081-bannerlord-YYYYMMDD-HHMMSS/`:

- `report.json`: deneme modu, gerçek Bannerlord PID'si,
  GPU bütçesi, her 4KiB/64KiB testinin gecikmesi, oyun RSS'i.
- `block-4k-trial-*.log`, `block-64k-trial-*.log`:
  gerçek OpenCL geri okuma / segment sınırı bütünlük kayıtları.
- Başarı şartı: her test `NOPEN_VRAM_DIRECT_PASS` döndürür,
  `spread_across_vram=YES` ve segment kenar testleri geçer,
  Bannerlord PID aynı kalır, GPU bütçe kontrolü geçer.
- Bunlar **gerçek Linux VRAM SWAP kararlılığının kanıtı değildir**.
  Gerçek `/dev/ublkb23` için ayrı hata izleme, ölüm durumunda
  ileri ilerleme (forward progress) ve kurtarma gereklidir.

## Sınırlar

4 GiB gerçek GPU SWAP veri yolunu, kullanıcı uygulamalarının
ölçülen 4KiB/64KiB taleplerine bağlıyoruz. Güncel sürümde
OpenCL işçisi Unix socket IPC ile çalışıyor ve her istek GPU
cevabını bekliyor; 40+ GiB/s VRAM bant genişliğini doğrudan
beklemiyoruz. Önce gerçek GPU/oyun rekabetini sayılarla ölçelim.
