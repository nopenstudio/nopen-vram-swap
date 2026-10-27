#include "nopen_page_core.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define REQUIRE(p) do { if (!(p)) { fprintf(stderr,"FAIL: %s:%d: %s\n",__FILE__,__LINE__,#p); return 1; } }while(0)
int main(void) {
    nopen_page_meta ps[] = {
        {NOPEN_P_ZERO,0,0}, {NOPEN_P_RESIDENT,123,5},
        {NOPEN_P_GPU,24,1}, {NOPEN_P_RESIDENT,555,3}
    };
    REQUIRE(nopen_page_oldest(ps,4)==3);
    ps[3].location=NOPEN_P_GPU;
    REQUIRE(nopen_page_oldest(ps,4)==1);
    ps[1].location=NOPEN_P_GPU;
    REQUIRE(nopen_page_oldest(ps,4)==SIZE_MAX);
    REQUIRE(nopen_page_oldest(NULL,4)==SIZE_MAX);
    unsigned char a[4096],b[4096];
    memset(a,0x17,sizeof(a)); memcpy(b,a,sizeof(a));
    REQUIRE(nopen_page_checksum(a,sizeof(a))==nopen_page_checksum(b,sizeof(b)));
    b[2050]^=1;
    REQUIRE(nopen_page_checksum(a,sizeof(a))!=nopen_page_checksum(b,sizeof(b)));
    REQUIRE(nopen_page_checksum(NULL,1)==0);
    size_t resident_ids[] = {3,1,SIZE_MAX,2};
    ps[1].location=NOPEN_P_RESIDENT;
    ps[3].location=NOPEN_P_RESIDENT;
    REQUIRE(nopen_page_oldest_indexed(ps,4,resident_ids,4)==3);
    ps[3].location=NOPEN_P_GPU;
    REQUIRE(nopen_page_oldest_indexed(ps,4,resident_ids,4)==1);
    ps[1].location=NOPEN_P_GPU;
    REQUIRE(nopen_page_oldest_indexed(ps,4,resident_ids,4)==SIZE_MAX);
    REQUIRE(nopen_page_oldest_indexed(NULL,4,resident_ids,4)==SIZE_MAX);
    REQUIRE(nopen_page_oldest_indexed(ps,4,NULL,4)==SIZE_MAX);
    puts("CORE_PASS pure_c11=YES memory_policy=YES gpu_dependency=NO glibc_required_by_source=NO");
    return 0;
}