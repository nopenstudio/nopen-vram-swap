#ifndef NOPEN_STORE_H
#define NOPEN_STORE_H
#include <stddef.h>
#include <stdint.h>

/* Fixed logical pages; OpenCL GPU buffers or RAM-only TEST MOCK (explicit). */
#define NOPEN_PAGE_SIZE 4096u
#define NOPEN_MAX_MIB 8192u
#define NOPEN_SEGMENT_MIB 256u

typedef enum { NOPEN_STORE_MOCK=1, NOPEN_STORE_OPENCL=2 } nopen_backend;
typedef struct nopen_store nopen_store;

/* No fallback: OPENCL returns an error if no qualifying discrete AMD GPU. */
int nopen_store_open(nopen_store **out, nopen_backend backend, size_t mib);
/* Contiguous pages are sent in bulk; 4KiB page addresses, checked range.
 * On error the destination may be partially transferred: caller must
 * validate data before committing page-state metadata. */
int nopen_store_write_pages(nopen_store *s, size_t first_page, const void *data, size_t pages);
int nopen_store_read_pages(nopen_store *s, size_t first_page, void *data, size_t pages);
int nopen_store_write(nopen_store *s, size_t page, const void *data);
int nopen_store_read(nopen_store *s, size_t page, void *data);
void nopen_store_close(nopen_store *s);
size_t nopen_store_page_count(const nopen_store *s);
const char *nopen_store_last_error(const nopen_store *s);
/* Testing only: fail all storage I/O starting on the Nth attempted operation. */
void nopen_store_inject_failure(nopen_store *s, size_t first_failed_operation);
#endif