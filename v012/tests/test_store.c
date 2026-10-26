#include "nopen_store.h"
#include <stdio.h>
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr,"CHECK FAILED %s:%d: %s\n",__FILE__,__LINE__,#expr); \
    exit(EXIT_FAILURE); } } while(0)
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static void pattern(unsigned char *p,size_t page) {
    uint32_t v=(uint32_t)page*2654435761u+0x9E3779B9u;
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {v^=v<<13;v^=v>>17;v^=v<<5;p[i]=(unsigned char)(v>>16);}
}
int main(void) {
    nopen_store *s=NULL;
    CHECK(nopen_store_open(&s,NOPEN_STORE_MOCK,2)==0);
    CHECK(nopen_store_page_count(s)==512);
    unsigned char *want=malloc(NOPEN_PAGE_SIZE),*actual=malloc(NOPEN_PAGE_SIZE);
    CHECK(want&&actual);
    for(size_t p=0;p<512;p++) {pattern(want,p);CHECK(nopen_store_write(s,p,want)==0);}
    for(size_t p=512;p>0;) {p--;pattern(want,p);CHECK(nopen_store_read(s,p,actual)==0);CHECK(memcmp(want,actual,NOPEN_PAGE_SIZE)==0);}
    CHECK(nopen_store_read(s,512,actual)!=0);
    CHECK(nopen_store_write(s,512,want)!=0);
    nopen_store_inject_failure(s,2);
    CHECK(nopen_store_read(s,0,actual)==0);
    CHECK(nopen_store_read(s,1,actual)!=0);
    CHECK(nopen_store_write(s,1,want)!=0);
    nopen_store_inject_failure(s,0);
    CHECK(nopen_store_read(s,1,actual)==0);
    actual[28]^=1;
    pattern(want,1);
    CHECK(memcmp(want,actual,NOPEN_PAGE_SIZE)!=0);
    free(want);free(actual);nopen_store_close(s);
    puts("PASS: 512 simulated GPU pages verified both directions, bounds, fault injection, corruption detection. NO global swap.");
    return 0;
}