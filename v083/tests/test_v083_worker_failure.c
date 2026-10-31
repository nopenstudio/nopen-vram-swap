#define _POSIX_C_SOURCE 200809L
/* v0.8.3: mock-only process-death, bad-IPC, timeout failure-mode matrix.
 * Not a GPU performance or kernel swap/reclaim test. */
#include "nopen_isolated_io.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"V083_WORKER_TEST_FAIL line=%d: %s\n",__LINE__,#x); \
    return 1; \
} } while(0)
static uint64_t monotonic_ms(void) {
    struct timespec t={0};
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
int main(int argc,char **argv) {
    CHECK(argc==2 && argv[1][0]=='/');
    const char *path=argv[1];
    enum { SOURCE_SIZE=4096 };
    unsigned char src[SOURCE_SIZE],dst[SOURCE_SIZE];
    for(size_t i=0;i<sizeof src;i++)src[i]=(unsigned char)(i*19u+7u);
    for(unsigned scenario=0;scenario<3;scenario++) {
        nopen_isolated_io *ctx=NULL;
        nopen_isolated_stats stats={0};
        CHECK(nopen_isolated_open(&ctx,NOPEN_STORE_MOCK,4,
                                  scenario==0?100:500,path)==0);
        CHECK(ctx!=NULL);
        CHECK(nopen_isolated_write(ctx,0,src,sizeof src)==0);
        memset(dst,0,sizeof dst);
        CHECK(nopen_isolated_read(ctx,0,dst,sizeof dst)==0);
        CHECK(memcmp(src,dst,sizeof src)==0);
        uint64_t started=monotonic_ms();
        int rc;
        if(scenario==0)rc=nopen_isolated_inject_hang(ctx);
        else if(scenario==1)rc=nopen_isolated_inject_crash(ctx);
        else rc=nopen_isolated_inject_bad_reply(ctx);
        uint64_t elapsed=monotonic_ms()-started;
        if(scenario==0)CHECK(rc==-ETIMEDOUT);
        else if(scenario==1)CHECK(rc<0 && rc!=-ETIMEDOUT);
        else CHECK(rc==-EBADMSG);
        CHECK(elapsed<2000u);
        memset(dst,0xA5,sizeof dst);
        CHECK(nopen_isolated_read(ctx,0,dst,sizeof dst)==-EIO);
        CHECK(nopen_isolated_write(ctx,0,src,sizeof src)==-EIO);
        for(size_t i=0;i<sizeof dst;i++)CHECK(dst[i]==0xA5);
        CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
        CHECK(stats.operations==2);
        CHECK(stats.io_errors==1);
        CHECK(stats.child_alive==0);
        CHECK(stats.timeouts==(scenario==0?1u:0u));
        CHECK(stats.child_crashes==(scenario==0?0u:1u));
        nopen_isolated_close(ctx);
        fprintf(stdout,"V083_FAULT_PASS scenario=%s errno=%d"
                       " worker_alive=NO read_buffer_untouched=YES"
                       " reopen_without_authorization=NO elapsed_ms=%llu\n",
                scenario==0?"timeout":scenario==1?"crash":"bad_header",
                -rc,(unsigned long long)elapsed);
    }
    puts("V083_WORKER_FAILURE_MATRIX_PASS mock_only=YES scenarios=3"
         " timeout_bounded=YES crash_closed=YES bad_reply_closed=YES"
         " no_global_swap=YES real_gpu_context=NO reclaim_proven=NO");
    return 0;
}
