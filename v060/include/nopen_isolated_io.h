#ifndef NOPEN_ISOLATED_IO_H
#define NOPEN_ISOLATED_IO_H
#include "nopen_blk_backend.h"
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct nopen_isolated_io nopen_isolated_io;
typedef struct {
    uint64_t operations, timeouts, child_crashes, io_errors;
    /* Request rejected before IPC dispatch because the shared worker was busy.
     * No GPU/worker data path was touched for this request. */
    uint64_t admission_timeouts;
    int child_alive;
} nopen_isolated_stats;
/* Two independent, monotonic-clock budgets:
 * admission_timeout_ms: how long a caller waits to acquire the IPC lock;
 * io_timeout_ms: total send/worker/receive deadline AFTER admission.
 * Neither controls a blocked OpenCL driver, kernel ublk timeout, GPU reset,
 * or recoverability of already acknowledged GPU-only SWAP pages.
 * A queue rejection returns -EAGAIN without terminating the worker.
 * A dispatched request with uncertain outcome remains fatal to this context.
 */
typedef struct {
    unsigned admission_timeout_ms; /* 1..5000 */
    unsigned io_timeout_ms;        /* 50..5000 */
} nopen_isolated_deadline_policy;
int nopen_isolated_open_with_policy(nopen_isolated_io **out,
                       nopen_backend backend,size_t pool_mib,
                       const nopen_isolated_deadline_policy *policy,
                       const char *absolute_worker_path);
/* Legacy one-budget API: both queue admission and dispatched IPC use the
 * same timeout_ms (50..5000). New callers should explicitly set both budgets.
 * A dispatched timeout closes the channel and marks the context permanently
 * lost; no restart, replay, or SWAP recovery is promised. SIGKILL is best
 * effort only: a stuck GPU kernel driver can outlive the process deadline. */
int nopen_isolated_open(nopen_isolated_io **out,nopen_backend backend,
                       size_t pool_mib,unsigned timeout_ms,
                       const char *absolute_worker_path);
int nopen_isolated_read(nopen_isolated_io *ctx,uint64_t offset,void *dest,size_t size);
int nopen_isolated_write(nopen_isolated_io *ctx,uint64_t offset,const void *src,size_t size);
int nopen_isolated_stats_get(nopen_isolated_io *ctx,nopen_isolated_stats *out);
void nopen_isolated_close(nopen_isolated_io *ctx);
/* Only mock backend; worker injects one deliberate 10s hang for deadline test. */
int nopen_isolated_inject_hang(nopen_isolated_io *ctx);
/* v0.8.3 mock-only: worker exits abruptly or returns a corrupted header.
 * Both MUST permanently fail-closed; neither may be used with OpenCL. */
int nopen_isolated_inject_crash(nopen_isolated_io *ctx);
int nopen_isolated_inject_bad_reply(nopen_isolated_io *ctx);
#ifdef __cplusplus
}
#endif
#endif
