#ifndef NOPEN_DURABLE_STORE_H
#define NOPEN_DURABLE_STORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* IE-03: REAL filesystem I/O prototype, intentionally NOT attached to live
 * kernel swap or OpenCL. Explicit same-boot epoch, 4K..64K transaction and
 * checksum-validated journal. File-system durability claims are conditional
 * on actual fsync semantics and hardware cache correctness.
 * No restart/reissue-safe ublk lifecycle exists yet. */
typedef struct nopen_durable_store nopen_durable_store;
enum {
    NOPEN_DURABLE_FAIL_NONE=0,
    NOPEN_DURABLE_FAIL_AFTER_DATA_SYNC=1,
    NOPEN_DURABLE_FAIL_AFTER_MANIFEST_SYNC=2
};
int nopen_durable_open(nopen_durable_store **out,const char *absolute_base_path,
                      unsigned capacity_mib,uint64_t boot_epoch,int create_new);
int nopen_durable_write(nopen_durable_store *s,uint64_t request_id,
                        uint64_t byte_offset,const void *src,size_t bytes);
int nopen_durable_read(nopen_durable_store *s,uint64_t byte_offset,
                       void *dst,size_t bytes);
/* Inject a MOCK fault after completed real disk sync; never on live SWAP. */
int nopen_durable_inject(nopen_durable_store *s,int phase);
void nopen_durable_close(nopen_durable_store *s);
#ifdef __cplusplus
}
#endif
#endif
