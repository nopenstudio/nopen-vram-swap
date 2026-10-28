#include "nopen_recovery.h"
#include "nopen_page_core.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PAGE NOPEN_PAGE_SIZE
#define MAX_IO NOPEN_BLK_MAX_BYTES
#define MIB (1024u * 1024u)

/* The CPU mirror is the authoritative, last-acknowledged state whenever
 * mirror mode is enabled. This intentionally CANCELS any RAM savings.
 * No GPU reset is attempted; the backing store is discarded on device loss.
 * All public operations and state transitions serialize via a mutex. */
struct nopen_recovery {
    nopen_blk_backend *gpu;
    unsigned char *mirror;
    uint64_t *checksums;
    unsigned char *scratch;
    size_t pages;
    nopen_backend backend;
    nopen_recovery_mode mode;
    nopen_recovery_stats stats;
    pthread_mutex_t mutex;
    size_t injected_rebuild_failure_after_pages; /* MOCK-only; zero disables */
};

static int bounds(const nopen_recovery *r,uint64_t off,size_t bytes) {
    if(!r || !bytes || bytes>MAX_IO ||
       (off%PAGE)!=0 || (bytes%PAGE)!=0 ||
       off/PAGE >= r->pages || bytes/PAGE > r->pages-off/PAGE)
        return -EINVAL;
    return 0;
}
static void gpu_lost(nopen_recovery *r) {
    if(r->stats.state!=NOPEN_RECOVERY_GPU_ACTIVE)return;
    r->stats.device_loss_events++;
    r->stats.state=r->mode==NOPEN_RECOVERY_HOST_MIRROR?
        NOPEN_RECOVERY_CPU_FALLBACK:NOPEN_RECOVERY_FAILED_CLOSED;
}
static int check_mirror(nopen_recovery *r,size_t first,size_t count) {
    for(size_t i=0;i<count;i++) {
        const unsigned char *p=r->mirror+(first+i)*PAGE;
        if(nopen_page_checksum(p,PAGE)!=r->checksums[first+i]) {
            r->stats.integrity_failures++;
            r->stats.state=NOPEN_RECOVERY_FAILED_CLOSED;
            return -EIO;
        }
    }
    return 0;
}
int nopen_recovery_open_bounded(nopen_recovery **out,nopen_backend backend,
                        size_t capacity_mib,nopen_recovery_mode mode,
                        size_t max_host_bytes) {
    if(!out)return -EINVAL;
    *out=NULL;
    if((backend!=NOPEN_STORE_OPENCL && backend!=NOPEN_STORE_MOCK) ||
       (mode!=NOPEN_RECOVERY_NO_MIRROR && mode!=NOPEN_RECOVERY_HOST_MIRROR) ||
       capacity_mib<4 || capacity_mib>64) return -EINVAL;
    const size_t page_count=capacity_mib*(MIB/PAGE);
    const size_t mirror_bytes=mode==NOPEN_RECOVERY_HOST_MIRROR?
        page_count*PAGE:0;
    const size_t checksum_bytes=mode==NOPEN_RECOVERY_HOST_MIRROR?
        page_count*sizeof(uint64_t):0;
    const size_t known_budget=sizeof(nopen_recovery)+MAX_IO+
        mirror_bytes+checksum_bytes;
    if(known_budget>max_host_bytes)return -ENOMEM;
    nopen_recovery *r=calloc(1,sizeof(*r));
    if(!r)return -ENOMEM;
    r->pages=capacity_mib*(MIB/PAGE);
    r->backend=backend;
    r->mode=mode;
    r->stats.state=NOPEN_RECOVERY_GPU_ACTIVE;
    r->scratch=malloc(MAX_IO);
    if(mode==NOPEN_RECOVERY_HOST_MIRROR) {
        /* Mirror is preallocated, never faulted in under GPU failure.
         * Actual anonymous memory may be lazily committed, but every page is
         * touched by calloc + the loop below before opening the GPU. */
        r->mirror=calloc(r->pages,PAGE);
        r->checksums=malloc(r->pages*sizeof(*r->checksums));
        if(r->mirror && r->checksums) {
            const unsigned char zero[PAGE]={0};
            const uint64_t digest=nopen_page_checksum(zero,PAGE);
            for(size_t i=0;i<r->pages;i++) {
                r->mirror[i*PAGE]=0;
                r->checksums[i]=digest;
            }
            r->stats.mirror_bytes=r->pages*PAGE;
        }
    }
    if(!r->scratch ||
       (mode==NOPEN_RECOVERY_HOST_MIRROR && (!r->mirror||!r->checksums))) {
        free(r->checksums);free(r->mirror);free(r->scratch);free(r);
        return -ENOMEM;
    }
    if(pthread_mutex_init(&r->mutex,NULL)!=0) {
        free(r->checksums);free(r->mirror);free(r->scratch);free(r);
        return -ENOMEM;
    }
    if(nopen_blk_open(&r->gpu,backend,capacity_mib)!=0) {
        pthread_mutex_destroy(&r->mutex);
        free(r->checksums);free(r->mirror);free(r->scratch);free(r);
        return -EIO;
    }
    *out=r;
    return 0;
}

int nopen_recovery_open(nopen_recovery **out,nopen_backend backend,
                        size_t capacity_mib,nopen_recovery_mode mode) {
    return nopen_recovery_open_bounded(out,backend,capacity_mib,mode,SIZE_MAX);
}
int nopen_recovery_device_lost(nopen_recovery *r) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    gpu_lost(r);
    const int rc=r->stats.state==NOPEN_RECOVERY_FAILED_CLOSED?-EIO:0;
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
int nopen_recovery_inject_io_failure(nopen_recovery *r,size_t operation) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    if(r->backend!=NOPEN_STORE_MOCK ||
       r->stats.state!=NOPEN_RECOVERY_GPU_ACTIVE) {
        pthread_mutex_unlock(&r->mutex);
        return -EPERM;
    }
    nopen_blk_inject_failure(r->gpu,operation);
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
/* Mock-only corrupt a full-mirror byte without updating its checksum. The
 * next fallback/read MUST fail closed rather than expose corrupted bytes. */
int nopen_recovery_inject_mirror_corruption(nopen_recovery *r,size_t page) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    int rc=0;
    if(r->backend!=NOPEN_STORE_MOCK || !r->mirror || page>=r->pages)
        rc=-EPERM;
    else r->mirror[page*PAGE]^=0x5Au;
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
int nopen_recovery_read(nopen_recovery *r,uint64_t off,void *out,size_t len) {
    if(!out || bounds(r,off,len)!=0)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    const size_t first=(size_t)(off/PAGE),count=len/PAGE;
    if(r->stats.state==NOPEN_RECOVERY_FAILED_CLOSED) {
        pthread_mutex_unlock(&r->mutex);
        return -EIO;
    }
    int rc=0;
    if(r->stats.state==NOPEN_RECOVERY_GPU_ACTIVE) {
        rc=nopen_blk_read(r->gpu,off,r->scratch,len);
        if(rc!=0) {
            r->stats.gpu_read_errors++;
            gpu_lost(r);
        } else if(r->mirror && check_mirror(r,first,count)==0) {
            /* Compare all bytes against last-acknowledged CPU mirror; even a
             * valid GPU checksum is not proof it is the correct generation. */
            if(memcmp(r->scratch,r->mirror+first*PAGE,len)!=0) {
                r->stats.integrity_failures++;
                gpu_lost(r);
            }
        } else if(r->mirror) {
            /* Corrupt authoritative mirror means neither source is trusted. */
            r->stats.state=NOPEN_RECOVERY_FAILED_CLOSED;
        }
    }
    if(r->stats.state==NOPEN_RECOVERY_CPU_FALLBACK) {
        if(check_mirror(r,first,count)==0) {
            memcpy(r->scratch,r->mirror+first*PAGE,len);
            r->stats.fallback_reads++;
        }
    }
    if(r->stats.state==NOPEN_RECOVERY_FAILED_CLOSED)rc=-EIO;
    else {memcpy(out,r->scratch,len);r->stats.reads++;rc=0;}
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
int nopen_recovery_write(nopen_recovery *r,uint64_t off,const void *src,size_t len) {
    if(!src || bounds(r,off,len)!=0)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    const size_t first=(size_t)(off/PAGE),count=len/PAGE;
    if(r->stats.state==NOPEN_RECOVERY_FAILED_CLOSED) {
        pthread_mutex_unlock(&r->mutex);
        return -EIO;
    }
    int rc=0;
    if(r->stats.state==NOPEN_RECOVERY_GPU_ACTIVE) {
        rc=nopen_blk_write(r->gpu,off,src,len);
        if(rc!=0) {
            r->stats.gpu_write_errors++;
            gpu_lost(r);
        }
    }
    if(r->stats.state==NOPEN_RECOVERY_CPU_FALLBACK) {
        /* CPU source remains authoritative. On failed GPU write, the new data
         * can be ACKed only because the complete mirror exists and is checked.
         * No use of GPU storage after this transition. */
        if(check_mirror(r,first,count)!=0)rc=-EIO;
        else {r->stats.fallback_writes++;rc=0;}
    }
    if(r->stats.state==NOPEN_RECOVERY_FAILED_CLOSED)rc=-EIO;
    if(rc==0) {
        if(r->mirror) {
            memcpy(r->mirror+first*PAGE,src,len);
            for(size_t i=0;i<count;i++)
                r->checksums[first+i]=nopen_page_checksum(
                    (const unsigned char *)src+i*PAGE,PAGE);
        }
        r->stats.writes++;
    }
    pthread_mutex_unlock(&r->mutex);
    return rc;
}

/* Two-phase fail-closed repair. The CPU mirror is authoritative, and a brand
 * new device backend becomes active only after ALL pages have been written,
 * read back and checksum compared against the mirror. The old backend remains
 * untouched until the rebuilt one has passed verification. Holding the mutex
 * serializes application I/O; kernel/driver calls can still block forever if
 * the device is broken, so this requires out-of-process supervision. */
int nopen_recovery_inject_rebuild_failure(nopen_recovery *r,size_t pages) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    if(r->backend!=NOPEN_STORE_MOCK) {
        pthread_mutex_unlock(&r->mutex);
        return -EPERM;
    }
    r->injected_rebuild_failure_after_pages=pages;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int nopen_recovery_rebuild_gpu(nopen_recovery *r) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    if(!r->mirror || r->stats.state!=NOPEN_RECOVERY_CPU_FALLBACK) {
        pthread_mutex_unlock(&r->mutex);
        return -EPERM;
    }
    r->stats.rebuild_attempts++;
    /* Read and verify ALL authoritative pages before trying GPU allocation.
     * This is costly, deliberate, and avoids claiming recovery of bad data. */
    if(check_mirror(r,0,r->pages)!=0) {
        r->stats.rebuild_failures++;
        pthread_mutex_unlock(&r->mutex);
        return -EIO;
    }
    nopen_blk_backend *fresh=NULL;
    const size_t capacity_mib=r->pages/(MIB/PAGE);
    if(nopen_blk_open(&fresh,r->backend,capacity_mib)!=0) {
        r->stats.rebuild_failures++;
        pthread_mutex_unlock(&r->mutex);
        return -EIO;
    }
    size_t verified=0;
    int rc=0;
    while(verified<r->pages) {
        const size_t pages=(r->pages-verified)>16u?16u:r->pages-verified;
        const uint64_t off=(uint64_t)verified*PAGE;
        const size_t bytes=pages*PAGE;
        if(nopen_blk_write(fresh,off,r->mirror+verified*PAGE,bytes)!=0 ||
           nopen_blk_read(fresh,off,r->scratch,bytes)!=0 ||
           memcmp(r->scratch,r->mirror+verified*PAGE,bytes)!=0) {
            rc=-EIO;
            break;
        }
        verified+=pages;
        r->stats.rebuild_verified_pages+=pages;
        if(r->backend==NOPEN_STORE_MOCK &&
           r->injected_rebuild_failure_after_pages &&
           verified>=r->injected_rebuild_failure_after_pages) {
            rc=-EIO;
            break;
        }
    }
    r->injected_rebuild_failure_after_pages=0;
    if(rc==0) {
        nopen_blk_backend *old=r->gpu;
        r->gpu=fresh;
        r->stats.state=NOPEN_RECOVERY_GPU_ACTIVE;
        r->stats.rebuild_successes++;
        /* Old backend cleanup may hang on real broken drivers. No guarantee of
         * kernel-level deadline or reset completion is made here. */
        nopen_blk_close(old);
    } else {
        r->stats.rebuild_failures++;
        nopen_blk_close(fresh);
        /* Stay in healthy CPU fallback after partial failed GPU repair. */
    }
    pthread_mutex_unlock(&r->mutex);
    return rc;
}
int nopen_recovery_get_stats(nopen_recovery *r,nopen_recovery_stats *out) {
    if(!r || !out)return -EINVAL;
    pthread_mutex_lock(&r->mutex);
    *out=r->stats;
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
void nopen_recovery_close(nopen_recovery *r) {
    if(!r)return;
    pthread_mutex_lock(&r->mutex);
    nopen_blk_close(r->gpu);
    r->gpu=NULL;
    pthread_mutex_unlock(&r->mutex);
    pthread_mutex_destroy(&r->mutex);
    free(r->checksums);free(r->mirror);free(r->scratch);free(r);
}
