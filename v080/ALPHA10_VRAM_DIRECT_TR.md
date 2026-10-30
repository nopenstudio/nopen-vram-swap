# Nopen VRAM SWAP: Alpha.10 VRAM Direct (GPU-only data path)

Bu değişiklik yalnızca **Nopen VRAM SWAP kaynak koduyla ilgilidir**.
Bilgisayardaki NVMe SSD'yi, disk bölümlerini, dosya sistemini,
mevcut Linux swap aygıtlarını veya zram'ı kaldırmaz/değiştirmez.

## Alpha.9 donanım kanıtı

RX 9060 XT, 256MiB:
- SSD-only: write_p50=1013.85us, ssd_write_avg=1036.78us.
- GPU-cache + SSD: write_p50=1033.62us,
  ssd_write_avg=1053.45us, gpu_upload_avg=149.60us,
  1999/2000 GPU cache upload.
Yorum: Nopen'ın zorunlu O_DIRECT|O_DSYNC SSD write-through
adımı 4KiB page ACK gecikmesini baskılıyor. GPU-only
tasarımının ne kadar hızlı olduğu ancak doğrudan ölçülerek görülür.

## Yeni VRAM Direct yolu

1. GPU işçisi OpenCL ile hedef 4096 MiB VRAM ayırır.
2. 4096B veya 65536B WRITE -> kontrol toplamı -> Unix socket IPC
   -> GPU VRAM -> **GPU WRITE tamamlandıktan sonra ACK**.
3. READ -> GPU VRAM -> kontrol toplamı -> Linux'a veri.
4. Hiç yazılmamış sayfalar işçinin RAM'deki metadata tablosu
   sayesinde sıfır okunur. Sıkıştırma veya SSD shadow yok.
5. VRAM/worker kaybında EIO; hiçbir SSD fallback yok.
   Reboot sonrası SWAP sayfaları korunmaz.
6. Kernel SWAP'e bağlamadan önce gerçek Radeon üstünde 4/64KiB
   bütünlük + p50/p95 gecikme testleri uygulanabilir.

`v070/experimental/ublk/nopen_ublk_swap_target.cpp`:
- `NOPEN_UBLK_STORAGE_MODE=vram-direct` açık seçeneği eklendi.
- Direct modda `NOPEN_UBLK_SHADOW_FILE` varsa güvenli biçimde reddedilir.
- Fiziksel hostta GPU-only SWAP için var olan risk izni
  **yanında ayrı** `NOPEN_VRAM_DIRECT_ACK` gerekir.
- Eski `start-4g` komutu **değişmedi, SSD shadow kullanmaya
  devam eder**. Yani yeni kod, kullanıcı istemeden mevcut
  çalışma biçimini değiştirmez.
- Gerçek GPU-only kernel SWAP'ın OOM/AMDGPU forward progress
  güvenliği doğrulanmış değildir. Bu sürümde canlı kernel SWAP'ı
  test için etkinleştirmeyin.

## Önerilen gerçek GPU testi (sadece Nopen kodu, kernel SWAP yok)

```fish
git clone --branch develop/v0.8.0-alpha.10 --single-branch \
 https://github.com/nopenstudio/nopen-vram-swap.git ~/nopen-v080a10
cd ~/nopen-v080a10
bash v080/tools/test_alpha10_vram_direct.sh 4096
```

Bu komut CMake ile uygulamayı derler; derleme dosyalarının
normal disk kullanımı ile SWAP sayfalarının SSD'ye
aktarılması farklı şeylerdir. **GPU testinin veri yolu SSD
shadow dosyası yaratmaz, okuma/yazma yapmaz.** Linux'un
`/dev/zram0` aygıtına veya `/dev/ublkb*` aygıtına dokunmaz.

4KiB ve 64KiB testleri, efektif transfer MiB/s ve
write/read p50/p95 gösterir. 4096MiB OpenCL VRAM tahsisi
GPU üzerindeki başka uygulamalarla kaynak rekabeti
yaratabilir.

## Sınırlar
PCIe/IPC, OpenCL API çağrıları ve her 4KiB isteğin
kendi başına senkron tamamlanması büyük blok testlerinde
görülen 40+GiB/s'ye ulaşılacağını garanti etmez. İlk hedef
SSD yazma gecikmesini gerçek GPU SWAP veri yolundan
çıkarıp gerçek 4KiB/64KiB GPU gecikmesini ölçmektir.
