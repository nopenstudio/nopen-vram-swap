#define _POSIX_C_SOURCE 200809L
/* v0.8 alpha.4: 1/2/4GiB AMD OpenCL transfer experiments, NO kernel swap.
 * Two modes use the same nopen_store API: direct or explicitly pinned host.
 * 4MiB buffers; all pages verified with per-page identity tags and bounded host RAM.
 * Mock backend is only for CI validation, never evidence of GPU performance. */
#include "nopen_store.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PAGE NOPEN_PAGE_SIZE
#define MIB (1024u*1024u)
#define BUFFER_BYTES (4u*MIB)
#define MEASURE_MAX 1024u

static double time_ns(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1.0;
    return (double)t.tv_sec*1.0e9+(double)t.tv_nsec;
}
static int sort_doubles(const void *a,const void *b) {
    const double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
static double pctl(const double *samples,size_t count,unsigned percentile) {
    double tmp[MEASURE_MAX];
    if(count>MEASURE_MAX || count==0)return -1;
    memcpy(tmp,samples,count*sizeof(*tmp));
    qsort(tmp,count,sizeof(tmp[0]),sort_doubles);
    size_t at=((size_t)percentile*count+99u)/100u;
    if(at<1)at=1;
    if(at>count)at=count;
    return tmp[at-1]/1000.0;
}
static void fill_pattern(unsigned char *buffer,size_t bytes,uint64_t address,
                         uint64_t iteration) {
    /* Every byte matters; page and iteration participate in the pattern.
     * The source is deterministic but not trivially all-zeros. */
    for(size_t i=0;i<bytes;i++) {
        uint64_t pg=(address+i)/PAGE;
        buffer[i]=(unsigned char)((pg*37u)+(i*13u)+(i>>7)+(iteration*19u));
    }
    /* Old 8-bit pattern repeats every 256 pages: a misaddressed buffer
     * could appear correct. Stamp the absolute 64-bit page number and
     * generation in each 4KiB page to reject aliasing across the arena.
     * All benchmark transfers are page-aligned and page-sized. */
    for(size_t off=0;off<bytes;off+=PAGE) {
        const uint64_t id=(address+off)/PAGE;
        for(unsigned j=0;j<8;j++) {
            buffer[off+j]=(unsigned char)(id>>(j*8u));
            buffer[off+8u+j]=(unsigned char)(iteration>>(j*8u));
        }
    }
}
static int no_gpu_swap_active(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char device[256]={0};
        if(sscanf(line,"%255s",device)==1 &&
           (strncmp(device,"/dev/ublkb",10)==0 ||
            strncmp(device,"/dev/nbd",8)==0)) {
            fclose(f);return 0;
        }
    }
    int rc=!ferror(f);
    fclose(f);
    return rc;
}
static int read_u64(const char *path,uint64_t *out) {
    FILE *f=fopen(path,"r");
    if(!f)return -1;
    unsigned long long val=0;
    int got=fscanf(f,"%llu",&val)==1;
    fclose(f);
    if(!got)return -1;
    *out=(uint64_t)val;
    return 0;
}
static int check_gpu_budget(size_t mib) {
    DIR *d=opendir("/sys/bus/pci/devices");
    if(!d)return -1;
    struct dirent *ent;
    unsigned selected=0;
    uint64_t total=0,used=0;
    while((ent=readdir(d))!=NULL) {
        if(ent->d_name[0]=='.')continue;
        char base[512],path[640];
        if(snprintf(base,sizeof base,"/sys/bus/pci/devices/%s",ent->d_name)<0)
            continue;
        uint64_t t=0,u=0;
        snprintf(path,sizeof path,"%s/vendor",base);
        FILE *f=fopen(path,"r");if(!f)continue;
        unsigned long long v=0;
        int valid=fscanf(f,"%llx",&v)==1;
        fclose(f);
        if(!valid || v!=0x1002ULL)continue;
        snprintf(path,sizeof path,"%s/class",base);
        f=fopen(path,"r");if(!f)continue;
        unsigned long long c=0;
        valid=fscanf(f,"%llx",&c)==1;
        fclose(f);
        if(!valid || (c>>16)!=0x03)continue;
        snprintf(path,sizeof path,"%s/mem_info_vram_total",base);
        if(read_u64(path,&t)!=0)continue;
        snprintf(path,sizeof path,"%s/mem_info_vram_used",base);
        if(read_u64(path,&u)!=0)continue;
        if(u>t || t==0){closedir(d);return -1;}
        selected++;
        total=t;used=u;
    }
    closedir(d);
    if(selected!=1)return -1;
    const uint64_t reserve=(uint64_t)mib*MIB;
    const uint64_t headroom=4ull*1024ull*MIB;
    if(reserve>total*3u/4u || used>total ||
       reserve>total-used || headroom>total-used-reserve)return -1;
    printf("V080_GPU_BUDGET_PASS gpu_vram_total_mib=%" PRIu64
           " gpu_vram_used_mib=%" PRIu64
           " capacity_mib=%zu headroom_min_mib=4096\n",
           total/MIB,(used+MIB-1)/MIB,mib);
    return 0;
}
static int check_mem_available(void) {
    FILE *f=fopen("/proc/meminfo","r");if(!f)return -1;
    char line[256];unsigned long long kib=0;
    int found=0;
    while(fgets(line,sizeof line,f))
        if(sscanf(line,"MemAvailable: %llu kB",&kib)==1) {
            found=1;break;
        }
    fclose(f);
    return found && kib>=1024u*1024u?0:-1;
}
static int checked_io(nopen_store *store,size_t offset,unsigned char *source,
                      unsigned char *got,size_t bytes,double *write_ns,
                      double *read_ns) {
    double t=time_ns();
    if(t<0)return -1;
    if(nopen_store_write_pages(store,offset/PAGE,source,bytes/PAGE)!=0)
        return -2;
    double after_write=time_ns();
    if(after_write<0)return -3;
    if(nopen_store_read_pages(store,offset/PAGE,got,bytes/PAGE)!=0)
        return -4;
    double after_read=time_ns();
    if(after_read<0 || memcmp(source,got,bytes)!=0)return -5;
    if(write_ns)*write_ns=after_write-t;
    if(read_ns)*read_ns=after_read-after_write;
    return 0;
}
/* When an experimental GPU-only arena coexists with a game, revoke the
 * experiment if OS or GPU headroom becomes too small. It never acts on the
 * game process, changes swapon, or launches a block device. */
static int hold_preflight(void) {
    if(!no_gpu_swap_active() || check_mem_available()!=0)return -1;
    DIR *d=opendir("/sys/bus/pci/devices");
    if(!d)return -1;
    struct dirent *ent;
    uint64_t total=0,used=0;
    unsigned found=0;
    while((ent=readdir(d))) {
        if(ent->d_name[0]=='.')continue;
        char p[512],name[640];
        unsigned long long vendor=0,cls=0;
        snprintf(p,sizeof p,"/sys/bus/pci/devices/%s",ent->d_name);
        snprintf(name,sizeof name,"%s/vendor",p);
        FILE *v=fopen(name,"r");
        if(!v)continue;
        int vok=fscanf(v,"%llx",&vendor)==1;
        fclose(v);
        if(!vok||vendor!=0x1002ULL)continue;
        snprintf(name,sizeof name,"%s/class",p);
        v=fopen(name,"r");
        if(!v)continue;
        vok=fscanf(v,"%llx",&cls)==1;
        fclose(v);
        if(!vok || (cls>>16)!=0x03)continue;
        snprintf(name,sizeof name,"%s/mem_info_vram_total",p);
        uint64_t t=0,u=0;
        if(read_u64(name,&t)!=0)continue;
        snprintf(name,sizeof name,"%s/mem_info_vram_used",p);
        if(read_u64(name,&u)!=0 || t==0 || u>t)continue;
        found++;total=t;used=u;
    }
    closedir(d);
    if(found!=1 || total-used<2ull*1024ull*MIB)return -1;
    printf("V080_GAME_HOLD_HEALTH vram_free_mib=%" PRIu64
           " mem_available_over_1g=YES kernel_swap=NO\n",(total-used)/MIB);
    fflush(stdout);
    return 0;
}
static int run(nopen_backend backend,size_t mib,const char *mode,
               size_t operations,size_t full_passes,size_t hold_seconds) {
    const size_t bytes=mib*MIB;
    unsigned char *source=NULL,*got=NULL;
    if(posix_memalign((void **)&source,PAGE,BUFFER_BYTES)!=0 ||
       posix_memalign((void **)&got,PAGE,BUFFER_BYTES)!=0) {
        free(source);free(got);
        return 4;
    }
    if(setenv("NOPEN_V080_IO_MODE",mode,1)!=0) {
        free(source);free(got);return 4;
    }
    nopen_store *store=NULL;
    const double start=time_ns();
    if(nopen_store_open(&store,backend,mib)!=0) {
        fputs("V080_GPU_BENCH_REFUSED GPU backend/allocation unavailable;"
              " no fallback to host RAM\n",stderr);
        free(source);free(got);return 5;
    }
    /* Prove non-aliasing simultaneous residency with complete tagged writes
     * first, then a distinct complete read pass. Repeating with a new
     * generation catches stale pages; host staging stays at 8MiB.
     * There is NO block device, kernel SWAP, or disk write here. */
    for(size_t pass=0;pass<full_passes;pass++) {
        for(size_t offset=0;offset<bytes;offset+=BUFFER_BYTES) {
            const size_t len=(bytes-offset<BUFFER_BYTES)
                            ?bytes-offset:BUFFER_BYTES;
            fill_pattern(source,len,(uint64_t)offset,(uint64_t)pass);
            if(nopen_store_write_pages(store,offset/PAGE,source,len/PAGE)!=0) {
                fprintf(stderr,"V080_GPU_INTEGRITY_FAILED"
                        " phase=full_arena_write pass=%zu offset=%zu\n",pass+1,offset);
                nopen_store_close(store);free(source);free(got);return 6;
            }
        }
        for(size_t offset=0;offset<bytes;offset+=BUFFER_BYTES) {
            const size_t len=(bytes-offset<BUFFER_BYTES)
                            ?bytes-offset:BUFFER_BYTES;
            fill_pattern(source,len,(uint64_t)offset,(uint64_t)pass);
            if(nopen_store_read_pages(store,offset/PAGE,got,len/PAGE)!=0 ||
               memcmp(source,got,len)!=0) {
                fprintf(stderr,"V080_GPU_INTEGRITY_FAILED"
                        " phase=full_arena_read_after_all_writes pass=%zu offset=%zu\n",
                        pass+1,offset);
                nopen_store_close(store);free(source);free(got);return 6;
            }
        }
        printf("V080_GPU_FULL_ARENA_VERIFIED backend=%s mode=%s"
               " capacity_mib=%zu full_bytes_checked=%zu"
               " all_writes_before_reads=YES simultaneous_logical_capacity=YES"
               " tagged_page_ids=YES verified_pass=%zu"
               " host_staging_mib=8 kernel_swap=NO\n",
               backend==NOPEN_STORE_OPENCL?"opencl":"mock",
               mode,mib,bytes,pass+1);
        fflush(stdout);
    }
    const size_t chunk_sizes[]={PAGE,16u*PAGE,MIB,BUFFER_BYTES};
    for(size_t c=0;c<sizeof chunk_sizes/sizeof chunk_sizes[0];c++) {
        const size_t chunk=chunk_sizes[c];
        double write_ns[MEASURE_MAX],read_ns[MEASURE_MAX];
        double total_write=0,total_read=0;
        const size_t slots=bytes/chunk;
        for(size_t i=0;i<operations;i++) {
            size_t slot=(i*137u+13u)%slots;
            const size_t offset=slot*chunk;
            fill_pattern(source,chunk,(uint64_t)offset,i+1u);
            const int rc=checked_io(store,offset,source,got,chunk,
                                    &write_ns[i],&read_ns[i]);
            if(rc!=0) {
                fprintf(stderr,"V080_GPU_INTEGRITY_FAILED"
                        " phase=throughput chunk=%zu offset=%zu rc=%d\n",
                        chunk,offset,rc);
                nopen_store_close(store);free(source);free(got);return 7;
            }
            total_write+=write_ns[i];total_read+=read_ns[i];
        }
        if(total_write<=0 || total_read<=0) {
            nopen_store_close(store);free(source);free(got);return 8;
        }
        const double transferred_mib=(double)(chunk*operations)/MIB;
        printf("V080_GPU_BENCH_RESULT backend=%s mode=%s capacity_mib=%zu"
               " chunk_kib=%zu operations=%zu"
               " write_mib_s=%.2f read_mib_s=%.2f"
               " write_p50_us=%.2f write_p95_us=%.2f"
               " read_p50_us=%.2f read_p95_us=%.2f"
               " measured_bytes_per_direction=%zu"
               " all_bytes_verified=YES kernel_swap=NO\n",
               backend==NOPEN_STORE_OPENCL?"opencl":"mock",
               mode,mib,chunk/1024u,operations,
               transferred_mib/(total_write/1.0e9),
               transferred_mib/(total_read/1.0e9),
               pctl(write_ns,operations,50),pctl(write_ns,operations,95),
               pctl(read_ns,operations,50),pctl(read_ns,operations,95),
               chunk*operations);
        fflush(stdout);
    }
    printf("V080_GPU_BENCH_PASS backend=%s mode=%s capacity_mib=%zu"
           " full_arena_integrity=YES kernel_swap=NO"
           " full_passes_completed=%zu elapsed_seconds=%.3f\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",mode,mib,
           full_passes,(time_ns()-start)/1.0e9);
    if(hold_seconds) {
        /* Throughput samples modify some pages. First restamp the entire
         * arena with a single generation so hold checks stay meaningful. */
        const uint64_t generation=0xA4u;
        for(size_t off=0;off<bytes;off+=BUFFER_BYTES) {
            fill_pattern(source,BUFFER_BYTES,(uint64_t)off,generation);
            if(nopen_store_write_pages(store,off/PAGE,source,BUFFER_BYTES/PAGE)!=0) {
                fputs("V080_GAME_HOLD_FAILED phase=restamp_write\n",stderr);
                nopen_store_close(store);free(source);free(got);return 9;
            }
        }
        for(size_t off=0;off<bytes;off+=BUFFER_BYTES) {
            fill_pattern(source,BUFFER_BYTES,(uint64_t)off,generation);
            if(nopen_store_read_pages(store,off/PAGE,got,BUFFER_BYTES/PAGE)!=0 ||
               memcmp(source,got,BUFFER_BYTES)!=0) {
                fputs("V080_GAME_HOLD_FAILED phase=restamp_read\n",stderr);
                nopen_store_close(store);free(source);free(got);return 9;
            }
        }
        printf("V080_GAME_HOLD_READY backend=%s capacity_mib=%zu hold_seconds=%zu"
               " kernel_swap=NO gpu_pool_active=YES\n",
               backend==NOPEN_STORE_OPENCL?"opencl":"mock",mib,hold_seconds);
        fflush(stdout);
        for(size_t second=0;second<hold_seconds;second++) {
            if(backend==NOPEN_STORE_OPENCL && hold_preflight()!=0) {
                fputs("V080_GAME_HOLD_ABORT safety_guard: low RAM/VRAM headroom"
                      " or swap activated; releasing pool\n",stderr);
                nopen_store_close(store);free(source);free(got);return 10;
            }
            if(second%5==0) {
                const size_t pages=bytes/PAGE;
                const size_t page=((second/5)*7919u)%pages;
                fill_pattern(source,PAGE,(uint64_t)page*PAGE,generation);
                if(nopen_store_read_pages(store,page,got,1)!=0 ||
                   memcmp(source,got,PAGE)!=0) {
                    fputs("V080_GAME_HOLD_FAILED phase=sample_page_integrity\n",stderr);
                    nopen_store_close(store);free(source);free(got);return 11;
                }
            }
            struct timespec wait={.tv_sec=1,.tv_nsec=0};
            while(nanosleep(&wait,&wait)!=0 && errno==EINTR) {}
        }
        /* Full post-hold verification: close the game first to avoid
         * a large readback burst during sensitive gameplay if possible. */
        for(size_t off=0;off<bytes;off+=BUFFER_BYTES) {
            fill_pattern(source,BUFFER_BYTES,(uint64_t)off,generation);
            if(nopen_store_read_pages(store,off/PAGE,got,BUFFER_BYTES/PAGE)!=0 ||
               memcmp(source,got,BUFFER_BYTES)!=0) {
                fputs("V080_GAME_HOLD_FAILED phase=post_hold_full_read\n",stderr);
                nopen_store_close(store);free(source);free(got);return 12;
            }
        }
        printf("V080_GAME_HOLD_PASS backend=%s capacity_mib=%zu"
               " full_post_hold_integrity=YES kernel_swap=NO\n",
               backend==NOPEN_STORE_OPENCL?"opencl":"mock",mib);
        fflush(stdout);
    }
    nopen_store_close(store);
    free(source);free(got);
    return 0;
}
int main(int argc,char **argv) {
    const char *backend_text=NULL,*mode=NULL,*ack=NULL;
    size_t mib=0,operations=128,full_passes=1,hold_seconds=0;
    for(int i=1;i<argc;i++) {
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--backend"))backend_text=val;
        else if(!strcmp(key,"--mode"))mode=val;
        else if(!strcmp(key,"--ack"))ack=val;
        else if(!strcmp(key,"--pool-mib") ||
                !strcmp(key,"--operations") ||
                !strcmp(key,"--full-passes") ||
                !strcmp(key,"--hold-seconds")) {
            if(val[0]=='+' || val[0]=='-')return 2;
            char *end=NULL;errno=0;
            unsigned long x=strtoul(val,&end,10);
            if(errno || end==val || *end)return 2;
            if(!strcmp(key,"--pool-mib"))mib=(size_t)x;
            else if(!strcmp(key,"--full-passes"))full_passes=(size_t)x;
            else if(!strcmp(key,"--hold-seconds"))hold_seconds=(size_t)x;
            else operations=(size_t)x;
        } else return 2;
    }
    if(!backend_text || !mode ||
       (strcmp(mode,"direct")!=0 && strcmp(mode,"pinned")!=0))return 2;
    const nopen_backend backend=!strcmp(backend_text,"opencl")
       ?NOPEN_STORE_OPENCL:!strcmp(backend_text,"mock")?NOPEN_STORE_MOCK:0;
    /* 4GiB only GPU-only, never kernel SWAP. A passing capacity
     * planner cannot authorize any kernel swap experiment. */
    const char *required_ack=
        (mib==1024)?"I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP":
        (mib==2048)?"I_ACCEPT_2GIB_GPU_ONLY_NO_SWAP":
        (mib==4096)?"I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP":NULL;
    if(!backend || geteuid()==0 || !no_gpu_swap_active() ||
       operations<16 || operations>MEASURE_MAX ||
       full_passes<1 || full_passes>4 ||
       (hold_seconds && (backend==NOPEN_STORE_OPENCL
          ?(mib!=4096 || hold_seconds<60 || hold_seconds>300 ||
             strcmp(mode,"direct")!=0)
          :(hold_seconds>3))) ||
       (backend==NOPEN_STORE_OPENCL
          ?(!required_ack || !ack || strcmp(ack,required_ack)!=0 ||
             (mib>=2048 && full_passes<2))
          :(mib<4 || mib>32))) {
        fputs("V080_GPU_BENCH_REFUSED safety guard: no root/ublk swap;"
              " OpenCL 1024MiB with --ack I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP,"
              " or 2048MiB with --full-passes 2 and"
              " --ack I_ACCEPT_2GIB_GPU_ONLY_NO_SWAP;"
              " or 4096MiB with --full-passes 2 and"
              " --ack I_ACCEPT_4GIB_GPU_ONLY_NO_SWAP;"
              " mock 4..32MiB; NO KERNEL SWAP\n",stderr);
        return 2;
    }
    if(backend==NOPEN_STORE_OPENCL &&
       (check_gpu_budget(mib)!=0 || check_mem_available()!=0)) {
        fputs("V080_GPU_BENCH_REFUSED memory headroom/AMD GPU ambiguity\n",
              stderr);
        return 3;
    }
    printf("V080_GPU_BENCH_START backend=%s mode=%s capacity_mib=%zu"
           " root=NO kernel_swap=NO"
           " real_gpu=%s\n",backend_text,mode,mib,
           backend==NOPEN_STORE_OPENCL?"YES":"NO");
    fflush(stdout);
    return run(backend,mib,mode,operations,full_passes,hold_seconds);
}
