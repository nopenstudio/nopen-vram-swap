# Nopen VRAM SWAP v0.5.0-dev — Linux-zen VM-safe architecture gate

**Status: experimental guarded Linux userfaultfd prototype, NOT a kernel module or a Linux swap driver.** The first stable release remains v1.0.

## What v0.4 actually proved

Ryzen 5 7500F / RX 9060 XT, 64MiB, prefetch16:
- 17/17 local tests passed; both baseline and GPU backends verified all 16384 pages.
- CPU baseline: fill 0.097196 s, verification 0.090826 s, peak RSS 67940 KiB.
- OpenCL GPU pager: fill 0.169479 s, verification 1.105549 s, peak RSS 108100 KiB.
- GPU pager **12.17x slower at sequential readback**, whole fill+verify **6.78x slower**, +40160 KiB process RSS (+39.22 MiB); 64 MiB was a NEGATIVE memory-economics result.
- Prior v0.2 4MiB block transfer: >20 GB/s H2D and D2H, but **not** equivalent to a 4KiB random fault or an application's paging path.

Therefore, no 2x/3x speedup claim is justified. v0.4 tests at 256MiB and 1024MiB remain unverified on the actual GPU.

## Architectural boundary v0.5

The dangerous approach is to issue GPU commands synchronously from Linux direct reclaim. A GPU allocation / firmware / driver completion path may itself need pageable RAM and reclaim; the reclaim thread then waits for a GPU operation whose progress depends on reclaim. This can produce deadlock, CPU stalls, or system panic. Existing failed NBD global-swap experiments make this particularly important.

v0.5 now includes both **read-only VM advisory** and a **supervised, process-local Linux userfaultfd pager**, not direct kernel reclaim integration:

- Sensor reads /proc/meminfo (MemTotal, MemAvailable); /proc/pressure/memory (PSI some/full avg10); /proc/swaps (active NBD guard); and discrete AMD /sys/bus/pci/devices/.../mem_info_vram_total/used.
- No sysctl writes, no cgroup mutation, no swapon/swapoff, no NBD, no ioctl, no kernel modules and no root required.
- Freestanding pure C11 policy has ZERO external symbols, independent of GNU and Linux. Runtime observer is POSIX/Linux-specific and therefore NOT GNU-free NopenOS runtime.
- Policy outputs only a recommendation: NORMAL=16 pages, CONSERVATIVE=4 pages, PAUSE=0, REFUSE=0. **These are engineering defaults, not empirically optimized thresholds.** Missing/contradictory sensor readings or NBD swap => REFUSE.
- Fail-safe headroom: >=2GiB (and 20% RAM) available, >=4GiB GPU VRAM spare beyond a small 64MiB experimental reservation, PSI full avg10 below 2%, PSI some avg10 below 10%.
- PSI some >=2% or full >=0.25% => conservative prefetch; PSI some >=10% or full >=2% => pause. The PSI thresholds need validation on the user's real workload.
- The probe cannot protect against all AMDGPU kernel driver bugs. Read-only polling also cannot prove that a GPU memory object is permanently physically resident in VRAM.

## On-device read-only test

~~~bash
cd ~
git clone --single-branch --branch develop/v0.5.0 \
  https://github.com/nopenstudio/nopen-vram-swap.git nopen-v050
cd ~/nopen-v050
cmake -S v050 -B build-v050 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v050 --parallel
ctest --test-dir build-v050 --output-on-failure
./build-v050/nopen-vm-probe --watch 3 | tee ~/nopen-v050-vm-observation.log
~~~

This command does not touch Bannerlord, kernel paging or swap. It only reads the current state. VM_PROBE_PASS means the sensor successfully read the fields, NOT that system swap works or the policy was adopted by a runtime pager.

The two larger v0.4 economics experiments remain a hard research gate. Do **not** attempt kernel memory integration on the main PC merely because the sensor succeeds.

## Architecture target for a future isolated kernel test

Strict requirements BEFORE any kernel module, reclaim hook or swap hook:
1. A separate physically isolated test machine or otherwise safe containment with independent console, crash logs, watchdog and out-of-band recovery; single-GPU passthrough may not be viable.
2. No GPU command submission or pageable RAM allocation in direct reclaim; strictly bounded, preallocated pinned staging, nonblocking enqueue and clear progress invariants. Review exact AMDGPU lock ordering, buffer eviction/residency and required kernel allocation context.
3. Faults and device reset have a defined data-fate policy; do not destroy the sole authoritative page copy while a GPU command is in-flight. Reserved CPU fallback memory or reliable replica must exist.
4. Proven bounded page-in/out latency including p99/max under memory pressure; DMA bulk bandwidth is not a substitute.
5. No changes to init/PID1 memory, system swap, host VM allocation policy or GPU device binding during initial testing.
6. Verify net host RAM economy at larger v0.4 logical sizes BEFORE risking kernel integration.

Official Linux kernel references:
- PSI: https://docs.kernel.org/accounting/psi.html
- MM allocation contexts / direct reclaim: https://docs.kernel.org/core-api/mm-api.html
- GFP and recursion deadlocks: https://docs.kernel.org/core-api/gfp_mask-from-fs-io.html

**v0.5 is an application-local Linux VM ABI prototype with a watchdog; it does not implement a global kernel-side swap/reclaim fast path and cannot yet be called a complete kernel-level memory tier.**

## Yeni: PSI kontrolü altında gerçek userfaultfd alt süreç deneyi

Linux çekirdeğinin `UFFDIO_REGISTER` ve `UFFDIO_COPY` arayüzleriyle yalnızca kendi ayrılmış uygulama bölgesinde page-in / page-out yapan eski `nopen-vram-demand` prototipini v0.5 denetleyicisine bağladık.

- `nopen-vm-gate` ana kullanıcı prosesidir; `nopen_vm_snapshot_linux()` ile MemAvailable, PSI, GPU VRAM ve aktif swap durumunu başlangıçta, ardından 100ms aralıklarla tekrar okur.
- Yalnızca `NORMAL` / `CONSERVATIVE` koşulunda test alt sürecini `fork` + `execv` ile başlatır; bellek yetersizliği, telemetri kaybı veya GPU cihazının kaybolması hâlinde alt süreci sonlandırır. `--timeout-seconds` 5–60 ile sınırlıdır.
- Ana prosesin kernel ayarları, swap cihazları ve diğer programların belleği değişmez. OpenCL işlemleri **userfaultfd worker alt sürecinde**, doğrudan kernel reclaim bağlamı dışında yapılır.
- Mock tabanlı kernel UFFD koşusu + 2. örneklemede yapay bellek baskısı tetikleyerek supervisor'ın yalnızca alt süreci sonlandırmasını sınayan entegrasyon testi CI'ya eklendi.
- Gözlemci kararları henüz prefetch sayılarına uygulanmaz; output `prefetch_applied=NO` diye raporlar. Kernel reclaim hook da `NO`. Bu iki hedef tamamlanmadan global SWAP iddiası yapılamaz.

### Gerçek donanımda güvenli test

~~~bash
cd ~/nopen-v050
git pull --ff-only
cmake -S v050 -B build-v050 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v050 --parallel
ctest --test-dir build-v050 --output-on-failure
./build-v050/nopen-vm-gate \
  --pager "$PWD/build-v050/v040/v030/v012/nopen-vram-demand" \
  --backend opencl --pool-mib 64 --logical-pages 1024 \
  --ram-pages 32 --operations 1000 --timeout-seconds 15 \
  | tee ~/nopen-v050-uffd-gate.log
~~~

`VM_GATE_PASS`, `STRESS_PASS` ve `verified_final_pages=1024` beraber görülmedikçe donanım kabulü yapılmaz. `VM_GATE_ABORT` sorun saptandığında süreç sonlandırmanın çalıştığını gösterir; bu bir performans kazanımı değildir.

256 MiB v0.4 gerçek GPU sonucunda CPU 264496 KiB / GPU 110104 KiB tepe RSS kullanıldı: GPU yolu **154392 KiB (150.77 MiB; %58.37)** daha az süreç RAM'iyle 65536/65536 sayfayı doğruladı; buna karşılık 4.296529 saniye okumada CPU'nun 0.362705 saniyesinden çok daha yavaştı. Verinin kaynağı: `v050/docs/RX9060XT_V040_256MIB.md`. **Sistem genelinde net RAM kazanımı henüz kanıtlanmadı.**

### v0.5 kabul durumu

CI mock, asan, OpenCL derleme ve supervised UFFD fault-isolation testi geçti; gerçek RX 9060 XT üzerinde `nopen-vm-gate` donanım kabulü henüz alınmadı. v0.5 uygulama düzeyi Linux VM ABI prototipi geliştirildi fakat çekirdek modülü/gerçek Linux swap için güvenli yeniden kazanım (reclaim) entegrasyonu henüz gerçekleştirilmedi. Bu sınır gizlenmeyecek.

## Kernel ublk yolu: gerçek blok veri motoru ve deneysel bağdaştırıcı

v0.5 kaynak koduna artık *gerçek* OpenCL VRAM kullanabilen 4KiB hizalı `nopen_blk_backend` eklendi (en çok 16 sayfalık tek I/O). Linux `ublk` blok katmanı callback'ini bu motora bağlayan C++ adaptör `v050/experimental/ublk/nopen_ublk_target.cpp` dosyasında. `ublk` resmi kernel blk-mq/io_uring altyapısıdır; kernel doğrudan OpenCL çağırmaz, I/O kullanıcı alanındaki ublk server'a iletilir. Ancak bu tek başına swap reclaim deadlock riskini çözmez.

**Yapılmış doğrulamalar:** explicit mock testlerinde 2000 rastgele okuma/yazma, 128 sayfanın son doğrulanması, 4KiB hizalama/sınır, zorla write/read failure ve poison-page davranışı; CI OpenCL derlemesi ve Linux upstream ublksrv callback C++ sözdizimi kontrolü. Fiziksel RX 9060 XT üzerinde yeni motorun kabul testi ve `ublk` link/runtime/VM block-device testi henüz yapılmadı.

**Sınır:** Bu `ublk` adaptörü standart CMake build'e eklenmez, hiçbir blok aygıtı otomatik oluşturmaz, normal masaüstünde DMI + özel çevre kilidi nedeniyle başlamayı reddeder. **Hiçbir koşulda ana bilgisayarda swap olarak etkinleştirmeyin.** Adaptör `FLUSH`/kalıcılık garanti etmez ve GPU resetinde veri kaybedebilir.

Resmî kernel belge: https://docs.kernel.org/block/ublk.html

### Yeni motorun güvenli donanım test komutu

~~~bash
cd ~/nopen-v050
git pull --ff-only
cmake -S v050 -B build-v050 -DCMAKE_BUILD_TYPE=Release -DNOPEN_OPENCL=ON
cmake --build build-v050 --parallel
ctest --test-dir build-v050 --output-on-failure
./build-v050/nopen-blk-check --backend opencl --operations 1000 | tee ~/nopen-v050-block-gpu.log
~~~

Bu araç yalnızca ayrı bir kullanıcı sürecinde 64 MiB VRAM ayırır, 4 MiB'lik benzersiz verileri 4KiB bloklar hâlinde doğrular; `ublk` veya global swap başlatmaz. PASS yalnızca **OpenCL blok motoru** için geçerlidir.

### v0.5 kabul kapısı

- [x] Çekirdek kullanıcı alanı VM API (`userfaultfd`) + ayrı watchdog ve PSI.
- [x] GPU blok backend'i / Linux ublk callback kaynak kodu.
- [x] Mock, OpenCL kaynak derleme, sanitizer ve upstream ublk başlığı sözdizimi testleri.
- [ ] RX 9060 XT fiziksel blok motoru kabul testi.
- [ ] ublk gerçek kernel device lifecycle + I/O testi, yalnızca yalıtılmış kernel test makinesinde.
- [ ] Kernel reclaim/OOM altında ilerleme, GPU reseti, blok I/O kaybı durumunda güvenli kurtarma ve sistem canlılığı ispatı.

İlk üç madde kod tarafında tamamlanmış olsa da çekirdek reclaim'in SWAP aracılığıyla sisteme bağlanması hâlâ test edilmedi. Bunu 'v0.5 tamamen bitti' olarak sunmak gerçek dışı olur.
