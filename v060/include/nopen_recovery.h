#ifndef NOPEN_RECOVERY_H
#define NOPEN_RECOVERY_H
#include "nopen_blk_backend.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Only one writer per instance; all public I/O and state are mutex-protected.
 * An optional HOST_MIRROR is deliberately expensive: its CPU RAM copy is the
 * authoritative, last-ACKed state during device loss. It is a recovery lab,
 * not a proof of RAM savings or a Linux swap implementation. */
typedef struct nopen_recovery nopen_recovery;
typedef enum {
    NOPEN_RECOVERY_GPU_ACTIVE = 1,
    NOPEN_RECOVERY_CPU_FALLBACK = 2,
    NOPEN_RECOVERY_FAILED_CLOSED = 3
} nopen_recovery_state;
typedef enum {
    NOPEN_RECOVERY_NO_MIRROR = 0,
    NOPEN_RECOVERY_HOST_MIRROR = 1
} nopen_recovery_mode;
typedef struct {
    uint64_t reads, writes;
    uint64_t gpu_read_errors, gpu_write_errors;
    uint64_t device_loss_events, fallback_reads, fallback_writes;
    uint64_t integrity_failures;
    uint64_t mirror_bytes;
    nopen_recovery_state state;
} nopen_recovery_stats;

/* Bounded 4..64MiB. NO fallback at open: GPU allocation must succeed.
 * HOST_MIRROR allocates complete mirror before opening the GPU backend and
 * will fail if the mirror allocation fails; never silently downgrade safety.
 */
int nopen_recovery_open(nopen_recovery **out,nopen_backend backend,
                        size_t capacity_mib,nopen_recovery_mode mode);
/* Caller-supplied total host-allocation budget, counted conservatively:
 * mirror, checksums, scratch and wrapper metadata. Returns -ENOMEM BEFORE
 * GPU initialization if impossible; does not change system memory limits.
 * The OpenCL runtime/driver allocations are outside this known budget. */
int nopen_recovery_open_bounded(nopen_recovery **out,nopen_backend backend,
                        size_t capacity_mib,nopen_recovery_mode mode,
                        size_t max_host_bytes);
int nopen_recovery_read(nopen_recovery *r,uint64_t offset,void *dst,size_t bytes);
int nopen_recovery_write(nopen_recovery *r,uint64_t offset,const void *src,size_t bytes);

/* Device-loss notification: default --> FAILED_CLOSED;
 * HOST_MIRROR --> CPU_FALLBACK (RAM authoritative), no GPU retries. */
int nopen_recovery_device_lost(nopen_recovery *r);
int nopen_recovery_get_stats(nopen_recovery *r,nopen_recovery_stats *out);
void nopen_recovery_close(nopen_recovery *r);

/* TEST INJECTION ONLY. For MOCK backend; never triggers actual GPU reset. */
int nopen_recovery_inject_io_failure(nopen_recovery *r,size_t next_operation);
/* MOCK-only diagnostic: corrupt authoritative CPU mirror for checksum tests. */
int nopen_recovery_inject_mirror_corruption(nopen_recovery *r,size_t page);
#ifdef __cplusplus
}
#endif
#endif
