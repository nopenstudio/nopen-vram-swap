# Nopen Memory Fabric — 0.12.0-alpha.1 mimarisi

## 1. Önceki kernel panic'ten öğrendiklerimiz

2026-10-26'da RX 9060 XT üzerinde 8192 MiB OpenCL veri bütünlüğü doğrulandı. Buna rağmen OpenCL+AF_UNIX+NBD sunucuya bağlanan `/dev/nbd15` aygıtı SWAP etkin iken Bannerlord/pencere değişimlerinde yanıt vermeyi bıraktı. Çekirdek `Read-error on swap-device`, NBD dead connection, süreçlerin SIGBUS, en sonunda PID 1'in `Attempted to kill init, exitcode=0x8b` panik mesajlarını üretti. Sunucu tarafında `phase=162` NBD okuma yanıt yükünün gönderilmesi aşamasını gösteriyordu. Bunlar sistem SWAP'i olarak güvenli ileriye ilerlemenin **kanıtlanmadığını** gösterir; sorunu sırf GPU/OpenCL bozuk diye tanımlamayız.

## 2. Yeni bellek yolu (bu sürüm çalışır, ancak yalnızca kooperatif demo)

```text
            SADECE UYGULAMA KAPSAMI

 Uygulama test iş parçacığı           UFFD sayfa-olayı iş parçacığı
         |                                      ^
   mmap'de 4KiB erişim                        |
         |               UFFD event ------------+
         |                                      |
   (sayfa yoksa bekler)                  OpenCL clEnqueueReadBuffer
         |                                      ^
         |                               GPU VRAM buffer (4KiB)
         |                                      ^
  sayfa RAM'e gelir <-- UFFDIO_COPY           |
         |                                      |
   pattern doğrulama                  Kooperatif evict:
         |                           clEnqueueWriteBuffer + MADV_DONTNEED
         +------------------------------->      |

 /proc/swaps: DEĞİŞMEZ
 /dev/nbd15: HİÇ AÇILMAZ
 PID 1/systemd: FARKLI BİR SÜREÇ
```

Bir sayfa çıkarılmadan önce GPU'ya yazım başarılı olmak zorundadır. Okuma hatasında RAM'e uydurma/sıfır veri yazılmaz; yalnızca test süreci sonlandırılır. Başarı = tüm örneklenen 4KiB sayfalar farklı round'lardaki verilerle `memcmp` doğrulanır. `mock` yalnızca açık test modu, gerçek backend yalnızca AMD GPU OpenCL.

## 3. Bu sürümün yapmadıkları

1. Rastgele başka bir süreç (Bannerlord) belleğini yönetmez. İleride uygulamanın açık tahsis API'si veya bilinçli entegrasyonu gerekir.
2. Thread-safe kirli sayfa takibi yapmaz, şeffaf proses geneli `malloc()` yerini almaz. **Evict işlemi yalnızca tüm diğer erişimler durmuşken çağrılmalıdır.**
3. GPU sıfırlanırken VRAM'deki tek kopyanın garantili korunacağını iddia etmez; uygulama seviyesinde hata izole edilir, sistemin kullanılabilirliği korunur.
4. CPU RAM'in donanım gecikmesinde VRAM'e erişim sağladığını iddia etmez. PCIe DMA transferi ve sayfa hatası/yürütücü gecikmesi vardır.
5. Swap kapasitesini yükseltmez. Sistemin bellek miktarı ve `/proc/swaps` değişmez.

## 4. NopenOS çekirdek entegrasyonu: gelecek tasarım, henüz yok

Doğrudan bir `/dev/nbd` üzerinden kullanıcı alanı servisinden geri sayfa istemek kernel reclaim/swap yolunu servis ömrüne ve socket ilerlemesine bağımlı yapar. O yol bu proje tarihinde başarısız oldu.

Gelecekte gerçek sistem SWAP'i hedeflenecekse, ayrı sürümlerde aşağıdaki engellerin **uygulamayla kanıtlanması** gerekir:

- `nopenmem` iç çekirdek arayüzü, amdgpu/DRM bellek tahsis ve taşıma hattına temiz entegrasyon. Reclaim yolunda servis bekleme yok.
- Önceden ayrılmış, reclaim sırasında yeni bellek talep etmeyen DMA tanımlayıcıları, staging buffer'lar ve I/O iş kuyruğu. Tahsis ve fence bağımlılıklarının ileriye ilerleme analizi.
- GPU reset/hang sırasında sayfalar kaybolmayacak mekanizma. RAM'de her zaman kopya tutmak verimli ek RAM sağlamaz; dayanıklı NVMe/başka güvenli katman ise ayrıca tasarlanmalıdır.
- GPU ayırma/oyun ihtiyaçlarına göre dinamik VRAM bütçesi, geriye dönüş, zaman aşımı politikası, yankılanabilir arızaların ayrıştırılması.
- Önce QEMU sanal makinede kernel modülü/probe, sonra gerçek makinede yalnızca sistem dışı blok I/O; sistem SWAP en son aşama.

**Özellikle:** ``tam performans`` için API/driver yolu ölçülerek optimize edilebilir fakat PCIe transfer bant genişliği ve ~4KiB CPU/GPU işlem senkronizasyonları RAM seviyesinde gecikme sunmayabilir. RX 9060 XT için 8GiB önceki test sıralı PCIe ~11.8/13.2 GiB/s, 4KiB sayfalar ~22us p50 idi; DDR5 RAM ile aynı şey değil. Bu ölçümler yeni UFFD yolu için ölçüm değildir.

## 5. İlerleme kapıları

| Aşama | Çıkış koşulu | v0.12.0-alpha.1 |
|---|---|---|
| Mock store | full page equality, fault injection, bounds | uygulandı ve test edildi |
| Real GPU store | OpenCL AMD fail-closed, 4KiB paged read/write | uygulandı, gerçek donanımda test bekliyor |
| UFFD pager | register, missing, evict, UFFDIO_COPY | uygulandı, Garuda testi bekliyor |
| Hata güvenliği | GPU okuma hatasında sadece uygulama hata verir | kod yolu var; gerçek fault injection entegrasyon testi sonraki sürüm |
| NopenOS kernel module | DRM/DMA reclaim guarantees | tasarım, **uygulama yok** |
| Bannerlord/system swap | hiç kernel panic üretmeyen şeffaf RAM kazanımı | **uygulama yok, yasak** |

Kaynak: https://docs.kernel.org/admin-guide/mm/userfaultfd.html
NBD swap riskleri: https://man.archlinux.org/man/nbd-client.8.en