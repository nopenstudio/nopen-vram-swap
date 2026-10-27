#define _POSIX_C_SOURCE 200809L
#include "nopen_memory.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

/* v0.4: compare the same logical 4KiB pages held in fully touched CPU RAM
 * versus an explicit cooperative OpenCL VRAM store with limited resident CPU
 * pages. This is NOT transparent system swap, a real game's memory, or proof
 * of whole-system RAM savings (driver/kernel allocations not in process RSS).
 * Original NBD/global SWAP is prohibited; never require root. */
#define MIB (1024u*1024u)
#define BATCH 64u
static double secs(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1.0;
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int num(const char *s,size_t *out) {
    if(!s||!*s||*s=='-'||*s=='+')return -1;
    errno=0;char *end=NULL;unsigned long long u=strtoull(s,&end,10);
    if(errno || !end || *end || u==0 || u>SIZE_MAX)return -1;
    *out=(size_t)u;return 0;
}
static int safe_swap(void) {
    FILE *f=fopen("/proc/swaps","r");char line[512];
    if(!f)return 0;
    if(!fgets(line,sizeof(line),f)){fclose(f);return 0;}
    while(fgets(line,sizeof(line),f)) {
        char path[256]={0};
        if(sscanf(line,"%255s",path)==1 && strncmp(path,"/dev/nbd",8)==0) {
            fclose(f);return 0;
        }
    }
    fclose(f);return 1;
}
static int enough_available(size_t bytes) {
    FILE *f=fopen("/proc/meminfo","r");
    if(!f)return 0;
    char line[256];unsigned long long kib=0;
    while(fgets(line,sizeof line,f))
        if(sscanf(line,"MemAvailable: %llu kB",&kib)==1)break;
    fclose(f);
    /* Conservative baseline: always leave at least 2 GiB unallocated.
     * This is only a guard, not an OOM-proof admission controller. */
    if(kib==0)return 0;
    const unsigned long long need=(unsigned long long)(bytes/1024u)+2ull*1024u*1024u;
    return kib>need;
}
static void pattern(unsigned char *dest,size_t page) {
    uint64_t state=UINT64_C(0x9e3779b97f4a7c15)^(uint64_t)page;
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {
        state^=state<<13;state^=state>>7;state^=state<<17;
        dest[i]=(unsigned char)(state>>23);
    }
}
static void report(const char *backend,size_t logical,size_t ram,size_t pf,
                   double init,double verify,uint64_t checked,
                   const nopen_memory_stats *stats) {
    struct rusage usage;
    if(getrusage(RUSAGE_SELF,&usage)!=0) {
        fputs("RUSAGE_UNAVAILABLE\n",stderr);return;
    }
    printf("ECONOMICS_RESULT version=0.4.0-dev backend=%s logical_mib=%zu "
           "logical_pages=%zu resident_limit_pages=%zu prefetch_pages=%zu "
           "init_s=%.6f verify_s=%.6f verified_pages=%"PRIu64" "
           "process_maxrss_kib=%ld page_ins=%"PRIu64" "
           "page_outs=%"PRIu64" prefetch_hits=%"PRIu64" "
           "prefetch_wasted=%"PRIu64" no_global_swap=YES "
           "scope=explicit_cooperative_process_only gpu_vram_excluded_from_rss=YES\n",
           backend,logical,logical*256u,ram,pf,init,verify,checked,usage.ru_maxrss,
           stats?stats->page_ins:0,stats?stats->page_outs:0,
           stats?stats->prefetch_hits:0,stats?stats->prefetch_wasted:0);
    fflush(stdout);
}
int main(int argc,char **argv) {
    enum {MODE_NONE,MODE_CPU,MODE_OPENCL,MODE_MOCK} mode=MODE_NONE;
    size_t logical=64,ram=32,prefetch=16;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("v0.4 memory economics (NOT system SWAP): --backend cpu|opencl|mock "
                 "--logical-mib 4|64|256|1024 --ram-pages 16..1024 "
                 "--prefetch-pages 0|4|16");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--backend")) {
            if(!strcmp(val,"cpu"))mode=MODE_CPU;
            else if(!strcmp(val,"opencl"))mode=MODE_OPENCL;
            else if(!strcmp(val,"mock"))mode=MODE_MOCK;
            else return 2;
        } else if(!strcmp(key,"--prefetch-pages")&&!strcmp(val,"0")) {
            prefetch=0;
        } else {
            size_t *dest=NULL;
            if(!strcmp(key,"--logical-mib"))dest=&logical;
            else if(!strcmp(key,"--ram-pages"))dest=&ram;
            else if(!strcmp(key,"--prefetch-pages"))dest=&prefetch;
            if(!dest||num(val,dest)!=0)return 2;
        }
    }
    if(mode==MODE_NONE || !(logical==4 || logical==64 || logical==256 || logical==1024) ||
       ram<16 || ram>1024 || ram>=logical*256u ||
       !(prefetch==0 || prefetch==4 || prefetch==16)) {
        fputs("INVALID_ARGS\n",stderr);return 2;
    }
    if(geteuid()==0 || !safe_swap()) {
        fputs("REFUSED root or active NBD/unreadable swaps\n",stderr);return 3;
    }
    const size_t count=logical*256u;
    const size_t bytes=count*NOPEN_PAGE_SIZE;
    if(!enough_available(bytes)) {
        fputs("REFUSED insufficient MemAvailable headroom for safe CPU comparison\n",stderr);
        return 3;
    }
    unsigned char *batch=malloc(BATCH*NOPEN_PAGE_SIZE);
    unsigned char *expect=malloc(NOPEN_PAGE_SIZE);
    unsigned char *actual=malloc(NOPEN_PAGE_SIZE);
    unsigned char *memory=NULL;
    nopen_store *store=NULL;
    nopen_memory *m=NULL;
    if(!batch||!expect||!actual) {
        fputs("ALLOCATION_FAILED staging\n",stderr);
        free(batch);free(expect);free(actual);return 4;
    }
    int status=4;
    if(mode==MODE_CPU) {
        memory=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(memory==MAP_FAILED){memory=NULL;goto finish;}
    } else {
        nopen_backend backend=(mode==MODE_MOCK)?NOPEN_STORE_MOCK:NOPEN_STORE_OPENCL;
        if(nopen_store_open(&store,backend,logical)!=0)goto finish;
        if(nopen_memory_open(&m,store,count,ram)!=0)goto finish;
    }
    double before=secs();
    if(before<0)goto finish;
    for(size_t p=0;p<count;p+=BATCH) {
        const size_t n=count-p<BATCH?count-p:BATCH;
        for(size_t j=0;j<n;j++)pattern(batch+j*NOPEN_PAGE_SIZE,p+j);
        if(mode==MODE_CPU) {
            memcpy(memory+p*NOPEN_PAGE_SIZE,batch,n*NOPEN_PAGE_SIZE);
        } else if(nopen_memory_import_pages(m,p,batch,n)!=0) {
            fprintf(stderr,"IMPORT_FAILED first=%zu %s\n",p,nopen_memory_last_error(m));
            goto finish;
        }
    }
    double filled=secs();
    if(filled<=before)goto finish;
    uint64_t verified=0;
    for(size_t p=0;p<count;p++) {
        if(mode==MODE_CPU) {
            memcpy(actual,memory+p*NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE);
        } else {
            if(prefetch && p%prefetch==0) {
                size_t n=count-p;
                if(n>prefetch)n=prefetch;
                if(nopen_memory_prefetch(m,p,n)!=0) {
                    fprintf(stderr,"PREFETCH_FAILED page=%zu %s\n",p,nopen_memory_last_error(m));
                    goto finish;
                }
            }
            if(nopen_memory_read(m,p,actual)!=0) {
                fprintf(stderr,"RESTORE_FAILED page=%zu %s\n",p,nopen_memory_last_error(m));
                goto finish;
            }
        }
        pattern(expect,p);
        if(memcmp(expect,actual,NOPEN_PAGE_SIZE)!=0) {
            fprintf(stderr,"CORRUPTION page=%zu\n",p);goto finish;
        }
        verified++;
    }
    double done=secs();
    if(done<=filled || verified!=count)goto finish;
    nopen_memory_stats stats={0};
    if(m) {
        nopen_memory_get_stats(m,&stats);
        if(stats.page_ins!=count || stats.prefetch_wasted!=0 ||
           stats.prefetch_failures!=0 || stats.resident_pages>ram ||
           stats.zero_pages!=0 || stats.prefetch_hits!=(prefetch?count:0) ||
           stats.demand_page_ins!=(prefetch?0:count)) {
            fprintf(stderr,"ACCOUNTING_FAILED ins=%"PRIu64" demand=%"PRIu64
                           " pfhits=%"PRIu64"\n",
                    stats.page_ins,stats.demand_page_ins,stats.prefetch_hits);
            goto finish;
        }
    }
    report(mode==MODE_CPU?"cpu":mode==MODE_MOCK?"mock":"opencl",
           logical,ram,prefetch,filled-before,done-filled,verified,
           m?&stats:NULL);
    printf("ECONOMICS_PASS version=0.4.0-dev backend=%s verified_pages=%"PRIu64
           " logical_mib=%zu no_global_swap=YES\n",
           mode==MODE_CPU?"cpu":mode==MODE_MOCK?"mock":"opencl",verified,logical);
    status=0;
finish:
    if(status)fprintf(stderr,"ECONOMICS_FAILED code=%d\n",status);
    if(memory)munmap(memory,bytes);
    nopen_memory_close(m);nopen_store_close(store);
    free(batch);free(expect);free(actual);
    return status;
}
