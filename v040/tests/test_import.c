#include "nopen_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"IMPORT_TEST_FAILED line=%d assertion=%s\n",__LINE__,#x);return 1; } }while(0)
int main(void) {
    nopen_store *store=NULL;
    nopen_memory *m=NULL;
    unsigned char a[8u*NOPEN_PAGE_SIZE],readback[NOPEN_PAGE_SIZE];
    nopen_memory_stats before,after;
    CHECK(nopen_store_open(&store,NOPEN_STORE_MOCK,1)==0);
    CHECK(nopen_memory_open(&m,store,64,8)==0);
    for(size_t i=0;i<sizeof(a);i++)a[i]=(unsigned char)((i*7u+(i/4096u)*17u)&255u);
    CHECK(nopen_memory_import_pages(m,0,a,8)==0);
    nopen_memory_get_stats(m,&before);
    CHECK(before.zero_pages==56 && before.vram_pages==8 && before.resident_pages==0);
    CHECK(nopen_memory_import_pages(m,0,a,1)!=0); /* refuse overwrite */
    CHECK(nopen_memory_import_pages(m,60,a,8)!=0); /* refuse OOB */
    CHECK(nopen_memory_import_pages(m,9,a,0)!=0);
    CHECK(nopen_memory_import_pages(m,9,a,65)!=0);
    CHECK(nopen_memory_import_pages(m,9,NULL,1)!=0);
    nopen_memory_get_stats(m,&after);
    CHECK(after.zero_pages==before.zero_pages && after.vram_pages==before.vram_pages);
    nopen_store_inject_failure(store,1);
    CHECK(nopen_memory_import_pages(m,8,a,8)!=0);
    nopen_memory_get_stats(m,&after);
    CHECK(after.zero_pages==56 && after.vram_pages==8 && after.failed_writes==1);
    nopen_store_inject_failure(store,0);
    CHECK(nopen_memory_import_pages(m,8,a,8)==0);
    nopen_memory_get_stats(m,&after);
    CHECK(after.zero_pages==48 && after.vram_pages==16);
    for(size_t p=0;p<16;p++) {
        CHECK(nopen_memory_read(m,p,readback)==0);
        CHECK(memcmp(readback,a+(p%8u)*NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE)==0);
    }
    nopen_memory_get_stats(m,&after);
    CHECK(after.zero_pages==48 && after.reads==16 && after.page_ins==16);
    CHECK(after.resident_pages<=8);
    nopen_memory_close(m);nopen_store_close(store);
    puts("IMPORT_TEST_PASS bulk_gpu_zero_pages=YES injection_atomic_metadata=YES bounds=YES readback=16");
    return 0;
}
