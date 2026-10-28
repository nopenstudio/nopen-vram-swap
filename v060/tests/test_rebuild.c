#include "nopen_recovery.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"REBUILD_TEST_FAIL line=%d expr=%s\n",__LINE__,#x);return 1;} }while(0)
#define P NOPEN_PAGE_SIZE
#define COUNT 1024u
typedef struct {
    nopen_recovery *r;
    int id;
    int errors;
} worker;
static void *worker_io(void *p) {
    worker *w=(worker*)p;
    unsigned char source[P],got[P];
    const size_t begin=(size_t)w->id*128u;
    for(size_t iteration=0;iteration<64;iteration++) {
        size_t page=begin+(iteration%128u);
        for(size_t j=0;j<P;j++)
            source[j]=(unsigned char)(w->id*23+iteration*11+j);
        if(nopen_recovery_write(w->r,(uint64_t)page*P,source,P)!=0 ||
           nopen_recovery_read(w->r,(uint64_t)page*P,got,P)!=0 ||
           memcmp(source,got,P)!=0) {
            w->errors++;
            break;
        }
    }
    return NULL;
}
int main(void) {
    nopen_recovery *r=NULL;
    nopen_recovery_stats s={0};
    unsigned char expected[COUNT*P]={0},got[P]={0};
    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_NO_MIRROR)==0);
    CHECK(nopen_recovery_device_lost(r)==-EIO);
    CHECK(nopen_recovery_rebuild_gpu(r)==-EPERM);
    nopen_recovery_close(r);r=NULL;

    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_HOST_MIRROR)==0);
    for(size_t page=0;page<COUNT;page++) {
        for(size_t i=0;i<P;i++)
            expected[page*P+i]=(unsigned char)(page*17u+i*3u);
        CHECK(nopen_recovery_write(r,(uint64_t)page*P,expected+page*P,P)==0);
    }
    CHECK(nopen_recovery_device_lost(r)==0);
    CHECK(nopen_recovery_inject_rebuild_failure(r,16)==0);
    CHECK(nopen_recovery_rebuild_gpu(r)==-EIO);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.state==NOPEN_RECOVERY_CPU_FALLBACK && s.rebuild_attempts==1 &&
          s.rebuild_failures==1 && s.rebuild_successes==0);
    for(size_t page=0;page<COUNT;page++) {
        memset(got,0xAF,sizeof(got));
        CHECK(nopen_recovery_read(r,(uint64_t)page*P,got,P)==0);
        CHECK(memcmp(got,expected+page*P,P)==0);
    }
    CHECK(nopen_recovery_rebuild_gpu(r)==0);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.state==NOPEN_RECOVERY_GPU_ACTIVE && s.rebuild_attempts==2 &&
          s.rebuild_failures==1 && s.rebuild_successes==1 &&
          s.rebuild_verified_pages>=COUNT);
    for(size_t page=0;page<COUNT;page++) {
        CHECK(nopen_recovery_read(r,(uint64_t)page*P,got,P)==0);
        CHECK(memcmp(got,expected+page*P,P)==0);
    }

    /* Four threads writing DISJOINT ranges. Lock must protect state and
     * memory buffers without exposing other threads' staged GPU data. */
    worker workers[4]={0};
    pthread_t threads[4];
    for(int i=0;i<4;i++) {
        workers[i].r=r;workers[i].id=i;
        CHECK(pthread_create(&threads[i],NULL,worker_io,&workers[i])==0);
    }
    for(int i=0;i<4;i++) {
        CHECK(pthread_join(threads[i],NULL)==0);
        CHECK(workers[i].errors==0);
    }

    /* Simulated loss again; mirror is now the authoritative committed truth. */
    CHECK(nopen_recovery_device_lost(r)==0);
    CHECK(nopen_recovery_rebuild_gpu(r)==0);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.state==NOPEN_RECOVERY_GPU_ACTIVE && s.rebuild_successes==2);
    nopen_recovery_close(r);r=NULL;

    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_HOST_MIRROR)==0);
    CHECK(nopen_recovery_device_lost(r)==0);
    CHECK(nopen_recovery_inject_mirror_corruption(r,0)==0);
    CHECK(nopen_recovery_rebuild_gpu(r)==-EIO);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.state==NOPEN_RECOVERY_FAILED_CLOSED && s.integrity_failures==1);
    CHECK(nopen_recovery_rebuild_gpu(r)==-EPERM);
    nopen_recovery_close(r);
    puts("REBUILD_TEST_PASS mirror_rehydrated=YES per_page_integrity=YES "
         "partial_failure_keeps_cpu=YES multiple_rebuilds=YES "
         "thread_serialization=YES mirror_corruption_rejected=YES "
         "actual_gpu_reset_tested=NO no_global_swap=YES");
    return 0;
}
