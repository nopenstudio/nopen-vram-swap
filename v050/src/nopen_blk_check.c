#define _POSIX_C_SOURCE 200809L
#include "nopen_blk_backend.h"
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
#define LOGICAL 1024u
static uint64_t rnd(uint64_t *v) {
    *v^=*v<<13;*v^=*v>>7;*v^=*v<<17;return *v;
}
static double seconds(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
}
static int swap_is_safe(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char path[256]={0};
        if(sscanf(line,"%255s",path)==1 && strstr(path,"/nbd")) {
            fclose(f);return 0;
        }
    }
    fclose(f);return 1;
}
int main(int argc,char **argv) {
    nopen_backend mode=0;
    size_t operations=1000;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("Nopen v0.5 checked GPU block transport (NOT block device/swap): "
                 "--backend mock|opencl [--operations 100..2000]");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--backend")) {
            if(!strcmp(val,"mock"))mode=NOPEN_STORE_MOCK;
            else if(!strcmp(val,"opencl"))mode=NOPEN_STORE_OPENCL;
            else return 2;
        } else if(!strcmp(key,"--operations")) {
            errno=0;char *end=NULL;
            unsigned long count=strtoul(val,&end,10);
            if(errno || end==val || *end || count<100 || count>2000)return 2;
            operations=(size_t)count;
        } else return 2;
    }
    if(!mode || geteuid()==0 || !swap_is_safe()) {
        fputs("BLK_CHECK_REFUSED backend/root/active NBD\n",stderr);
        return 3;
    }
    nopen_blk_backend *b=NULL;
    unsigned char *expected=calloc(LOGICAL,PAGE);
    unsigned char *write_page=malloc(PAGE),*read_page=malloc(PAGE);
    if(!expected||!write_page||!read_page)goto failure;
    int rc=nopen_blk_open(&b,mode,64);
    if(rc!=0) {
        fprintf(stderr,"BLOCK_OPEN_FAILED rc=%d backend=%s\n",rc,mode==NOPEN_STORE_OPENCL?"opencl":"mock");
        goto failure;
    }
    uint64_t rng=UINT64_C(0x5fae39b84e029b7d);
    double start=seconds();
    if(start<0)goto failure;
    size_t reads=0,writes=0;
    for(size_t i=0;i<operations;i++) {
        size_t page=(size_t)(rnd(&rng)%LOGICAL);
        int write=(rnd(&rng)&1)!=0;
        if(write) {
            for(size_t j=0;j<PAGE;j++)
                write_page[j]=(unsigned char)(i*17u+page*13u+j);
            if(nopen_blk_write(b,(uint64_t)page*PAGE,write_page,PAGE)!=0)
                goto failure;
            memcpy(expected+page*PAGE,write_page,PAGE);
            writes++;
        } else {
            if(nopen_blk_read(b,(uint64_t)page*PAGE,read_page,PAGE)!=0)
                goto failure;
            if(memcmp(read_page,expected+page*PAGE,PAGE)!=0) {
                fprintf(stderr,"BLK_CHECK_CORRUPTION access=%zu page=%zu\n",i,page);
                goto failure;
            }
            reads++;
        }
    }
    for(size_t page=0;page<LOGICAL;page++) {
        if(nopen_blk_read(b,(uint64_t)page*PAGE,read_page,PAGE)!=0)
            goto failure;
        if(memcmp(read_page,expected+page*PAGE,PAGE)!=0) {
            fprintf(stderr,"BLK_CHECK_CORRUPTION final_page=%zu\n",page);
            goto failure;
        }
    }
    double elapsed=seconds()-start;
    struct rusage usage;
    if(elapsed<=0 || getrusage(RUSAGE_SELF,&usage)!=0)goto failure;
    printf("BLOCK_IO_RESULT version=0.5.0-dev backend=%s "
           "operations=%zu reads=%zu writes=%zu verified_final_pages=%u "
           "elapsed_s=%.6f process_maxrss_kib=%ld "
           "real_kernel_device=NO direct_reclaim=NO no_global_swap=YES\n",
           mode==NOPEN_STORE_OPENCL?"opencl":"mock",
           operations,reads,writes,LOGICAL,elapsed,usage.ru_maxrss);
    puts("BLOCK_IO_PASS checksums=YES verified_final_pages=1024 "
         "kernel_block_adapter_code=YES kernel_block_device_created=NO "
         "not_system_swap=YES");
    nopen_blk_close(b);free(expected);free(write_page);free(read_page);return 0;
failure:
    fputs("BLK_CHECK_FAILED no_performance_claim=YES\n",stderr);
    nopen_blk_close(b);free(expected);free(write_page);free(read_page);return 5;
}
