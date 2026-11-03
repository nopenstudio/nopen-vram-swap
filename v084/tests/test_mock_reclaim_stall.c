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
#include <pthread.h>
#define PAGE 4096u
#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"V084_STALL_TEST_FAILED line=%d test=%s\n",__LINE__,#x); \
    return 1; \
} } while(0)
static uint64_t ticks(void) {
    struct timespec t={0};
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
typedef struct {
    nopen_isolated_io *ctx;
    unsigned char dest[PAGE];
    int result;
} stalled_call;
static void *call_read(void *arg) {
    stalled_call *c=arg;
    memset(c->dest,0xC7,sizeof c->dest);
    c->result=nopen_isolated_read(c->ctx,0,c->dest,sizeof c->dest);
    return NULL;
}
/* Issue #10: distinguish rejection of an UNDISPATCHED queued request from
 * a timeout of the ACTIVE worker. Neither outcome is a SWAP recovery. */
static int test_two_budgets(const char *worker) {
    nopen_isolated_deadline_policy policy={
        .admission_timeout_ms=35,.io_timeout_ms=500
    };
    nopen_isolated_io *ctx=NULL;
    unsigned char data[PAGE];
    memset(data,0xA5,sizeof data);
    CHECK(setenv("NOPEN_V084_MOCK_WAIT_ON_RECLAIM","read",1)==0);
    CHECK(nopen_isolated_open_with_policy(&ctx,NOPEN_STORE_MOCK,4,
                                          &policy,worker)==0);
    CHECK(unsetenv("NOPEN_V084_MOCK_WAIT_ON_RECLAIM")==0);
    stalled_call first={.ctx=ctx,.result=0};
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,call_read,&first)==0);
    /* The worker sleeps 10s on READ, so the first thread holds the
     * serialized parent IPC mutex until its 500ms response timeout. */
    struct timespec delay={.tv_sec=0,.tv_nsec=80000000L};
    while(nanosleep(&delay,&delay)<0 && errno==EINTR){}
    uint64_t begun=ticks();
    const int queued_rc=nopen_isolated_write(ctx,PAGE,data,PAGE);
    const uint64_t queued_ms=ticks()-begun;
    CHECK(queued_rc==-EAGAIN && queued_ms<250u);
    CHECK(pthread_join(thread,NULL)==0);
    CHECK(first.result==-ETIMEDOUT);
    for(size_t i=0;i<sizeof first.dest;i++)CHECK(first.dest[i]==0xC7);
    nopen_isolated_stats stats={0};
    CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
    CHECK(stats.admission_timeouts==1 && stats.timeouts==1);
    CHECK(stats.operations==0 && stats.io_errors==1 && stats.child_alive==0);
    CHECK(nopen_isolated_write(ctx,PAGE,data,PAGE)==-EIO);
    nopen_isolated_close(ctx);
    ctx=NULL;
    policy.admission_timeout_ms=0;
    CHECK(nopen_isolated_open_with_policy(&ctx,NOPEN_STORE_MOCK,4,
                                          &policy,worker)==-EINVAL);
    CHECK(ctx==NULL);
    policy.admission_timeout_ms=35;
    policy.io_timeout_ms=5001;
    CHECK(nopen_isolated_open_with_policy(&ctx,NOPEN_STORE_MOCK,4,
                                          &policy,worker)==-EINVAL);
    CHECK(ctx==NULL);
    puts("V084_DEADLINE_POLICY_PASS admission_rejected_before_dispatch=YES"
         " fatal_dispatched_timeout=YES read_buffer_unchanged=YES"
         " gpu_only_recovery=NO kernel_swap=NO");
    return 0;
}
/* A busy worker can still succeed: local queue rejection must NOT
 * invalidate already acknowledged pages or kill the healthy subprocess. */
static int test_admission_is_nonfatal(const char *worker) {
    const nopen_isolated_deadline_policy policy={
        .admission_timeout_ms=35,.io_timeout_ms=1000
    };
    nopen_isolated_io *ctx=NULL;
    CHECK(setenv("NOPEN_IPC_MOCK_DELAY_MS","300",1)==0);
    CHECK(nopen_isolated_open_with_policy(&ctx,NOPEN_STORE_MOCK,4,
                                          &policy,worker)==0);
    CHECK(unsetenv("NOPEN_IPC_MOCK_DELAY_MS")==0);
    stalled_call first={.ctx=ctx,.result=-999};
    pthread_t thread;
    CHECK(pthread_create(&thread,NULL,call_read,&first)==0);
    struct timespec delay={.tv_sec=0,.tv_nsec=100000000L};
    while(nanosleep(&delay,&delay)<0 && errno==EINTR){}
    unsigned char page[PAGE];
    memset(page,0x7D,sizeof page);
    const int queued=nopen_isolated_write(ctx,PAGE,page,PAGE);
    CHECK(queued==-EAGAIN);
    CHECK(pthread_join(thread,NULL)==0);
    CHECK(first.result==0);
    for(size_t i=0;i<sizeof first.dest;i++)CHECK(first.dest[i]==0);
    nopen_isolated_stats stats={0};
    CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
    CHECK(stats.operations==1 && stats.admission_timeouts==1);
    CHECK(stats.timeouts==0 && stats.io_errors==0 && stats.child_alive==1);
    /* The admitted operation and a subsequent request still work. */
    CHECK(nopen_isolated_write(ctx,PAGE,page,PAGE)==0);
    CHECK(nopen_isolated_stats_get(ctx,&stats)==0);
    CHECK(stats.operations==2 && stats.child_alive==1);
    nopen_isolated_close(ctx);
    puts("V084_ADMISSION_NONFATAL_PASS healthy_worker_retained=YES"
         " old_ack_data_not_invalidated=YES rejected_write_not_acked=YES"
         " mock_only=YES kernel_swap=NO");
    return 0;
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
               " elapsed_ms=%llu\n",op,(unsigned long long)elapsed);
    }
    CHECK(test_two_budgets(argv[1])==0);
    CHECK(test_admission_is_nonfatal(argv[1])==0);
    puts("V084_MOCK_RECLAIM_STALL_PASS worker_alive_before_timeout=YES"
         " synthetic_wait=YES normal_read_write=YES"
         " no_memory_pressure=YES no_gpu=YES no_kernel_swap=YES"
         " real_forward_progress_proven=NO");
    return 0;
}
