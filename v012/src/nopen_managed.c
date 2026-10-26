#include "nopen_memory.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MIB (1024u*1024u)
static double now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec*1000.0+(double)t.tv_nsec/1e6;
}
static void fill(unsigned char *buf,size_t page,unsigned round) {
    uint64_t h=((uint64_t)page+1)*UINT64_C(0x9E3779B97F4A7C15) ^
               ((uint64_t)round+1)*UINT64_C(0xD6E8FEB86659FD93);
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {
        h^=h>>12;h^=h<<25;h^=h>>27;
        buf[i]=(unsigned char)((h*UINT64_C(0x2545F4914F6CDD1D))>>56);
    }
}
static int number(const char*s,size_t*out) {
    if(!s||!*s||*s=='-'||*s=='+')return -1;
    errno=0;char*end=NULL;unsigned long long n=strtoull(s,&end,10);
    if(errno||end==s||*end||n==0||n>SIZE_MAX)return -1;
    *out=(size_t)n;return 0;
}
static void usage(void) {
    puts("Nopen Memory Fabric v0.12.0-alpha.2 — cooperative managed VRAM pages (NOT system SWAP)");
    puts("--backend mock|opencl --pool-mib 64 --logical-pages 1024 --ram-pages 32 --rounds 3");
    puts("GPU pages and resident RAM pages are bounded; explicit fail-closed data verification.");
    puts("Never enables NBD, Linux SWAP, privileged services, or extra Bannerlord RAM.");
}
static int unsafe_swap(void) {
    FILE*f=fopen("/proc/swaps","r");if(!f)return 1;
    char line[512];
    if(!fgets(line,sizeof(line),f)){fclose(f);return 1;}
    while(fgets(line,sizeof(line),f)) {
        char path[384]={0};
        if(sscanf(line,"%383s",path)==1&&strstr(path,"/nbd")) {
            fprintf(stderr,"REFUSED: active NBD swap detected: %s\n",path);
            fclose(f);return 1;
        }
    }
    fclose(f);return 0;
}
int main(int argc,char **argv) {
    nopen_backend backend=NOPEN_STORE_MOCK;
    size_t pool_mib=64,logical_pages=1024,ram_pages=32,rounds=3;
    for(int i=1;i<argc;i++) {
        const char*a=argv[i];
        if(!strcmp(a,"--help")||!strcmp(a,"-h")){usage();return 0;}
        if(i+1>=argc){usage();return 2;}
        const char*v=argv[++i];
        if(!strcmp(a,"--backend")) {
            if(!strcmp(v,"opencl"))backend=NOPEN_STORE_OPENCL;
            else if(!strcmp(v,"mock"))backend=NOPEN_STORE_MOCK;
            else {usage();return 2;}
        } else {
            size_t *dest=NULL;
            if(!strcmp(a,"--pool-mib"))dest=&pool_mib;
            else if(!strcmp(a,"--logical-pages"))dest=&logical_pages;
            else if(!strcmp(a,"--ram-pages"))dest=&ram_pages;
            else if(!strcmp(a,"--rounds"))dest=&rounds;
            if(!dest||number(v,dest)!=0){usage();return 2;}
        }
    }
    if(pool_mib>NOPEN_MAX_MIB||rounds>20||ram_pages>logical_pages||
       logical_pages>pool_mib*(MIB/NOPEN_PAGE_SIZE)||logical_pages>1048576u) {
        usage();return 2;
    }
    if(unsafe_swap())return 4;
    nopen_store *store=NULL;
    if(nopen_store_open(&store,backend,pool_mib)!=0){fprintf(stderr,"GPU store init failed\n");return 4;}
    nopen_memory *m=NULL;
    if(nopen_memory_open(&m,store,logical_pages,ram_pages)!=0){fprintf(stderr,"Memory manager init failed\n");nopen_store_close(store);return 4;}
    unsigned char *want=malloc(NOPEN_PAGE_SIZE),*got=malloc(NOPEN_PAGE_SIZE);
    if(!want||!got){fprintf(stderr,"Scratch allocation failed\n");goto fail;}
    double begin=now_ms();
    for(size_t r=0;r<rounds;r++) {
        for(size_t p=0;p<logical_pages;p++) {
            fill(want,p,(unsigned)r);
            if(nopen_memory_write(m,p,want)!=0){fprintf(stderr,"FAIL_WRITE: %s\n",nopen_memory_last_error(m));goto fail;}
        }
        if(nopen_memory_trim(m,0)!=0){fprintf(stderr,"FAIL_TRIM: %s\n",nopen_memory_last_error(m));goto fail;}
        /* Reverse traversal forces actual GPU -> CPU page restoration. */
        for(size_t p=logical_pages;p>0;) {
            p--;fill(want,p,(unsigned)r);
            if(nopen_memory_read(m,p,got)!=0){fprintf(stderr,"FAIL_READ: %s\n",nopen_memory_last_error(m));goto fail;}
            if(memcmp(want,got,NOPEN_PAGE_SIZE)!=0){fprintf(stderr,"FAIL_CORRUPTION: logical page=%zu round=%zu\n",p,r);goto fail;}
        }
        if(nopen_memory_trim(m,0)!=0){fprintf(stderr,"FAIL_TRIM: %s\n",nopen_memory_last_error(m));goto fail;}
        nopen_memory_stats st;nopen_memory_get_stats(m,&st);
        printf("ROUND_PASS round=%zu logical_pages=%zu resident=%zu vram_pages=%zu pageouts=%"PRIu64" pageins=%"PRIu64"\n",
               r+1,logical_pages,st.resident_pages,st.vram_pages,st.page_outs,st.page_ins);
    }
    {
        nopen_memory_stats st;nopen_memory_get_stats(m,&st);
        double elapsed=(now_ms()-begin)/1000.0;
        printf("MANAGED_PASS version=0.12.0-alpha.2 backend=%s logical_pages=%zu cpu_resident_limit=%zu pool_mib=%zu rounds=%zu "
               "verified_mib=%.2f seconds=%.4f pageouts=%"PRIu64" pageins=%"PRIu64" corruptions=%"PRIu64" "
               "read_failures=%"PRIu64" write_failures=%"PRIu64" no_global_swap=YES\n",
               backend==NOPEN_STORE_OPENCL?"opencl":"mock",logical_pages,ram_pages,pool_mib,rounds,
               (double)logical_pages*(double)rounds*(double)NOPEN_PAGE_SIZE/MIB,elapsed,
               st.page_outs,st.page_ins,st.checksum_errors,st.failed_reads,st.failed_writes);
    }
    free(want);free(got);nopen_memory_close(m);nopen_store_close(store);return 0;
fail:
    free(want);free(got);nopen_memory_close(m);nopen_store_close(store);return 5;
}