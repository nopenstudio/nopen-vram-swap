#define _POSIX_C_SOURCE 200809L
/* v0.8 alpha.2: exactly 1GiB AMD OpenCL transfer experiment, NO kernel swap.
 * Two modes use the same nopen_store API: direct or explicitly pinned host.
 * 4MiB buffers; full 1GiB GPU write/read verification with bounded host RAM.
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
static int run(nopen_backend backend,size_t mib,const char *mode,
               size_t operations) {
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
    /* Force the whole capacity into the backend. Validate ALL bytes from
     * every 4MiB chunk, not just a sparse sample or one byte per 4KiB. */
    for(size_t offset=0;offset<bytes;offset+=BUFFER_BYTES) {
        const size_t len=(bytes-offset<BUFFER_BYTES)
                        ?bytes-offset:BUFFER_BYTES;
        fill_pattern(source,len,(uint64_t)offset,0);
        const int rc=checked_io(store,offset,source,got,len,NULL,NULL);
        if(rc!=0) {
            fprintf(stderr,"V080_GPU_INTEGRITY_FAILED"
                    " phase=full_arena offset=%zu rc=%d\n",offset,rc);
            nopen_store_close(store);free(source);free(got);return 6;
        }
    }
    printf("V080_GPU_FULL_ARENA_VERIFIED backend=%s mode=%s"
           " capacity_mib=%zu full_bytes_checked=%zu"
           " host_staging_mib=8 kernel_swap=NO\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",
           mode,mib,bytes);
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
           " elapsed_seconds=%.3f\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",mode,mib,
           (time_ns()-start)/1.0e9);
    nopen_store_close(store);
    free(source);free(got);
    return 0;
}
int main(int argc,char **argv) {
    const char *backend_text=NULL,*mode=NULL,*ack=NULL;
    size_t mib=0,operations=128;
    for(int i=1;i<argc;i++) {
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--backend"))backend_text=val;
        else if(!strcmp(key,"--mode"))mode=val;
        else if(!strcmp(key,"--ack"))ack=val;
        else if(!strcmp(key,"--pool-mib") ||
                !strcmp(key,"--operations")) {
            if(val[0]=='+' || val[0]=='-')return 2;
            char *end=NULL;errno=0;
            unsigned long x=strtoul(val,&end,10);
            if(errno || end==val || *end)return 2;
            if(!strcmp(key,"--pool-mib"))mib=(size_t)x;
            else operations=(size_t)x;
        } else return 2;
    }
    if(!backend_text || !mode ||
       (strcmp(mode,"direct")!=0 && strcmp(mode,"pinned")!=0))return 2;
    const nopen_backend backend=!strcmp(backend_text,"opencl")
       ?NOPEN_STORE_OPENCL:!strcmp(backend_text,"mock")?NOPEN_STORE_MOCK:0;
    if(!backend || geteuid()==0 || !no_gpu_swap_active() ||
       operations<16 || operations>MEASURE_MAX ||
       (backend==NOPEN_STORE_OPENCL
         ?(mib!=1024 || !ack || strcmp(ack,"I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP"))
         :(mib<4 || mib>32))) {
        fputs("V080_GPU_BENCH_REFUSED safety guard: no root/ublk swap;"
              " real OpenCL ONLY 1024MiB with --ack"
              " I_ACCEPT_1GIB_GPU_ONLY_NO_SWAP; mock 4..32MiB\n",stderr);
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
    return run(backend,mib,mode,operations);
}
