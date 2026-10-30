#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define PAGE 4096u
#define MIB (1024u*1024u)
#define SAMPLE_PAGES 1024u

static double clock_us(void) {
    struct timespec ts={0};
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return -1;
    return ts.tv_sec*1.0e6+ts.tv_nsec/1.0e3;
}
static int cmp_double(const void *a,const void *b) {
    const double x=*(const double*)a,y=*(const double*)b;
    return (x>y)-(x<y);
}
static int has_kernel_gpu_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 1;
    char line[256]={0};int bad=0;
    if(!fgets(line,sizeof(line),f)){fclose(f);return 1;}
    while(fgets(line,sizeof(line),f)) {
        if(!strncmp(line,"/dev/ublkb",10) || !strncmp(line,"/dev/nbd",8))
            bad=1;
    }
    if(ferror(f))bad=1;
    fclose(f);return bad;
}
int main(int argc,char **argv) {
    const char *worker=NULL,*disk=NULL;
    nopen_backend backend=0;
    const char *mode="async";
    size_t mib=4,ops=2000;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("Nopen v080 alpha6 async GPU cache benchmark (NO kernel SWAP):"
                 " --worker /abs/path --shadow-file /abs/disk/path"
                 " --backend mock|opencl --capacity-mib 4|256|4096"
                 " --mode sync|async|disk-only --operations 100..10000");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*value=argv[i];
        if(!strcmp(key,"--worker"))worker=value;
        else if(!strcmp(key,"--mode")) {
            if(strcmp(value,"async") && strcmp(value,"sync") &&
               strcmp(value,"disk-only"))return 2;
            mode=value;
        }
        else if(!strcmp(key,"--shadow-file"))disk=value;
        else if(!strcmp(key,"--backend")) {
            if(!strcmp(value,"opencl"))backend=NOPEN_STORE_OPENCL;
            else if(!strcmp(value,"mock"))backend=NOPEN_STORE_MOCK;
            else return 2;
        } else if(!strcmp(key,"--capacity-mib")||
                  !strcmp(key,"--operations")) {
            char *end=NULL;errno=0;
            const unsigned long v=strtoul(value,&end,10);
            if(errno||end==value||*end)return 2;
            if(!strcmp(key,"--capacity-mib"))mib=(size_t)v;
            else ops=(size_t)v;
        } else return 2;
    }
    if(geteuid()==0 || has_kernel_gpu_swap() || !backend ||
       !worker || worker[0]!='/' || !disk || disk[0]!='/' ||
       (mib!=4 && mib!=256 && mib!=4096) || ops<100||ops>10000) {
        fputs("V080_ASYNC_BENCH_REFUSED invalid args or root or GPU SWAP active\n",stderr);
        return 2;
    }
    if(setenv("NOPEN_SHADOW_ASYNC_GPU",
              strcmp(mode,"sync")==0?"0":"1",1)!=0)return 2;
    unsigned char *expected=calloc(SAMPLE_PAGES,PAGE);
    double *lat=calloc(ops,sizeof(*lat));
    unsigned char buf[PAGE],got[PAGE];
    nopen_shadow *shadow=NULL;
    int result=3;
    if(!expected||!lat)goto cleanup;
    printf("V080_ASYNC_BENCH_START backend=%s mode=%s capacity_mib=%zu"
           " sample_pages=%u kernel_swap=NO durable_disk=YES\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",mode,mib,SAMPLE_PAGES);
    fflush(stdout);
    const double init_start=clock_us();
    if(nopen_shadow_open(&shadow,backend,mib,2000,worker,disk)!=0)
        goto cleanup;
    const double init_s=(clock_us()-init_start)/1.0e6;
    if(strcmp(mode,"disk-only")==0) {
        /* Baseline: real direct/synchronous SSD durability with all Nopen
         * metadata, page checksum and locks, but WITHOUT GPU cache writes.
         * No kernel SWAP is installed or activated. */
        if(nopen_shadow_simulate_gpu_loss(shadow)!=0)goto cleanup;
    }
    const size_t pages=mib*MIB/PAGE;
    const double start=clock_us();
    for(size_t i=0;i<ops;i++) {
        const size_t index=(i*131u+i/7u)%SAMPLE_PAGES;
        const size_t device_page=(index*7919u)%pages;
        for(size_t k=0;k<PAGE;k++)
            buf[k]=(unsigned char)(i*37u+k*11u+(index>>2u));
        const double before=clock_us();
        if(nopen_shadow_write(shadow,(uint64_t)device_page*PAGE,buf,PAGE)!=0) {
            fprintf(stderr,"V080_ASYNC_BENCH_FAILED phase=write op=%zu\n",i);
            goto cleanup;
        }
        lat[i]=clock_us()-before;
        memcpy(expected+index*PAGE,buf,PAGE);
        if(i%97u==0) {
            if(nopen_shadow_read(shadow,(uint64_t)device_page*PAGE,got,PAGE)!=0 ||
               memcmp(got,buf,PAGE)!=0) {
                fprintf(stderr,"V080_ASYNC_BENCH_FAILED phase=immediate_read op=%zu\n",i);
                goto cleanup;
            }
        }
    }
    const double write_s=(clock_us()-start)/1.0e6;
    nopen_shadow_stats stats={0};
    if(nopen_shadow_get_stats(shadow,&stats)!=0 ||
       stats.ssd_write_samples!=ops)goto cleanup;
    /* GPU uploads are deliberately cancelled now. Every acknowledged page
     * must still round-trip from the authoritative SSD without GPU. */
    if(nopen_shadow_simulate_gpu_loss(shadow)!=0)goto cleanup;
    const size_t seen_start=ops>SAMPLE_PAGES?ops-SAMPLE_PAGES:0;
    size_t verified=0;
    /* Find the most recently written payload for every deterministic slot
     * and verify its original device offset. */
    for(size_t index=0;index<SAMPLE_PAGES;index++) {
        int touched=0;
        for(size_t i=0;i<ops;i++)if((i*131u+i/7u)%SAMPLE_PAGES==index) {
            touched=1;
        }
        if(!touched)continue;
        const size_t page=(index*7919u)%pages;
        if(nopen_shadow_read(shadow,(uint64_t)page*PAGE,got,PAGE)!=0||
           memcmp(got,expected+index*PAGE,PAGE)!=0) {
            fprintf(stderr,"V080_ASYNC_BENCH_FAILED phase=post_loss page=%zu\n",page);
            goto cleanup;
        }
        verified++;
    }
    (void)seen_start;
    qsort(lat,ops,sizeof(*lat),cmp_double);
    printf("V080_ASYNC_BENCH_PASS backend=%s mode=%s capacity_mib=%zu"
           " operations=%zu verified_pages=%zu gpu_worker_loss=YES"
           " init_s=%.3f write_s=%.3f write_p50_us=%.2f"
           " write_p95_us=%.2f enqueued=%" PRIu64
           " dropped=%" PRIu64 " gpu_completed=%" PRIu64
           " gpu_upload_avg_us=%.2f gpu_upload_max_us=%" PRIu64
           " ssd_write_avg_us=%.2f ssd_write_max_us=%" PRIu64
           " ssd_write_samples=%" PRIu64
           " kernel_swap=NO\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",mode,mib,
           ops,verified,init_s,write_s,lat[ops/2],lat[(ops*95)/100],
           stats.async_enqueued,stats.async_dropped,stats.gpu_writes,
           stats.gpu_writes?
             (double)stats.async_gpu_upload_us_total/(double)stats.gpu_writes:0.0,
           stats.async_gpu_upload_us_max,
           (double)stats.ssd_write_us_total/(double)stats.ssd_write_samples,
           stats.ssd_write_us_max,stats.ssd_write_samples);
    result=0;
cleanup:
    nopen_shadow_close(shadow);
    free(lat);free(expected);
    if(result==0)unlink(disk);
    else fprintf(stderr,"V080_ASYNC_BENCH_FAILED preserved_shadow=%s\n",
                 disk?disk:"(none)");
    return result;
}
