#define _POSIX_C_SOURCE 200809L
/* v0.8.4. A mock worker is LIVE but artificially blocks on a synthetic
 * "host memory needed" event during a NORMAL READ/WRITE request. NO system
 * memory pressure, GPU allocation, /dev block device or kernel SWAP.
 * The only proven outcome is bounded userspace IPC error handling. */
#include "nopen_isolated_io.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define PAGE 4096u
#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"V084_STALL_TEST_FAILED line=%d test=%s\\n",__LINE__,#x); \
    return 1; \
} } while(0)
static uint64_t ticks(void) {
    struct timespec t={0};
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
int main(int argc,char **argv) {
    CHECK(argc==2 && argv[1][0]=='/');
    unsigned char source[PAGE],dest[PAGE];
    for(size_t j=0;j<PAGE;j++)source[j]=(unsigned char)(j*7u+11u);

    /* Uninjected mock baseline. */
    CHECK(unsetenv("NOPEN_V084_MOCK_WAIT_ON_RECLAIM")==0);
    nopen_isolated_io *ctx=NULL;
    CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,300,argv[1])==0);
    CHECK(nopen_isolated_write(ctx,0,source,PAGE)==0);
    CHECK(nopen_isolated_read(ctx,0,dest,PAGE)==0);
    CHECK(memcmp(source,dest,PAGE)==0);
    nopen_isolated_close(ctx);

    for(unsigned which=0;which<2;which++) {
        const char *op=which==0?"read":"write";
        CHECK(setenv("NOPEN_V084_MOCK_WAIT_ON_RECLAIM",op,1)==0);
        ctx=NULL;
        CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,100,argv[1])==0);
        CHECK(unsetenv("NOPEN_V084_MOCK_WAIT_ON_RECLAIM")==0);
        memset(dest,0xA5,PAGE);
        uint64_t started=ticks();
        const int rc=which==0?nopen_isolated_read(ctx,0,dest,PAGE)
                              :nopen_isolated_write(ctx,0,source,PAGE);
        uint64_t elapsed=ticks()-started;
        CHECK(rc==-ETIMEDOUT);
        CHECK(elapsed<1500u);
        for(size_t j=0;j<PAGE;j++)CHECK(dest[j]==0xA5);
        nopen_isolated_stats stats={0};
        CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
        CHECK(stats.timeouts==1 && stats.io_errors==1);
        CHECK(stats.child_alive==0 && stats.operations==0);
        CHECK(nopen_isolated_read(ctx,0,dest,PAGE)==-EIO);
        CHECK(nopen_isolated_write(ctx,0,source,PAGE)==-EIO);
        for(size_t j=0;j<PAGE;j++)CHECK(dest[j]==0xA5);
        nopen_isolated_close(ctx);
        printf("V084_STALLED_IO_PASS op=%s timeout=YES"
               " output_unchanged=YES worker_failed_closed=YES"
               " elapsed_ms=%llu\\n",op,(unsigned long long)elapsed);
    }
    puts("V084_MOCK_RECLAIM_STALL_PASS worker_alive_before_timeout=YES"
         " synthetic_wait=YES normal_read_write=YES"
         " no_memory_pressure=YES no_gpu=YES no_kernel_swap=YES"
         " real_forward_progress_proven=NO");
    return 0;
}
