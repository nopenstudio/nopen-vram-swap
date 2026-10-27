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
/* Freestanding policy; owner maintains the resident index. */
size_t nopen_page_oldest_indexed(const nopen_page_meta *pages, size_t logical_count,
                                const size_t *resident_ids, size_t resident_slots) {
    if (!pages || !resident_ids) return SIZE_MAX;
    size_t victim=SIZE_MAX;
    uint64_t age=UINT64_MAX;
    for (size_t i=0;i<resident_slots;i++) {
        size_t p=resident_ids[i];
        if (p>=logical_count || pages[p].location!=NOPEN_P_RESIDENT) continue;
        if (pages[p].fault_age<age) { age=pages[p].fault_age;victim=p; }
    }
    return victim;
}
