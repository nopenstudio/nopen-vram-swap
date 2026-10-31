#define _POSIX_C_SOURCE 200809L
/* Nopen v0.8.5 Phase B: explicit *soft* GPU-only residency observation.
 * OpenCL buffer allocation is NOT a physically pinned reservation.
 * Never installs a block device, activates swap, changes boot config, or
 * alters what a game reports about its GPU memory budget.
 */
#include "nopen_store.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MIB (1024u * 1024u)
#define CHUNK_BYTES (4u*MIB)
#define PAGES_PER_CHUNK (CHUNK_BYTES/NOPEN_PAGE_SIZE)
#define PAGE NOPEN_PAGE_SIZE

typedef struct {
    uint64_t total, vram_used, gtt_used, mem_available;
} health;

static int get_u64(const char *path,uint64_t *out) {
    FILE *f=fopen(path,"r");
    if(!f)return -1;
    unsigned long long v=0;
    int rc=fscanf(f,"%llu",&v)==1?0:-1;
    fclose(f);
    if(rc)return -1;
    *out=(uint64_t)v;
    return 0;
}
static int read_mem_available(uint64_t *value) {
    FILE *f=fopen("/proc/meminfo","r");
    if(!f)return -1;
    char line[256];
    int success=0;
    while(fgets(line,sizeof line,f)) {
        unsigned long long kib=0;
        if(sscanf(line,"MemAvailable: %llu kB",&kib)==1) {
            *value=(uint64_t)kib*1024u;
            success=1;break;
        }
    }
    if(ferror(f))success=0;
    fclose(f);
    return success?0:-1;
}
static int no_nopen_kernel_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    int safe=1;
    while(fgets(line,sizeof line,f)) {
        char name[256]={0};
        if(sscanf(line,"%255s",name)!=1){safe=0;break;}
        if(strncmp(name,"/dev/ublk",9)==0 ||
           strncmp(name,"/dev/nbd",8)==0){safe=0;break;}
    }
    if(ferror(f))safe=0;
    fclose(f);
    return safe;
}
/* Exactly one AMD PCI graphics device with VRAM stats is supported.
 * This preflight reads whole-system counters, not Nopen's object residency.
 * Multiple devices fail closed rather than silently monitoring the wrong GPU.
 */
static int gpu_health(health *h) {
    DIR *d=opendir("/sys/bus/pci/devices");
    if(!d)return -1;
    unsigned found=0;
    struct dirent *e;
    health tmp={0};
    while((e=readdir(d))!=NULL) {
        if(e->d_name[0]=='.')continue;
        char base[512],file[640];
        int n=snprintf(base,sizeof base,"/sys/bus/pci/devices/%s",e->d_name);
        if(n<0 || (size_t)n>=sizeof base)continue;
        unsigned long long vendor=0,cls=0;
        snprintf(file,sizeof file,"%s/vendor",base);
        FILE *f=fopen(file,"r");
        if(!f)continue;
        int ok=fscanf(f,"%llx",&vendor)==1;
        fclose(f);
        if(!ok || vendor!=0x1002ULL)continue;
        snprintf(file,sizeof file,"%s/class",base);
        f=fopen(file,"r");
        if(!f)continue;
        ok=fscanf(f,"%llx",&cls)==1;
        fclose(f);
        if(!ok || (cls>>16)!=0x03)continue;
        snprintf(file,sizeof file,"%s/mem_info_vram_total",base);
        if(get_u64(file,&tmp.total))continue;
        snprintf(file,sizeof file,"%s/mem_info_vram_used",base);
        if(get_u64(file,&tmp.vram_used))continue;
        snprintf(file,sizeof file,"%s/mem_info_gtt_used",base);
        if(get_u64(file,&tmp.gtt_used))continue;
        found++;
        if(found>1)break;
    }
    closedir(d);
    if(found!=1 || tmp.total==0 || tmp.vram_used>tmp.total)return -1;
    if(read_mem_available(&tmp.mem_available)!=0)return -1;
    *h=tmp;
    return 0;
}
static int healthy(const health *h,unsigned mib,int initial) {
    const uint64_t minimum_host=1024ull*MIB;
    const uint64_t minimum_graphics=(initial?4096ull:2048ull)*MIB;
    const uint64_t wanted=(initial?(uint64_t)mib*MIB:0);
    if(h->mem_available<minimum_host ||
       h->vram_used>h->total ||
       wanted>h->total-h->vram_used ||
       minimum_graphics>h->total-h->vram_used-wanted)return 0;
    if(initial && wanted>h->total*3/4)return 0;
    return 1;
}
static void print_health(const char *phase,const health *h,
                         const health *baseline) {
    /* Whole-GPU deltas can reflect ANY process. Not proof of VRAM pinning. */
    int64_t dv=(int64_t)(h->vram_used/MIB)-(int64_t)(baseline->vram_used/MIB);
    int64_t dg=(int64_t)(h->gtt_used/MIB)-(int64_t)(baseline->gtt_used/MIB);
    printf("V085_HOLDER_GPU_HEALTH phase=%s"
           " vram_used_mib=%" PRIu64 " gtt_used_mib=%" PRIu64
           " mem_available_mib=%" PRIu64
           " vram_global_delta_mib=%" PRId64
           " gtt_global_delta_mib=%" PRId64
           " deltas_attributed_to_nopen=NO pinned_physical_vram=UNPROVEN\n",
           phase,h->vram_used/MIB,h->gtt_used/MIB,h->mem_available/MIB,
           dv,dg);
    fflush(stdout);
}
static void stamp(unsigned char *p,size_t count,uint64_t byte_offset,
                  uint64_t generation) {
    for(size_t i=0;i<count;i++) {
        const uint64_t page=(byte_offset+i)/PAGE;
        p[i]=(unsigned char)(page*37u+i*13u+(i>>7)+generation*19u);
    }
    for(size_t off=0;off<count;off+=PAGE) {
        uint64_t page=(byte_offset+off)/PAGE;
        for(unsigned j=0;j<8;j++) {
            p[off+j]=(unsigned char)(page>>(8u*j));
            p[off+8+j]=(unsigned char)(generation>>(8u*j));
        }
    }
}
static int verify_all(nopen_store *s,unsigned char *src,
                      unsigned char *got,size_t bytes,uint64_t generation) {
    for(size_t off=0;off<bytes;off+=CHUNK_BYTES) {
        size_t n=bytes-off<CHUNK_BYTES?bytes-off:CHUNK_BYTES;
        stamp(src,n,off,generation);
        if(nopen_store_read_pages(s,off/PAGE,got,n/PAGE)!=0 ||
           memcmp(src,got,n)!=0) {
            fprintf(stderr,"V085_HOLDER_INTEGRITY_FAIL offset_bytes=%zu\n",off);
            return -1;
        }
    }
    return 0;
}
static int hold_loop(nopen_store *s,unsigned mib,unsigned secs,
                     unsigned char *src,unsigned char *got,
                     int mock,const health *baseline) {
    const uint64_t generation=0x85u;
    const size_t size=(size_t)mib*MIB;
    for(size_t off=0;off<size;off+=CHUNK_BYTES) {
        size_t n=size-off<CHUNK_BYTES?size-off:CHUNK_BYTES;
        stamp(src,n,off,generation);
        if(nopen_store_write_pages(s,off/PAGE,src,n/PAGE)!=0) {
            fprintf(stderr,"V085_HOLDER_WRITE_FAIL offset_bytes=%zu\n",off);
            return 6;
        }
    }
    if(verify_all(s,src,got,size,generation)!=0)return 7;
    puts("V085_HOLDER_FULL_ARENA_PASS all_pages_tagged=YES"
         " initial_readback=YES kernel_swap=NO");
    fflush(stdout);
    for(unsigned sec=0;sec<secs;sec++) {
        if(!no_nopen_kernel_swap()) {
            fputs("V085_HOLDER_ABORT ublk_or_nbd_swap_active_or_unreadable\n",stderr);
            return 8;
        }
        if(!mock) {
            health current={0};
            if(gpu_health(&current)!=0 ||
               current.total!=baseline->total ||
               !healthy(&current,mib,0)) {
                fputs("V085_HOLDER_ABORT gpu_or_host_headroom_insufficient\n",stderr);
                return 8;
            }
            if(sec%5==0)print_health("hold",&current,baseline);
        }
        /* Check one deterministic page per second without allocating RAM. */
        const size_t pages=size/PAGE;
        const size_t pg=(((size_t)sec+1u)*7919u)%pages;
        const size_t chunk_off=(pg/PAGES_PER_CHUNK)*CHUNK_BYTES;
        const size_t page_off=(pg%PAGES_PER_CHUNK)*PAGE;
        stamp(src,CHUNK_BYTES,chunk_off,generation);
        if(nopen_store_read_pages(s,pg,got,1)!=0 ||
           memcmp(src+page_off,got,PAGE)!=0) {
            fprintf(stderr,"V085_HOLDER_SAMPLE_FAIL page=%zu\n",pg);
            return 9;
        }
        struct timespec nap={.tv_sec=1,.tv_nsec=0};
        while(nanosleep(&nap,&nap)!=0 && errno==EINTR){}
    }
    if(verify_all(s,src,got,size,generation)!=0)return 10;
    puts("V085_HOLDER_FINAL_INTEGRITY_PASS full_arena=YES"
         " kernel_swap=NO physical_pin=UNPROVEN");
    fflush(stdout);
    return 0;
}
static int parse_unsigned(const char *s,unsigned *out) {
    if(!s || !*s || *s=='-' || *s=='+')return -1;
    errno=0;
    char *end=NULL;
    unsigned long value=strtoul(s,&end,10);
    if(errno || end==s || *end || value>4096u)return -1;
    *out=(unsigned)value;
    return 0;
}
int main(int argc,char **argv) {
    const char *backend=NULL,*ack=NULL;
    unsigned pool=0,seconds=0;
    for(int i=1;i<argc;i++) {
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*value=argv[i];
        if(strcmp(key,"--backend")==0)backend=value;
        else if(strcmp(key,"--ack")==0)ack=value;
        else if(strcmp(key,"--pool-mib")==0) {
            if(parse_unsigned(value,&pool))return 2;
        } else if(strcmp(key,"--seconds")==0) {
            if(parse_unsigned(value,&seconds))return 2;
        } else return 2;
    }
    const int real=backend && strcmp(backend,"opencl")==0;
    const int mock=backend && strcmp(backend,"mock")==0;
    if(geteuid()==0 || !no_nopen_kernel_swap() ||
       (!real && !mock) ||
       (real && (pool!=1024 || seconds<5 || seconds>60 ||
                 !ack || strcmp(ack,"I_ACCEPT_1GIB_HOLDER_NO_SWAP")!=0)) ||
       (mock && (pool<4 || pool>16 || seconds>2))) {
        fputs("V085_HOLDER_REFUSED backend=mock:4..16MiB/0..2s "
              "or backend=opencl:1024MiB/5..60s explicit ACK; "
              "root=NO kernel_swap=NO\n",stderr);
        return 2;
    }
    health baseline={0};
    if(real && (gpu_health(&baseline)!=0 || !healthy(&baseline,pool,1))) {
        fputs("V085_HOLDER_REFUSED gpu_vram_or_ram_headroom_failed\n",stderr);
        return 3;
    }
    unsigned char *src=NULL,*got=NULL;
    if(posix_memalign((void **)&src,PAGE,CHUNK_BYTES)!=0 ||
       posix_memalign((void **)&got,PAGE,CHUNK_BYTES)!=0) {
        free(src);free(got);
        return 4;
    }
    if(real)print_health("before",&baseline,&baseline);
    nopen_store *store=NULL;
    int rc=0;
    if(nopen_store_open(&store,real?NOPEN_STORE_OPENCL:NOPEN_STORE_MOCK,pool)!=0) {
        fputs("V085_HOLDER_REFUSED could_not_allocate_GPU_object\n",stderr);
        rc=5;
    } else {
        printf("V085_HOLDER_STARTED backend=%s pool_mib=%u hold_seconds=%u"
               " only_userland=YES kernel_swap=NO guaranteed_pin=NO\n",
               real?"opencl":"mock",pool,seconds);
        fflush(stdout);
        rc=hold_loop(store,pool,seconds,src,got,mock,&baseline);
    }
    nopen_store_close(store);
    if(real) {
        health after={0};
        if(gpu_health(&after)==0)print_health("after",&after,&baseline);
        else fputs("V085_HOLDER_WARNING after_counters_unavailable\n",stderr);
    }
    free(src);free(got);
    if(rc==0)
        puts("V085_HOLDER_PASS integrity=YES pool_released=YES"
             " physical_pin=UNPROVEN no_driver_reservation=YES"
             " no_kernel_swap=YES");
    return rc;
}
