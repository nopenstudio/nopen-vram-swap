#include "nopen_memory.h"
#include <stdint.h>
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"CHECK FAILED %s:%d: %s\n",__FILE__,__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while(0)
#include <stdlib.h>
#include <string.h>

static void pattern(unsigned char *buf,size_t page,unsigned round) {
    uint64_t h=((uint64_t)page+1)*UINT64_C(0x9E3779B97F4A7C15) ^
               ((uint64_t)round+1)*UINT64_C(0xD6E8FEB86659FD93);
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {
        h^=h>>12;h^=h<<25;h^=h>>27;
        buf[i]=(unsigned char)((h*UINT64_C(0x2545F4914F6CDD1D))>>56);
    }
}
int main(void) {
    nopen_store *s=NULL;
    CHECK(nopen_store_open(&s,NOPEN_STORE_MOCK,2)==0);
    nopen_memory *m=NULL;
    CHECK(nopen_memory_open(&m,s,320,8)==0);
    unsigned char *want=malloc(NOPEN_PAGE_SIZE),*got=malloc(NOPEN_PAGE_SIZE);
    CHECK(want&&got);
    /* The 320-page logical working set exceeds 8 CPU-resident slots by 40x. */
    for(unsigned round=0;round<3;round++) {
        for(size_t i=0;i<320;i++) {pattern(want,i,round);CHECK(nopen_memory_write(m,i,want)==0);}
        CHECK(nopen_memory_trim(m,0)==0);
        for(size_t i=320;i>0;) {i--;pattern(want,i,round);CHECK(nopen_memory_read(m,i,got)==0);
            CHECK(memcmp(got,want,NOPEN_PAGE_SIZE)==0);}
        CHECK(nopen_memory_trim(m,0)==0);
    }
    nopen_memory_stats st;
    nopen_memory_get_stats(m,&st);
    CHECK(st.logical_pages==320&&st.resident_pages==0&&st.vram_pages==320);
    CHECK(st.resident_limit_pages==8&&st.page_ins>=960&&st.page_outs>=960);
    CHECK(st.failed_reads==0&&st.failed_writes==0&&st.checksum_errors==0);
    /* A read failure must NOT discard previously resident pages or return fake zeros. */
    pattern(want,4,2);CHECK(nopen_memory_read(m,4,got)==0);
    nopen_store_inject_failure(s,1);
    CHECK(nopen_memory_read(m,5,got)!=0);
    nopen_memory_get_stats(m,&st);CHECK(st.resident_pages==1&&st.failed_reads==1);
    nopen_store_inject_failure(s,0);
    CHECK(nopen_memory_read(m,4,got)==0&&memcmp(want,got,NOPEN_PAGE_SIZE)==0);
    CHECK(nopen_memory_trim(m,0)==0);
    /* Deliberately corrupt the underlying VRAM slot, then verify closed failure. */
    CHECK(nopen_store_read(s,7,got)==0);got[121]^=0xA5;
    CHECK(nopen_store_write(s,7,got)==0);
    CHECK(nopen_memory_read(m,7,got)!=0);
    nopen_memory_get_stats(m,&st);CHECK(st.checksum_errors==1&&st.resident_pages==0);
    nopen_memory_close(m);nopen_store_close(s);

    /* GPU write failure during eviction must keep the only RAM copy intact. */
    s=NULL;CHECK(nopen_store_open(&s,NOPEN_STORE_MOCK,1)==0);
    m=NULL;CHECK(nopen_memory_open(&m,s,4,1)==0);
    pattern(want,0,1);CHECK(nopen_memory_write(m,0,want)==0);
    nopen_store_inject_failure(s,1);
    pattern(got,1,1);CHECK(nopen_memory_write(m,1,got)!=0);
    nopen_memory_get_stats(m,&st);CHECK(st.resident_pages==1&&st.vram_pages==0&&st.failed_writes==1);
    nopen_store_inject_failure(s,0);
    CHECK(nopen_memory_read(m,0,got)==0&&memcmp(want,got,NOPEN_PAGE_SIZE)==0);
    CHECK(nopen_memory_trim(m,0)==0);
    nopen_memory_close(m);nopen_store_close(s);
    free(want);free(got);
    puts("MANAGED_PASS: 320 logical pages / 8 resident slots; 3 rounds, failure atomicity, VRAM checksum, LRU eviction, no global swap");
    return 0;
}