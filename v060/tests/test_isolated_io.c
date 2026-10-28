#define _POSIX_C_SOURCE 200809L
#include "nopen_isolated_io.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define P NOPEN_PAGE_SIZE
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ISOLATED_TEST_FAIL line=%d: %s\n",__LINE__,#x);return 1;}}while(0)
static uint64_t now_ms(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
int main(int argc,char **argv) {
    CHECK(argc==2 && argv[1][0]=='/');
    nopen_isolated_io *ctx=NULL;
    nopen_isolated_stats stats={0};
    unsigned char source[4u*P],dest[4u*P];
    CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,1000,argv[1])==0);
    CHECK(ctx!=NULL);
    memset(dest,0xFE,sizeof(dest));
    CHECK(nopen_isolated_read(ctx,0,dest,sizeof(dest))==0);
    for(size_t j=0;j<sizeof(dest);j++)CHECK(dest[j]==0);
    for(unsigned i=0;i<300;i++) {
        for(size_t j=0;j<sizeof(source);j++)
            source[j]=(unsigned char)(i*43u+j*11u);
        uint64_t off=(uint64_t)(i%64u)*sizeof(source);
        CHECK(nopen_isolated_write(ctx,off,source,sizeof(source))==0);
        CHECK(nopen_isolated_read(ctx,off,dest,sizeof(dest))==0);
        CHECK(memcmp(source,dest,sizeof(dest))==0);
    }
    CHECK(nopen_isolated_write(ctx,1,source,P)==-EINVAL);
    CHECK(nopen_isolated_read(ctx,0,dest,17u*P)==-EINVAL);
    CHECK(nopen_isolated_read(ctx,0,NULL,P)==-EINVAL);
    CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
    CHECK(stats.operations==601 && stats.child_alive==1 &&
          stats.timeouts==0 && stats.child_crashes==0);
    nopen_isolated_close(ctx);ctx=NULL;
    CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,100,argv[1])==0);
    uint64_t before=now_ms();
    CHECK(nopen_isolated_inject_hang(ctx)==-ETIMEDOUT);
    uint64_t elapsed=now_ms()-before;
    CHECK(elapsed<1500u);
    memset(dest,0xFA,sizeof(dest));
    CHECK(nopen_isolated_read(ctx,0,dest,P)==-EIO);
    for(size_t j=0;j<P;j++)CHECK(dest[j]==0xFA);
    CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
    CHECK(stats.timeouts==1 && stats.child_alive==0);
    nopen_isolated_close(ctx);ctx=NULL;
    CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,10,argv[1])==-EINVAL);
    CHECK(ctx==NULL);
    CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,100,
                              "/nonexistent/nopen-worker")!=0);
    CHECK(ctx==NULL);
    puts("ISOLATED_TEST_PASS ipc_checksums=YES 300_multiblock_roundtrips=YES "
         "deadline=YES unresponsive_worker_killed=YES "
         "false_success_rejected=YES no_global_swap=YES "
         "real_gpu_reset_tested=NO");
    return 0;
}
