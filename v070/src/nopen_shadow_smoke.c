#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
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
#define MIB (1024u*1024u)
static uint64_t next(uint64_t *v) {
    *v^=*v<<13; *v^=*v>>7; *v^=*v<<17; return *v;
}
static double now(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0)return -1.0;
    return (double)ts.tv_sec+(double)ts.tv_nsec/1.0e9;
}
static int not_active_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof(line),f)){fclose(f);return 0;}
    while(fgets(line,sizeof(line),f)) {
        char name[256]={0};
        if(sscanf(line,"%255s",name)==1 &&
           (!strncmp(name,"/dev/ublkb",11) || !strncmp(name,"/dev/nbd",8))) {
            fclose(f);return 0;
        }
    }
    const int bad=ferror(f);
    fclose(f);
    return !bad;
}
int main(int argc,char **argv) {
    nopen_backend backend=0;
    const char *worker=NULL,*file=NULL;
    size_t ops=1000,mib=4,loss_after=500;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("Nopen v0.7 read/write + forced GPU worker loss (NO kernel swap): "
                 "--worker /abs/worker --backend mock|opencl "
                 "--shadow-file /abs/nvme-file --capacity-mib 4..256 "
                 "--operations 100..10000 --loss-after 1..9999");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--worker"))worker=val;
        else if(!strcmp(key,"--backend")) {
            if(!strcmp(val,"mock"))backend=NOPEN_STORE_MOCK;
            else if(!strcmp(val,"opencl"))backend=NOPEN_STORE_OPENCL;
            else return 2;
        } else if(!strcmp(key,"--shadow-file"))file=val;
        else {
            if(val[0]=='-'||val[0]=='+')return 2;
            char *end=NULL;errno=0;
            unsigned long v=strtoul(val,&end,10);
            if(errno||end==val||*end)return 2;
            if(!strcmp(key,"--capacity-mib"))mib=(size_t)v;
            else if(!strcmp(key,"--operations"))ops=(size_t)v;
            else if(!strcmp(key,"--loss-after"))loss_after=(size_t)v;
            else return 2;
        }
    }
    if(geteuid()==0 || !not_active_swap() ||
       !worker || worker[0]!='/' || !file || file[0]!='/' ||
       !backend || mib<4||mib>256||ops<100||ops>10000 ||
       !loss_after||loss_after>=ops) {
        fputs("V070_SHADOW_HARDWARE_REFUSED root, swap activity, or invalid flags\n",
              stderr);
        return 2;
    }
    size_t total_pages=mib*(MIB/P);
    unsigned char *known=calloc(total_pages,P);
    unsigned char *page=malloc(P),*got=malloc(P);
    nopen_shadow *s=NULL;
    int status=4;
    if(!known||!page||!got)goto done;
    const int rc=nopen_shadow_open(&s,backend,mib,2000,worker,file);
    if(rc!=0) {
        fprintf(stderr,"V070_SHADOW_HARDWARE_OPEN_FAILED rc=%d path=%s\n",rc,file);
        goto done;
    }
    uint64_t seed=UINT64_C(0x8749d291dab53120);
    size_t writes=0,reads=0,verified=0;
    double begin=now();
    if(begin<0)goto done;
    for(size_t i=0;i<ops;i++) {
        if(i==loss_after) {
            if(nopen_shadow_simulate_gpu_loss(s)!=0)goto done;
        }
        size_t index=(size_t)(next(&seed)%total_pages);
        if(next(&seed)&1u) {
            for(size_t x=0;x<P;x++)
                page[x]=(unsigned char)(i*31u+index*7u+x);
            if(nopen_shadow_write(s,(uint64_t)index*P,page,P)!=0)
                goto done;
            memcpy(known+index*P,page,P);
            writes++;
        } else {
            memset(got,0xA5,P);
            if(nopen_shadow_read(s,(uint64_t)index*P,got,P)!=0 ||
               memcmp(got,known+index*P,P)!=0)goto done;
            reads++;
        }
    }
    for(size_t page_id=0;page_id<total_pages;page_id++) {
        if(nopen_shadow_read(s,(uint64_t)page_id*P,got,P)!=0 ||
           memcmp(got,known+page_id*P,P)!=0) {
            fprintf(stderr,"V070_SHADOW_CORRUPTION page=%zu\n",page_id);
            goto done;
        }
        verified++;
    }
    nopen_shadow_stats stats={0};
    struct rusage rss={0};
    const double elapsed=now()-begin;
    if(elapsed<=0||nopen_shadow_get_stats(s,&stats)!=0||
       getrusage(RUSAGE_SELF,&rss)!=0 || stats.gpu_active ||
       stats.gpu_failures!=1 || stats.poisoned || verified!=total_pages)
        goto done;
    printf("V070_SHADOW_HARDWARE_RESULT backend=%s capacity_mib=%zu"
           " operations=%zu writes=%zu reads=%zu verified_pages=%zu"
           " shadow_fallback_reads=%" PRIu64 " gpu_failures=%" PRIu64
           " disk_failures=%" PRIu64 " elapsed_s=%.6f"
           " parent_maxrss_kib=%ld forced_worker_loss=YES"
           " real_gpu_reset=NO kernel_swap=NO\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",
           mib,ops,writes,reads,verified,
           stats.shadow_reads,stats.gpu_failures,stats.disk_failures,
           elapsed,rss.ru_maxrss);
    puts("V070_SHADOW_HARDWARE_PASS all_pages_verified=YES "
         "read_after_worker_loss=YES write_after_worker_loss=YES "
         "no_global_swap=YES no_physical_gpu_reset=YES");
    status=0;
done:
    nopen_shadow_close(s);
    if(status!=0)
        fprintf(stderr,"V070_SHADOW_HARDWARE_FAILED preserved_shadow_file=%s\n",
                file?file:"(none)");
    else if(unlink(file)!=0)
        fprintf(stderr,"V070_SHADOW_WARNING could_not_remove_test_shadow=%s\n",file);
    free(known);free(page);free(got);
    return status;
}
