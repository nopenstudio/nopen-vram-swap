# Nopen VRAM SWAP v0.6.0-dev — Kod kabul raporu

**Karar: v0.6 deneysel prototip geliştirme hedefi kod/test düzeyinde tamamlandı.** Gerçek GPU reseti ve host RAM reclaim güvenliğine ilişkin donanım kanıtı YOK; bunlar v1.0 ürün güvenliği için ayrı zorunlu şartlardır. Bu rapor Nopen VRAM SWAP'i çalışan sistem-geneli swap olarak sunmaz.

## Tamamlanan ve çalıştırılan bileşenler

| Özellik | Teknik doğrulama |
|---|---|
| GPU I/O hatası | `GPU_ACTIVE → FAILED_CLOSED`, bozuk sayfa dönüşü yok; mock fault injection |
| CPU aynalı kayıp kurtarma | `GPU_ACTIVE → CPU_FALLBACK`, her sayfa hash/checksum ile doğrulanıyor |
| GPU'yu yeniden oluşturma | 1.024 sayfa yeni GPU backend'ine yazılıp geri okunuyor, önce tam doğrulama sonra `GPU_ACTIVE`; kısmi hata CPU fallback'de kalıyor |
| RAM / OOM | CPU mirror bütçesini aşan tahsis GPU açılmadan reddediliyor |
| GPU worker'ın kilitlenmesi | Bağımsız exec-spawned worker; 50–5000ms I/O timeout, öldürme girişimi, ardından kalıcı fail-closed |
| IPC veri bütünlüğü | 4–64KiB bloklarda iki yönlü checksum; kopyalama yalnızca tam yanıt sonrası |
| Linux `ublk` blok arayüzü | Upstream pinned `libublksrv` ile derleme ve link başarıyla |
| **Gerçek kernel ublk yaşam döngüsü** | Geçici GitHub VM'de gerçek `/dev/ublkb*` aygıt oluşturma, O_DIRECT 4KiB yaz/oku, `cmp`, aygıt silme **başarıyla çalıştırıldı** |
| İki ayrı ublk veri akışı | CPU mirror ve exec-isolated worker mock backend ile kernel I/O'da doğrulandı |

## Kanıt (GitHub-hosted geçici VM)

- `ublk` çekirdek sürücüsü yüklenip kullanıldı: `UBLK_KERNEL_PRESENT=YES`.
- `UBLK_LIFECYCLE_PASS created=YES direct_4k_io=YES bytewise_verified=YES deleted=YES mirror_and_isolated_worker=YES mock_only=YES no_global_swap=YES`.
- Model / regresyon setinde **33/33 CTest** geçti (v0.6 kurtarma, hasarlı mirror, kısmi GPU rebuild, 4-thread concurrency, 300 tur 16KiB IPC, timeout, failure isolation, v0.5/v0.4/v0.3 ve Alpha5 devamlılığı).
- ASan/UBSan, OpenCL kod derlemesi, freestanding C11 bellek politikası ve `libublksrv` tam C++ link kontrolleri CI'da mevcut.
- GitHub Actions kanıt run: https://github.com/nopenstudio/nopen-vram-swap/actions/runs/40739867688 (kernel iki mod), https://github.com/nopenstudio/nopen-vram-swap/actions/runs/40740070621 (33 test ve kernel iki mod).

## Kritik olarak henüz kanıtlanmayanlar

1. **RX 9060 XT donanımında gerçek GPU reseti** veya `amdgpu` sürücü kilitlenmesi sonrası GPU context yeniden açılabilirliği. Gerçek GPU reseti otomatik tetiklenmedi. Kod simülasyon, API hata ve watchdog testleriyle doğrulandı.
2. Gerçek Linux kernel swap (`swapon`) ve kernel reclaim sırasında CPU yedeğin reclaim tarafından tahliye edilmeden, GPU sürücüsünün bağımsız ilerlemesi. Gerçek ublk block I/O testi **swap testi değildir**.
3. GPU worker D-state / kernel driver deadlock senaryosunda `SIGKILL` veya timeout bir çözüm değildir. Üst blok callback zaman aşımı uygulasa bile sistem genelini kurtarmayı garanti edemez.
4. CPU mirror koruması **RAM tasarrufunu ortadan kaldırır**; kernel daemon restart sonrası RAM mirror kaybolabilir. No-mirror hata durumunda doğru fail-closed, ancak kayıp RAM sayfasının içeriği geri kazanılamaz.
5. 8GiB hostta tam donanım ekonomisi + p99 frame-time + Bannerlord üzerinde gerçek RAM tier kazanımı.

## Sürüm kararı

**v0.6.0-dev kod paketi tamamlanıp v0.7'nin ölçüm/oyun entegrasyon koduna geçilebilir.** 'v0.6 stabil swap' diye adlandırılmayacak ve v1.0 güvenlik kapıları geçilmeden sistem genelinde etkinleştirilmeyecek. Var olan masaüstü sistemde NBD global swap'i yeniden açmıyoruz. Kullanıcı GPU üzerinde güvenli OpenCL laboratuvarı için `v060/README_TR.md` komutları hazırdır.

Kaynak dalı: https://github.com/nopenstudio/nopen-vram-swap/tree/develop/v0.6.0
PR: https://github.com/nopenstudio/nopen-vram-swap/pull/5
