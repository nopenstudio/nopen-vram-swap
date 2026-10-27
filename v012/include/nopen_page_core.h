#ifndef NOPEN_PAGE_CORE_H
#define NOPEN_PAGE_CORE_H
#include <stddef.h>
#include <stdint.h>

/* Pure ISO C11 page policy. No GNU, Linux syscalls, GPU libraries or allocation.
 * This is a POLICY library, not a Linux kernel module or stable kernel ABI. */
typedef enum { NOPEN_P_ZERO=0, NOPEN_P_RESIDENT=1, NOPEN_P_GPU=2 } nopen_page_state;
typedef struct {
    nopen_page_state location;
    uint64_t checksum;
    uint64_t fault_age;
} nopen_page_meta;
uint64_t nopen_page_checksum(const unsigned char *bytes, size_t length);
size_t nopen_page_oldest(const nopen_page_meta *pages, size_t count);
#endif