#ifndef NOPEN_MEMORY_H
#define NOPEN_MEMORY_H

#include "nopen_store.h"
#include <stddef.h>
#include <stdint.h>

/* v0.12.0-alpha.2: cooperative, process-local paged memory manager.
 * This is not a Linux swap device, kernel module, or transparent malloc hook.
 * All functions require an external stop-the-world / single-threaded caller.
 * Store lifetime MUST exceed manager lifetime. Page size fixed at 4096 bytes.
 */
typedef struct nopen_memory nopen_memory;
typedef struct {
    size_t logical_pages, resident_limit_pages;
    size_t resident_pages, vram_pages, zero_pages;
    uint64_t page_ins, page_outs, writes, reads;
    uint64_t failed_reads, failed_writes, checksum_errors, madvise_errors;
} nopen_memory_stats;

/* logical_pages <= preallocated VRAM-store page_count; 0 < resident_slots <= logical_pages.
 * An anonymous resident staging pool of only resident_slots * 4096 is mapped.
 */
int nopen_memory_open(nopen_memory **out, nopen_store *store,
                      size_t logical_pages, size_t resident_slots);
/* Read/write exactly one complete 4KiB logical page; nonzero = failure, no fake zero page. */
int nopen_memory_read(nopen_memory *m, size_t page, void *out_4k);
int nopen_memory_write(nopen_memory *m, size_t page, const void *in_4k);
/* Explicitly push least-recently-used pages to the GPU; never required by Linux reclaim. */
int nopen_memory_trim(nopen_memory *m, size_t keep_resident);
void nopen_memory_get_stats(const nopen_memory *m, nopen_memory_stats *out);
const char *nopen_memory_last_error(const nopen_memory *m);
void nopen_memory_close(nopen_memory *m);
#endif