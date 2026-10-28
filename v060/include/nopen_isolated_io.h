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
    int child_alive;
} nopen_isolated_stats;
/* Linux-only process isolation: parent owns the kernel-block callback while a
 * worker process owns the OpenCL context. Every operation has a 50..5000 ms
 * response deadline. Timeout -> worker SIGKILL (no blocking waitpid) and
 * permanently fail-closed. No transparent swap and no recovery after crash.
 * A stuck kernel GPU driver cannot be reset by SIGKILL: this isolates the
 * ublk callback's response deadline, NOT kernel liveness. */
int nopen_isolated_open(nopen_isolated_io **out,nopen_backend backend,
                       size_t pool_mib,unsigned timeout_ms);
int nopen_isolated_read(nopen_isolated_io *ctx,uint64_t offset,void *dest,size_t size);
int nopen_isolated_write(nopen_isolated_io *ctx,uint64_t offset,const void *src,size_t size);
int nopen_isolated_stats_get(nopen_isolated_io *ctx,nopen_isolated_stats *out);
void nopen_isolated_close(nopen_isolated_io *ctx);
/* Only mock backend; worker injects one deliberate 10s hang for deadline test. */
int nopen_isolated_inject_hang(nopen_isolated_io *ctx);
#ifdef __cplusplus
}
#endif
#endif
