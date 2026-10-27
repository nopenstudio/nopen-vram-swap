#include "nopen_page_core.h"
#include <stdint.h>

uint64_t nopen_page_checksum(const unsigned char *bytes, size_t length) {
    uint64_t v=UINT64_C(14695981039346656037);
    if (!bytes && length!=0) return 0;
    for(size_t i=0;i<length;i++) {
        v^=bytes[i]; v*=UINT64_C(1099511628211);
    }
    return v;
}
size_t nopen_page_oldest(const nopen_page_meta *pages,size_t count) {
    size_t victim=SIZE_MAX;
    uint64_t min_age=UINT64_MAX;
    if(!pages)return SIZE_MAX;
    for(size_t i=0;i<count;i++) {
        if(pages[i].location==NOPEN_P_RESIDENT && pages[i].fault_age<min_age) {
            victim=i; min_age=pages[i].fault_age;
        }
    }
    return victim;
}