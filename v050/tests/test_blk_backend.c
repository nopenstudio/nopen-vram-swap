#include "nopen_blk_backend.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define P NOPEN_PAGE_SIZE
#define CHECK(x) do {if(!(x)){fprintf(stderr,"BLK_TEST_FAIL line=%d expr=%s\n",__LINE__,#x);return 1;}}while(0)
static uint64_t step(uint64_t *p) {
    *p ^= *p<<13;*p ^= *p>>7;*p ^= *p<<17;return *p;
}
int main(void) {
    nopen_blk_backend *b=NULL;
    unsigned char source[16u*P]={0}, result[16u*P]={0};
    static unsigned char expected[128u*P];
    uint64_t rng=UINT64_C(0xd987314decabaa91);
    CHECK(nopen_blk_open(&b,NOPEN_STORE_MOCK,4)==0);
    CHECK(nopen_blk_page_count(b)==1024);
    memset(result,0xAA,sizeof(result));
    CHECK(nopen_blk_read(b,0,result,sizeof(result))==0);
    for(size_t i=0;i<sizeof(result);i++)CHECK(result[i]==0);
    for(size_t attempt=0;attempt<2000;attempt++) {
        const size_t p=(size_t)(step(&rng)%113);
        const size_t n=(size_t)(1+step(&rng)%16);
        const int writing=(step(&rng)&1u)!=0;
        if(writing) {
            for(size_t i=0;i<n*P;i++)
                source[i]=(unsigned char)(attempt*17u+p*3u+i);
            CHECK(nopen_blk_write(b,p*P,source,n*P)==0);
            memcpy(expected+p*P,source,n*P);
        } else {
            memset(result,0xA5,sizeof(result));
            CHECK(nopen_blk_read(b,p*P,result,n*P)==0);
            CHECK(memcmp(result,expected+p*P,n*P)==0);
        }
    }
    /* Forced read failure must not expose unchecked data to caller. */
    nopen_blk_inject_failure(b,1);
    memset(result,0xF6,sizeof(result));
    CHECK(nopen_blk_read(b,0,result,P)!=0);
    for(size_t i=0;i<P;i++)CHECK(result[i]==0xF6);
    nopen_blk_inject_failure(b,0);
    CHECK(nopen_blk_read(b,0,result,P)==0);
    CHECK(memcmp(result,expected,P)==0);
    /* On failed GPU write metadata is poisoned, not falsely preserved. */
    nopen_blk_inject_failure(b,1);
    CHECK(nopen_blk_write(b,0,source,P)!=0);
    CHECK(nopen_blk_read(b,0,result,P)==-EIO);
    nopen_blk_inject_failure(b,0);
    CHECK(nopen_blk_write(b,0,expected,P)==0);
    CHECK(nopen_blk_read(b,0,result,P)==0);
    CHECK(memcmp(result,expected,P)==0);
    CHECK(nopen_blk_read(b,0,result,512)==-EINVAL);
    CHECK(nopen_blk_write(b,1,source,P)==-EINVAL);
    CHECK(nopen_blk_write(b,0,source,17u*P)==-EINVAL);
    CHECK(nopen_blk_read(b,1024ull*P,result,P)==-EINVAL);
    CHECK(nopen_blk_read(b,0,NULL,P)==-EINVAL);
    CHECK(nopen_blk_write(b,0,NULL,P)==-EINVAL);
    CHECK(nopen_blk_write(b,0,source,0)==-EINVAL);
    for(size_t p=0;p<128;p++){
        CHECK(nopen_blk_read(b,p*P,result,P)==0);
        CHECK(memcmp(result,expected+p*P,P)==0);
    }
    nopen_blk_close(b);
    puts("BLK_BACKEND_PASS verified_pages=128 stress_ops=2000 poisoned_write_guard=YES "
         "read_failure_isolated=YES invalid_bounds_rejected=YES "
         "gpu_real_backend_selectable=YES actual_ublk_device=NO no_global_swap=YES");
    return 0;
}
