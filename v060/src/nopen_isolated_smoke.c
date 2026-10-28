#define _POSIX_C_SOURCE 200809L
#include "nopen_isolated_io.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define P NOPEN_PAGE_SIZE
#define N 1024u
static uint64_t step(uint64_t *v) {
    *v^=*v<<13;*v^=*v>>7;*v^=*v<<17;return *v;
}
static double secs(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1.0;
    return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
}
static int swaps_ok(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char path[256]={0};
        if(sscanf(line,"%255s",path)==1 && strncmp(path,"/dev/nbd",8)==0) {
            fclose(f);return 0;
        }
    }
    int err=ferror(f);fclose(f);
    return err==0;
}
int main(int argc,char **argv) {
    const char *worker=NULL;
    nopen_backend backend=0;
    size_t ops=1000;
    unsigned deadline=2000;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("Nopen v0.6 isolated GPU process test (NOT kernel swap): "
                 "--worker /absolute/path/nopen-isolated-worker --backend mock|opencl "
                 "--operations 100..2000 --timeout-ms 50..5000");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--worker"))worker=val;
        else if(!strcmp(key,"--backend")) {
            if(!strcmp(val,"mock"))backend=NOPEN_STORE_MOCK;
            else if(!strcmp(val,"opencl"))backend=NOPEN_STORE_OPENCL;
            else return 2;
        } else {
            char *end=NULL;errno=0;
            unsigned long x=strtoul(val,&end,10);
            if(errno||end==val||*end)return 2;
            if(!strcmp(key,"--operations"))ops=(size_t)x;
            else if(!strcmp(key,"--timeout-ms"))deadline=(unsigned)x;
            else return 2;
        }
    }
    if(!worker||worker[0]!='/'||!backend||ops<100||ops>2000||
       deadline<50||deadline>5000||geteuid()==0||!swaps_ok()) {
        fputs("ISOLATED_SMOKE_REFUSED invalid params, root, or active NBD\n",stderr);
        return 2;
    }
    unsigned char *reference=calloc(N,P);
    unsigned char *source=malloc(P),*dest=malloc(P);
    nopen_isolated_io *ctx=NULL;
    int status=4;
    if(!reference || !source || !dest)goto out;
    if(nopen_isolated_open(&ctx,backend,4,deadline,worker)!=0)goto out;
    uint64_t rng=UINT64_C(0xfa89392e592612cb);
    const double start=secs();
    if(start<0)goto out;
    size_t writes=0,reads=0;
    for(size_t i=0;i<ops;i++) {
        size_t page=(size_t)(step(&rng)%N);
        if(step(&rng)&1u) {
            for(size_t j=0;j<P;j++)
                source[j]=(unsigned char)(i*13u+page*19u+j*3u);
            if(nopen_isolated_write(ctx,(uint64_t)page*P,source,P)!=0)goto out;
            memcpy(reference+page*P,source,P);writes++;
        } else {
            memset(dest,0xD7,P);
            if(nopen_isolated_read(ctx,(uint64_t)page*P,dest,P)!=0 ||
               memcmp(dest,reference+page*P,P)!=0)goto out;
            reads++;
        }
    }
    for(size_t p=0;p<N;p++) {
        if(nopen_isolated_read(ctx,(uint64_t)p*P,dest,P)!=0 ||
           memcmp(dest,reference+p*P,P)!=0)goto out;
    }
    nopen_isolated_stats info;
    struct rusage usage;
    const double total=secs()-start;
    if(total<=0 || nopen_isolated_stats_get(ctx,&info)!=0 ||
       getrusage(RUSAGE_SELF,&usage)!=0 ||
       info.timeouts!=0 || info.io_errors!=0 || !info.child_alive)
        goto out;
    printf("ISOLATED_GPU_RESULT version=0.6.0-dev backend=%s "
           "operations=%zu reads=%zu writes=%zu final_verified_pages=%u "
           "elapsed_s=%.6f process_maxrss_kib=%ld "
           "io_timeouts=%"PRIu64" child_crashes=%"PRIu64" "
           "worker_process=YES no_global_swap=YES\n",
           backend==NOPEN_STORE_MOCK?"mock":"opencl",ops,reads,writes,N,
           total,usage.ru_maxrss,info.timeouts,info.child_crashes);
    puts("ISOLATED_GPU_PASS checked_ipc=YES final_pages_verified=1024 "
         "gpu_reset_tested=NO kernel_swap=NO no_global_swap=YES");
    status=0;
out:
    if(status)fputs("ISOLATED_GPU_FAILED no_performance_claim=YES\n",stderr);
    nopen_isolated_close(ctx);
    free(reference);free(source);free(dest);
    return status;
}
