#define _POSIX_C_SOURCE 200809L
#include "nopen_memory.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

/* Controlled single-threaded 4KiB page-access workload.
 * Not userfaultfd, kernel swap, a real game's workload, or an NVMe comparison.
 * This workload is sequential to expose useful adjacent-page prefetch. */
static double seconds(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1.0;
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int number(const char *s,size_t *out) {
    if(!s||!*s||*s=='-'||*s=='+')return -1;
    errno=0;char *end=NULL;
    unsigned long long x=strtoull(s,&end,10);
    if(errno||!end||*end||x==0||x>SIZE_MAX)return -1;
    *out=(size_t)x;return 0;
}
static int safe_swap(void) {
    FILE *f=fopen("/proc/swaps","r");char line[512];
    if(!f)return 0;
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char path[256]={0};
        if(sscanf(line,"%255s",path)==1 && strncmp(path,"/dev/nbd",8)==0) {
            fclose(f);return 0;
        }
    }
    fclose(f);return 1;
}
static void pattern(unsigned char *p,size_t page) {
    uint64_t x=UINT64_C(0x9e3779b97f4a7c15)^(uint64_t)page;
    for(size_t j=0;j<NOPEN_PAGE_SIZE;j++) {
        x^=x<<13;x^=x>>7;x^=x<<17;
        p[j]=(unsigned char)(x>>23);
    }
}
int main(int argc,char **argv) {
    nopen_backend backend=0;
    size_t pool=4,logical=512,ram=32,prefetch=0,passes=2;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("v0.3 cooperative GPU page test (NOT swap): --backend mock|opencl --pool-mib 4..256 --logical-pages N --ram-pages N --prefetch-pages 0..16 --passes 1..5");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--backend")) {
            if(!strcmp(val,"mock"))backend=NOPEN_STORE_MOCK;
            else if(!strcmp(val,"opencl"))backend=NOPEN_STORE_OPENCL;
            else return 2;
        } else if(!strcmp(key,"--prefetch-pages")) {
            /* Zero explicitly disables prefetch, for a comparable baseline. */
            if(!strcmp(val,"0"))prefetch=0;
            else if(number(val,&prefetch)!=0)return 2;
        } else {
            size_t *dest=NULL;
            if(!strcmp(key,"--pool-mib"))dest=&pool;
            else if(!strcmp(key,"--logical-pages"))dest=&logical;
            else if(!strcmp(key,"--ram-pages"))dest=&ram;
            else if(!strcmp(key,"--passes"))dest=&passes;
            if(!dest||number(val,dest))return 2;
        }
    }
    if(!backend||pool<1||pool>256||logical<=ram||logical>pool*256 ||
       ram<1||prefetch>16||prefetch>ram||passes>5) {
        fputs("INVALID_ARGS\n",stderr);return 2;
    }
    if(!safe_swap()) {fputs("REFUSED active NBD or unreadable /proc/swaps\n",stderr);return 3;}
    nopen_store *store=NULL;
    nopen_memory *m=NULL;
    unsigned char *expected=malloc(NOPEN_PAGE_SIZE),*actual=malloc(NOPEN_PAGE_SIZE);
    int status=4;
    if(!expected||!actual)goto out;
    if(nopen_store_open(&store,backend,pool)!=0)goto out;
    if(nopen_memory_open(&m,store,logical,ram)!=0)goto out;
    /* All pages have distinct reproducible patterns. */
    for(size_t page=0;page<logical;page++) {
        pattern(expected,page);
        if(nopen_memory_write(m,page,expected)!=0)goto out;
    }
    if(nopen_memory_trim(m,0)!=0)goto out;
    double begin=seconds();
    if(begin<0)goto out;
    for(size_t pass=0;pass<passes;pass++) {
        for(size_t p=0;p<logical;p++) {
            if(prefetch && p%prefetch==0) {
                size_t count=logical-p;
                if(count>prefetch)count=prefetch;
                if(nopen_memory_prefetch(m,p,count)!=0) {
                    fprintf(stderr,"PREFETCH_ERROR %s\n",nopen_memory_last_error(m));
                    goto out;
                }
            }
            if(nopen_memory_read(m,p,actual)!=0)goto out;
            pattern(expected,p);
            if(memcmp(actual,expected,NOPEN_PAGE_SIZE)!=0) {
                fprintf(stderr,"INTEGRITY_FAIL page=%zu\n",p);
                goto out;
            }
        }
    }
    double elapsed=seconds()-begin;
    nopen_memory_stats stats;
    nopen_memory_get_stats(m,&stats);
    struct rusage usage;
    if(elapsed<=0||getrusage(RUSAGE_SELF,&usage)!=0 ||
       stats.prefetch_wasted!=0||stats.prefetch_failures!=0 ||
       stats.resident_pages>ram || stats.prefetch_hits!=(prefetch?logical*passes:0) ||
       stats.demand_page_ins!=(prefetch?0:logical*passes) ||
       stats.prefetch_pages!=(prefetch?logical*passes:0))goto out;
    printf("PAGER_RESULT version=0.3.0-dev backend=%s mode=%s logical_pages=%zu "
           "ram_pages=%zu passes=%zu prefetch_pages=%zu elapsed_s=%.6f "
           "verified_pages=%zu demand_page_ins=%"PRIu64
           " prefetch_batches=%"PRIu64" prefetch_reads=%"PRIu64
           " prefetch_hits=%"PRIu64" prefetch_wasted=%"PRIu64
           " page_outs=%"PRIu64" process_maxrss_kib=%ld no_global_swap=YES\n",
           backend==NOPEN_STORE_MOCK?"mock":"opencl",prefetch?"prefetch":"demand",
           logical,ram,passes,prefetch,elapsed,logical*passes,
           stats.demand_page_ins,stats.prefetch_batches,stats.prefetch_pages,
           stats.prefetch_hits,stats.prefetch_wasted,stats.page_outs,usage.ru_maxrss);
    printf("PAGER_PASS verified_pages=%zu no_global_swap=YES backend=%s\n",
           logical*passes,backend==NOPEN_STORE_MOCK?"mock":"opencl");
    status=0;
out:
    if(status!=0)fprintf(stderr,"PAGER_FAILED code=%d\n",status);
    nopen_memory_close(m);nopen_store_close(store);free(expected);free(actual);
    return status;
}
