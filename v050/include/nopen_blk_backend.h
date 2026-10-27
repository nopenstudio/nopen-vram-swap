#ifndef NOPEN_BLK_BACKEND_H
#define NOPEN_BLK_BACKEND_H
#include "nopen_store.h"
#include <stddef.h>
#include <stdint.h>

/* An actual GPU-backed 4KiB block I/O data plane for future ublk.
 * This is not a registered Linux block device and may not be used as host swap.
 * Fail-closed: a failed write poisons the entire affected logical range,
 * because GPU writes can have partially completed before a transfer error.
 * Reads never return unchecked bytes to the caller. */
#define NOPEN_BLK_MAX_BYTES (16u * NOPEN_PAGE_SIZE)
typedef struct nopen_blk_backend nopen_blk_backend;
int nopen_blk_open(nopen_blk_backend **out,nopen_backend backend,size_t capacity_mib);
int nopen_blk_read(nopen_blk_backend *b,uint64_t byte_offset,void *buf,size_t length);
int nopen_blk_write(nopen_blk_backend *b,uint64_t byte_offset,const void *buf,size_t length);
void nopen_blk_close(nopen_blk_backend *b);
void nopen_blk_inject_failure(nopen_blk_backend *b,size_t operation);
const char *nopen_blk_last_error(const nopen_blk_backend *b);
size_t nopen_blk_page_count(const nopen_blk_backend *b);
#endif
