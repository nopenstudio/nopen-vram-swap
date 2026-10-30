# Nopen VRAM SWAP v0.8.0-alpha.3 — 2 GiB gerçek AMD GPU-only test adayı

**Donanım kabulü henüz yapılmadı.** Bu dal, alpha.2'nin 1 GiB gerçek donanım
PASS raporu üzerine hazırlanmıştır. Yerel RX 9060 XT çalıştırılmadan 2 GiB
başarılı ilan edilemez. ZRAM korunur. Kernel SWAP etkinleştirilmez.

## Neden yeni test gerekli?

Alpha.2'nin orijinal veri örüntüsü yalnızca 8-bit değerlerle sınırlı
olduğu için farklı 4 KiB sayfalarda aynı içerik oluşabiliyordu.
Bu, yanlış GPU adreslemelerinin bazılarını tesadüfen saklayabilir.
Alpha.3 her 4 KiB sayfaya benzersiz 64-bit **mutlak sayfa kimliği** ve
64-bit **geçiş sürümü** yazar. Karşılaştırma tam 4 KiB sayfanın tamamını kapsar.
Önce 2 GiB'nin tamamı yazılır, sonra tamamı okunup karşılaştırılır.
Bu ardışık süreç farklı içerikle iki kere tekrarlanır.
Host staging 8 MiB ile sınırlıdır. Yeni bir SWAP cihazı açılmaz.

## Kaynaktan gelen güvenlik kapıları

- Normal kullanıcı, root/sudo yok.
- Etkin ublk veya NBD kernel SWAP varsa deneme reddedilir.
- Gerçek AMD ayrık OpenCL GPU zorunlu, RAM-only fallback yok.
- VRAM headroom en az 4 GiB, MemAvailable en az 1 GiB.
- Sadece 1024 veya 2048 MiB gerçek GPU benchmark; 4096 MiB **engelli**.
- 2048 MiB için özel onay belirteci ve en az iki tam-arena turu zorunlu.
- Oyun kapalıyken test; FPS, gerçek SWAP ve Bannerlord kazancı kanıtı değil.
- CI mock testleri gerçek GPU fiziksel kapasite kanıtı sayılmaz.

## Garuda / fish terminal

Bannerlord, Proton oyun oturumlarını kapatıp açık işlerini kaydet.

```fish
git clone --branch develop/v0.8.0-alpha.3 --single-branch \
  https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a3
cd ~/nopen-v080a3
bash v080/tools/run_alpha3_gpu.sh
```

**Kesinlikle sudo kullanma.** Script 2 GiB bütçesini denetler, derler,
CTest çalıştırır, ardından iki tam 2 GiB etiketli veri doğrulaması ve
4/64/1024/4096 KiB blok hız/gecikme deneyini direct modunda yapar.
Her çalıştırmada özgün rapor dizini açar. Başarısızsa raporu sakla.
Pinned yolu alpha.2'de genel fayda göstermediği için bu testte kullanılmaz.

Beklenen kanıtlar:
- `V080_GPU_FULL_ARENA_VERIFIED ... capacity_mib=2048 ... verified_pass=1`
- `V080_GPU_FULL_ARENA_VERIFIED ... capacity_mib=2048 ... verified_pass=2`
- `V080_GPU_BENCH_PASS ... capacity_mib=2048 ... full_passes_completed=2`
- `V080_ALPHA3_LOCAL_PASS ... kernel_swap=NO`

## Sonraki aşama

2 GiB fiziksel test PASS ve log gözden geçirme sonrasında alpha.4 için
4 GiB GPU-only ve oyunla VRAM rekabeti A/B testi hazırlanabilir.
**4 GiB Linux kernel SWAP etkinleştirmek kesinlikle yasak:**
https://github.com/nopenstudio/nopen-vram-swap/issues/7

Ölçüm, swap sayfası başına SSD write-through veya Linux reclaim
güvenliğini henüz kanıtlamaz. 4 GiB çalışma bu belirsizliği değiştirmez.
