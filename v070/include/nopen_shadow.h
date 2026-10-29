#ifndef NOPEN_SHADOW_H
#define NOPEN_SHADOW_H
#include "nopen_isolated_io.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct nopen_shadow nopen_shadow;
typedef struct {
    uint64_t writes, reads, gpu_reads, gpu_writes;
    uint64_t shadow_reads, zero_reads, gpu_failures, integrity_mismatches;
    uint64_t disk_failures, backed_bytes;
    int gpu_active, poisoned;
} nopen_shadow_stats;

/* Every acknowledged write first persists to a fully initialized, O_DIRECT |
 * O_DSYNC NVMe shadow file. Then it is sent to GPU. Lost or corrupt GPU
 * worker can no longer strand an ACKed swap page: read uses shadow.
 * This is not a pure GPU-swap benchmark: SSD write-through overhead is real.
 * The caller must use a truly independent, non-swap-backed filesystem.
 * New path O_EXCL; NEVER truncate/reuse an existing data file. */
int nopen_shadow_open(nopen_shadow **out,nopen_backend backend,
    size_t capacity_mib,unsigned timeout_ms,
    const char *worker_absolute_path,const char *shadow_absolute_path);
int nopen_shadow_read(nopen_shadow *s,uint64_t offset,void *dst,size_t bytes);
int nopen_shadow_write(nopen_shadow *s,uint64_t offset,const void *src,size_t bytes);
int nopen_shadow_get_stats(nopen_shadow *s,nopen_shadow_stats *out);
void nopen_shadow_close(nopen_shadow *s);
/* MOCK-only, test-triggered 10s hang. Returns failure; fallback persists. */
int nopen_shadow_mock_inject_gpu_hang(nopen_shadow *s);
/* Lab-only fault injection, safe with real OpenCL: intentionally close/kill
 * only this process's GPU worker without resetting physical GPU. All pages
 * must subsequently be served from authoritative O_DIRECT SSD shadow. */
int nopen_shadow_simulate_gpu_loss(nopen_shadow *s);
#ifdef __cplusplus
}
#endif
#endif
