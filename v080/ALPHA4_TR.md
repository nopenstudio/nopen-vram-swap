# Nopen VRAM SWAP v0.8.0-alpha.4 — 4 GiB GPU-only + Bannerlord birlikte çalışma

**Durum:** Donanım testi adayı, kullanıcı makinesinde henüz sınanmadı.
Bu sürümün amacı gerçek AMD Radeon RX 9060 XT üzerinde 4 GiB ayırmak,
tüm sayfaları iki ayrı geçişte doğrulamak, ardından ayrı bir deneyde
oyunla VRAM kapasitesi için yarışmanın etkisini ölçmektir.
**4 GiB kernel SWAP açılmaz. RAM genişlemesi veya FPS artışı kanıtlanmış değildir.**

## Ön koşul

Alpha.3 RX 9060 XT gerçek 2048 MiB çift tam-arena kontrolü başarılı:
`V080_ALPHA3_LOCAL_PASS real_gpu=YES capacity_mib=2048 full_passes=2 kernel_swap=NO`.
Bu kanıt 4 GiB'nin tek başına başarılı olacağını garanti etmez.
Kapalı blocker: **YOK**; açık blocker: [Issue #7](https://github.com/nopenstudio/nopen-vram-swap/issues/7)
gerçek kernel SWAP güvenliği için hâlâ geçerlidir.

## Aşama A: oyun KAPALI, 4 GiB GPU-only kapasite

Önce tüm çalışmaları kaydet; Bannerlord'u ve diğer ağır oyunları kapat.
Testler root/sudo olmadan yapılır; mevcut zram değişmez.

```fish
git clone --branch develop/v0.8.0-alpha.4 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a4
cd ~/nopen-v080a4
bash v080/tools/run_alpha4_gpu.sh
```

Önce planlayıcı, CTest ve açık GPU-onay güvenlik kapıları çalışır.
Sonra aynı 4 GiB GPU havuzunda iki kez tam arena yazımı,
ayrı tam arena okuması, 64-bit sayfa kimliği ve nesil doğrulaması yapılır.
Yazma/okuma hızı 4/64/1024/4096 KiB bloklarla raporlanır.
Başarı kanıtı:
`V080_ALPHA4_LOCAL_PASS real_gpu=YES capacity_mib=4096 full_passes=2 kernel_swap=NO`.
Bu aşamadan sonra GPU havuzu otomatik serbest bırakılır.

## Aşama B: Bannerlord ile GPU VRAM kaynak rekabeti

Bu **ayrı ve isteğe bağlı** deney, yalnızca Aşama A aynı Git commit
üzerinde başarıyla tamamlandıktan sonra kullanılabilir. Fiziksel GPU
sürücü kilitlenmesi veya oyunda ciddi FPS düşüşü olasılığı vardır.
Aşama A henüz doğrulanmadan doğrudan bu adıma geçme.

1. Önce Bannerlord açıkken, Nopen havuzu **yok** durumunda aynı save,
   grafik ayarları ve sahnede 120 saniyelik RAM/VRAM ölçümü al.
   `python3 v070/tools/find_bannerlord.py` PID'yi belirleyebilir.
   Read-only telemetri: `v060/tools/game_baseline.py`.
   FPS/frametime için MangoHud ile ayrıca CSV kaydet.
2. Oyunu tamamen kapat. Sonra bir terminalde
   `bash v080/tools/run_alpha4_game_hold.sh` çalıştır.
   Bu betik Aşama A'nın başarılı logunu ve Git revision'ını kontrol eder.
3. `V080_GAME_HOLD_READY` mesajından **sonra** Bannerlord'u aç.
   Aynı sahnede Nopen GPU-only havuzu etkinken ölçüm al.
   Havuz en fazla 300 saniye açık tutulur. Oyun mümkünse bitiş
   doğrulamasından önce kapatılır.
4. Test `V080_GAME_HOLD_PASS` ile bitebilir. RAM veya VRAM boşluğu
   kritik düzeye düşerse `V080_GAME_HOLD_ABORT` vererek GPU havuzunu
   bırakır; bu başarısız / iptal edilmiş bir deneydir.
5. Tekrar Nopen **kapalı** durumunda A ölçümü alınabilir.
   Sıralama A/B/A, tek bir başlangıç yükleme farkından kaynaklanan
   hatalı yorumları azaltır.

**Önemli:** Oyun testi sırasında Nopen GPU belleğini *meşgul* eder;
Bannerlord sayfalarını GPU'ya taşımaz. Linux SWAP gerçekten etkin
olmadığından kullanıcının 8 GiB RAM'i 12 GiB'ye çıkmaz.
Bu test VRAM kapasitesi, GTT taşması, oyun frametime ve
sürücü kararlılığı üzerindeki olumsuz etkileri araştırır.
FPS artışının bulunmaması normaldir ve projeyi tek başına çürütmez.

### Salt okunur telemetri örneği (ikinci terminal)

```fish
cd ~/nopen-v080a4
python3 v070/tools/find_bannerlord.py
set pid (python3 v070/tools/find_bannerlord.py --pid-only)
python3 v060/tools/game_baseline.py --pid $pid --duration-seconds 120 \
  --csv "$HOME/nopen-bannerlord-alpha4-on.csv"
```

Nopen kapalı A ölçümü için farklı bir CSV dosya adı kullan:
`nopen-bannerlord-alpha4-off.csv`. Üzerine yazma bilinçli olarak reddedilir.

## Kırmızı çizgiler

- Normal kullanıcı, sadece ayrık AMD OpenCL, CUDA/CPU fallback yok.
- 4 GiB oyun testinde minimum 1 GiB MemAvailable ve
  GPU VRAM'de başlatma sırasında 4 GiB boşluk korunur.
- Oyun sırasında VRAM serbest alanı 2 GiB altına düşerse deney bırakılır.
- Kernel ublk/NBD swap etkinse testler reddedilir; zram korunur.
- `sudo swapon`, `start-pilot 4096`, `NBD` veya `ublk` **YOK**.
- Failover/SSD write-through bu deneyin parçası değildir.
  Kaynak kodun gerçek GPU kapasitesi üzerinde çalışması OS reclaim
  ilerleme garantisi sağlamaz.
