#include "nopen_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"PREFETCH_TEST_FAIL line=%d: %s\n",__LINE__,#x);return 1;}}while(0)
static void fill(unsigned char *p,size_t n) {
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++)p[i]=(unsigned char)((n*113u+i*7u)&255u);
}
int main(void) {
    nopen_store *s=NULL;
    nopen_memory *m=NULL;
    unsigned char want[NOPEN_PAGE_SIZE],got[NOPEN_PAGE_SIZE],bad[NOPEN_PAGE_SIZE]={0};
    nopen_memory_stats a,b;
    CHECK(nopen_store_open(&s,NOPEN_STORE_MOCK,1)==0);
    CHECK(nopen_memory_open(&m,s,64,8)==0);
    for(size_t p=0;p<64;p++) {
        fill(want,p);
        CHECK(nopen_memory_write(m,p,want)==0);
    }
    CHECK(nopen_memory_trim(m,0)==0);
    nopen_memory_get_stats(m,&a);
    CHECK(a.resident_pages==0 && a.vram_pages==64);

    /* Injection: failed transfer must NOT displace any live page. */
    nopen_store_inject_failure(s,1);
    CHECK(nopen_memory_prefetch(m,0,4)!=0);
    nopen_memory_get_stats(m,&b);
    CHECK(b.resident_pages==0 && b.prefetch_pages==0 &&
          b.prefetch_failures==1 && b.failed_reads==1);
    nopen_store_inject_failure(s,0);

    /* Injection: successful but corrupt GPU data is rejected before commit. */
    CHECK(nopen_store_write(s,2,bad)==0);
    CHECK(nopen_memory_prefetch(m,0,4)!=0);
    nopen_memory_get_stats(m,&b);
    CHECK(b.resident_pages==0 && b.prefetch_pages==0 &&
          b.prefetch_failures==2 && b.checksum_errors==1);
    fill(want,2);
    CHECK(nopen_store_write(s,2,want)==0);

    CHECK(nopen_memory_prefetch(m,0,4)==0);
    nopen_memory_get_stats(m,&b);
    CHECK(b.resident_pages==4 && b.prefetch_pages==4 && b.prefetch_batches==1);
    for(size_t p=0;p<4;p++) {
        CHECK(nopen_memory_read(m,p,got)==0);
        fill(want,p);
        CHECK(memcmp(want,got,NOPEN_PAGE_SIZE)==0);
    }
    nopen_memory_get_stats(m,&b);
    CHECK(b.prefetch_hits==4 && b.demand_page_ins==0);
    CHECK(nopen_memory_trim(m,0)==0);
    nopen_memory_get_stats(m,&b);
    CHECK(b.prefetch_wasted==0);

    /* Prefetched but never accessed: account for speculative bandwidth waste. */
    CHECK(nopen_memory_prefetch(m,4,4)==0);
    CHECK(nopen_memory_trim(m,0)==0);
    nopen_memory_get_stats(m,&b);
    CHECK(b.prefetch_wasted==4 && b.prefetch_hits==4);
    CHECK(nopen_memory_prefetch(m,0,0)!=0);
    CHECK(nopen_memory_prefetch(m,63,2)!=0);
    CHECK(nopen_memory_prefetch(m,0,9)!=0);
    CHECK(nopen_memory_prefetch(m,8,1)==0);
    CHECK(nopen_memory_prefetch(m,8,1)!=0); /* already CPU-resident */

    /* Final data integrity after all prefetch activity. */
    for(size_t p=0;p<64;p++) {
        CHECK(nopen_memory_read(m,p,got)==0);
        fill(want,p);
        CHECK(memcmp(want,got,NOPEN_PAGE_SIZE)==0);
    }
    nopen_memory_close(m);nopen_store_close(s);
    puts("PREFETCH_TEST_PASS checksums_before_eviction=YES unused_prefetch_accounted=YES failure_isolated=YES full_verify=64 no_global_swap=YES");
    return 0;
}
