#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PAGE 4096u
#define PAGE_COUNT 1024u
#define CHECK(ok) do { if(!(ok)) { \
    fprintf(stderr,"V080_ASYNC_SHADOW_TEST_FAILED line=%d check=%s\n",__LINE__,#ok); \
    return 2; } } while(0)

int main(int argc,char **argv) {
    CHECK(argc==3 && argv[1][0]=='/' && argv[2][0]=='/');
    CHECK(setenv("NOPEN_SHADOW_ASYNC_GPU","1",1)==0);
    char dir[4096];
    const int n=snprintf(dir,sizeof dir,"%s/nopen-async-XXXXXX",argv[2]);
    CHECK(n>0 && (size_t)n<sizeof dir);
    CHECK(mkdtemp(dir)!=NULL);
    char disk[4096];
    const int m=snprintf(disk,sizeof disk,"%s/shadow.bin",dir);
    CHECK(m>0 && (size_t)m<sizeof disk);
    unsigned char *expected=calloc(PAGE_COUNT,PAGE);
    unsigned char src[PAGE],got[PAGE];
    CHECK(expected!=NULL);
    nopen_shadow *s=NULL;
    CHECK(nopen_shadow_open(&s,NOPEN_STORE_MOCK,4,1000,argv[1],disk)==0);
    nopen_shadow_stats stat={0};
    CHECK(nopen_shadow_get_stats(s,&stat)==0 && stat.async_gpu_mode==1);
    /* Rewrites to the SAME page while async uploads run must never make
     * stale GPU content readable. The durable disk is always authoritative. */
    for(size_t i=0;i<2400;i++) {
        const size_t index=(i*31u+(i/7u)*97u)%PAGE_COUNT;
        for(size_t j=0;j<PAGE;j++)
            src[j]=(unsigned char)(i*37u+j*11u+(i>>3));
        CHECK(nopen_shadow_write(s,(uint64_t)index*PAGE,src,PAGE)==0);
        memcpy(expected+index*PAGE,src,PAGE);
        if(i%9u==0) {
            CHECK(nopen_shadow_read(s,(uint64_t)index*PAGE,got,PAGE)==0);
            CHECK(memcmp(got,expected+index*PAGE,PAGE)==0);
        }
    }
    CHECK(nopen_shadow_get_stats(s,&stat)==0);
    CHECK(stat.writes==2400 && stat.async_enqueued>0 && !stat.poisoned);
    /* Stop uploads and kill the worker while pages remain acknowledged:
     * all reads, including dropped GPU-cache uploads, must still succeed. */
    CHECK(nopen_shadow_simulate_gpu_loss(s)==0);
    for(size_t page=0;page<PAGE_COUNT;page++) {
        memset(got,0xA5,PAGE);
        CHECK(nopen_shadow_read(s,(uint64_t)page*PAGE,got,PAGE)==0);
        CHECK(memcmp(got,expected+page*PAGE,PAGE)==0);
    }
    CHECK(nopen_shadow_get_stats(s,&stat)==0);
    CHECK(stat.gpu_failures==1 && !stat.gpu_active && !stat.poisoned);
    for(size_t j=0;j<PAGE;j++)src[j]=(unsigned char)(j*17u+7u);
    CHECK(nopen_shadow_write(s,PAGE*3u,src,PAGE)==0);
    CHECK(nopen_shadow_read(s,PAGE*3u,got,PAGE)==0);
    CHECK(memcmp(got,src,PAGE)==0);
    printf("V080_ASYNC_SHADOW_PASS writes=%llu enqueued=%llu"
           " skipped=%llu gpu_completions=%llu disk_reads=%llu"
           " after_worker_loss_verified_pages=%u disk_ack_durable=YES"
           " kernel_swap=NO\n",
           (unsigned long long)stat.writes,
           (unsigned long long)stat.async_enqueued,
           (unsigned long long)stat.async_dropped,
           (unsigned long long)stat.gpu_writes,
           (unsigned long long)stat.async_disk_reads,
           PAGE_COUNT);
    nopen_shadow_close(s);
    free(expected);
    CHECK(unlink(disk)==0);
    CHECK(rmdir(dir)==0);
    return 0;
}
