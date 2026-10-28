#define _POSIX_C_SOURCE 200809L
#include "nopen_recovery.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define PAGE NOPEN_PAGE_SIZE
#define ARENA_PAGES 1024u
static uint64_t next(uint64_t *v) {
    *v^=*v<<13;*v^=*v>>7;*v^=*v<<17;return *v;
}
static double now(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int safe_swaps(void) {
    FILE *f=fopen("/proc/swaps","r");
    char line[512];
    if(!f)return 0;
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char dev[256]={0};
        if(sscanf(line,"%255s",dev)==1 && strncmp(dev,"/dev/nbd",8)==0) {
            fclose(f);return 0;
        }
    }
    int rc=ferror(f);
    fclose(f);
    return rc==0;
}
int main(int argc,char **argv) {
    nopen_backend backend=0;
    nopen_recovery_mode mode=NOPEN_RECOVERY_NO_MIRROR;
    size_t operations=1000,inject_after=500;
    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i],"--help")==0) {
            puts("Nopen v0.6 GPU loss simulation (NOT an actual GPU reset): "
                 "--backend mock|opencl --mode mirror|failclosed "
                 "--operations 100..20000 --inject-loss-after 1..20000");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*v=argv[i];
        if(strcmp(key,"--backend")==0) {
            if(strcmp(v,"mock")==0)backend=NOPEN_STORE_MOCK;
            else if(strcmp(v,"opencl")==0)backend=NOPEN_STORE_OPENCL;
            else return 2;
        } else if(strcmp(key,"--mode")==0) {
            if(strcmp(v,"mirror")==0)mode=NOPEN_RECOVERY_HOST_MIRROR;
            else if(strcmp(v,"failclosed")==0)mode=NOPEN_RECOVERY_NO_MIRROR;
            else return 2;
        } else {
            size_t *value=NULL;
            if(strcmp(key,"--operations")==0)value=&operations;
            else if(strcmp(key,"--inject-loss-after")==0)value=&inject_after;
            if(!value || !*v || *v=='-' || *v=='+')return 2;
            errno=0;char *end=NULL;unsigned long n=strtoul(v,&end,10);
            if(errno||end==v||*end||n<1||n>20000)return 2;
            *value=(size_t)n;
        }
    }
    if(!backend || operations<100 || operations>20000 ||
       inject_after==0 || inject_after>=operations || geteuid()==0 ||
       !safe_swaps()) {
        fputs("RECOVERY_LAB_REFUSED invalid backend/options, root or active NBD swap\n",stderr);
        return 2;
    }
    unsigned char *expected=calloc(ARENA_PAGES,PAGE);
    unsigned char *write_page=malloc(PAGE),*read_page=malloc(PAGE);
    nopen_recovery *r=NULL;
    int result=4;
    if(!expected || !write_page || !read_page)goto done;
    if(nopen_recovery_open_bounded(&r,backend,4,mode,16u*1024u*1024u)!=0)goto done;
    uint64_t seed=UINT64_C(0x6f1a22d06b439d71);
    size_t successful=0,denied=0,reads=0,writes=0;
    double start=now();
    if(start<0)goto done;
    for(size_t i=0;i<operations;i++) {
        if(i==inject_after) {
            const int rc=nopen_recovery_device_lost(r);
            if(mode==NOPEN_RECOVERY_NO_MIRROR && rc!=-EIO)goto done;
            if(mode==NOPEN_RECOVERY_HOST_MIRROR && rc!=0)goto done;
        }
        const size_t page=(size_t)(next(&seed)%ARENA_PAGES);
        const int writing=(next(&seed)&1u)!=0;
        if(writing) {
            for(size_t j=0;j<PAGE;j++)
                write_page[j]=(unsigned char)(i*19u + page*3u + j*5u);
            const int rc=nopen_recovery_write(r,(uint64_t)page*PAGE,write_page,PAGE);
            if(mode==NOPEN_RECOVERY_NO_MIRROR && i>=inject_after) {
                if(rc!=-EIO)goto done;
                denied++;
            } else {
                if(rc!=0)goto done;
                memcpy(expected+page*PAGE,write_page,PAGE);successful++;writes++;
            }
        } else {
            memset(read_page,0xA9,PAGE);
            const int rc=nopen_recovery_read(r,(uint64_t)page*PAGE,read_page,PAGE);
            if(mode==NOPEN_RECOVERY_NO_MIRROR && i>=inject_after) {
                if(rc!=-EIO)goto done;
                for(size_t j=0;j<PAGE;j++)
                    if(read_page[j]!=0xA9)goto done;
                denied++;
            } else {
                if(rc!=0 || memcmp(read_page,expected+page*PAGE,PAGE)!=0)goto done;
                successful++;reads++;
            }
        }
    }
    if(mode==NOPEN_RECOVERY_HOST_MIRROR) {
        for(size_t p=0;p<ARENA_PAGES;p++) {
            if(nopen_recovery_read(r,(uint64_t)p*PAGE,read_page,PAGE)!=0 ||
               memcmp(read_page,expected+p*PAGE,PAGE)!=0) goto done;
        }
    }
    nopen_recovery_stats stats;
    if(nopen_recovery_get_stats(r,&stats)!=0 ||
       stats.device_loss_events!=1 ||
       stats.state!=(mode==NOPEN_RECOVERY_NO_MIRROR?
            NOPEN_RECOVERY_FAILED_CLOSED:NOPEN_RECOVERY_CPU_FALLBACK) ||
       (mode==NOPEN_RECOVERY_NO_MIRROR && denied!=operations-inject_after))
        goto done;
    struct rusage usage;
    const double elapsed=now()-start;
    if(elapsed<=0 || getrusage(RUSAGE_SELF,&usage)!=0)goto done;
    printf("RECOVERY_RESULT version=0.6.0-dev backend=%s mode=%s operations=%zu "
           "fault_injection=simulated_notification_ONLY real_gpu_reset=NO "
           "successful=%zu denied=%zu reads=%zu writes=%zu "
           "verified_final_pages=%u mirror_bytes=%"PRIu64" "
           "fallback_reads=%"PRIu64" fallback_writes=%"PRIu64" "
           "elapsed_s=%.6f process_maxrss_kib=%ld no_global_swap=YES\n",
           backend==NOPEN_STORE_MOCK?"mock":"opencl",
           mode==NOPEN_RECOVERY_HOST_MIRROR?"mirror":"failclosed",
           operations,successful,denied,reads,writes,
           mode==NOPEN_RECOVERY_HOST_MIRROR?ARENA_PAGES:0u,
           stats.mirror_bytes,stats.fallback_reads,stats.fallback_writes,
           elapsed,usage.ru_maxrss);
    puts("RECOVERY_LAB_PASS gpu_loss_simulated=YES "
         "detected_data_corruption=NO actual_gpu_reset_tested=NO "
         "no_global_swap=YES kernel_block_device=NO");
    result=0;
done:
    if(result!=0)fputs("RECOVERY_LAB_FAILED no_performance_or_reset_claim=YES\n",stderr);
    nopen_recovery_close(r);
    free(expected);free(write_page);free(read_page);
    return result;
}
