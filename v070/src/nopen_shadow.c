#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include "nopen_page_core.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <unistd.h>

#ifndef O_DIRECT
#define O_DIRECT 040000
#endif
#define PAGE NOPEN_PAGE_SIZE
#define BLOCK NOPEN_BLK_MAX_BYTES
#define MIB (1024u*1024u)
#define GPU_QUEUE_SLOTS 32u
/* Persistent disk remains authoritative. Bounded GPU uploads are best-effort. */
typedef struct {
    uint64_t offset;
    size_t length;
    uint64_t versions[BLOCK/PAGE];
    unsigned char payload[BLOCK];
} gpu_upload_job;
/* Linux tmpfs is not a durable independent store. */
#define TMPFS_SUPER_MAGIC 0x01021994L
#define BTRFS_SUPER_MAGIC 0x9123683eL
struct nopen_shadow {
    nopen_isolated_io *gpu;
    int fd;
    pthread_mutex_t lock;
    unsigned char *aligned;
    uint64_t *checksums;
    unsigned char *written_bits; /* one bit/page, ~128KiB at 4GiB */
    size_t pages;
    nopen_shadow_stats stats;
    int gpu_active;
    int poisoned;
    /* v0.8 alpha.6 opt-in: unlink page-out acknowledgement from GPU IPC.
     * Acknowledgement still requires the full O_DIRECT|O_DSYNC disk write. */
    int async_gpu,async_started,async_stop,gpu_busy,cv_initialized;
    pthread_t uploader;
    pthread_cond_t upload_cv;
    gpu_upload_job *upload_jobs;
    size_t upload_head,upload_count;
    uint64_t *page_versions;
    unsigned char *gpu_valid_bits;
};
static void gpu_bit(nopen_shadow *s,size_t page,int on) {
    const unsigned char bit=(unsigned char)(1u<<(page&7u));
    if(on)s->gpu_valid_bits[page>>3] |= bit;
    else s->gpu_valid_bits[page>>3] &= (unsigned char)~bit;
}
static int all_gpu_valid(const nopen_shadow *s,size_t first,size_t pages) {
    for(size_t i=0;i<pages;i++)
        if(!(s->gpu_valid_bits[(first+i)>>3] &
             (unsigned char)(1u<<((first+i)&7u))))return 0;
    return 1;
}
static void *gpu_upload_worker(void *arg) {
    nopen_shadow *s=arg;
    pthread_mutex_lock(&s->lock);
    for(;;) {
        while(!s->async_stop && !s->upload_count)
            pthread_cond_wait(&s->upload_cv,&s->lock);
        /* Teardown never drops ACKed data: SSD already stores every write.
         * Pending GPU-cache uploads may safely be discarded on shutdown. */
        if(s->async_stop || !s->gpu_active)break;
        gpu_upload_job *job=&s->upload_jobs[s->upload_head];
        s->gpu_busy=1;
        pthread_mutex_unlock(&s->lock);
        const int rc=nopen_isolated_write(s->gpu,job->offset,
                                          job->payload,job->length);
        pthread_mutex_lock(&s->lock);
        if(rc!=0) {
            s->gpu_active=0;s->stats.gpu_active=0;s->stats.gpu_failures++;
            memset(s->gpu_valid_bits,0,(s->pages+7u)/8u);
            fprintf(stderr,"NOPEN_ASYNC_GPU_FAILOVER rc=%d disk_authority=YES"
                           " kernel_page_data_preserved=YES\n",rc);
        } else {
            for(size_t i=0;i<job->length/PAGE;i++) {
                const size_t page=(size_t)(job->offset/PAGE)+i;
                /* Rewrites race asynchronously. Never mark stale GPU
                 * content valid after a newer disk-backed ACK. */
                if(s->page_versions[page]==job->versions[i])
                    gpu_bit(s,page,1);
            }
            s->stats.gpu_writes++;
        }
        s->upload_head=(s->upload_head+1u)%GPU_QUEUE_SLOTS;
        s->upload_count--;
        s->gpu_busy=0;
    }
    pthread_mutex_unlock(&s->lock);
    return NULL;
}
static void stop_async_worker(nopen_shadow *s) {
    if(!s || !s->async_started)return;
    pthread_mutex_lock(&s->lock);
    s->async_stop=1;
    pthread_cond_signal(&s->upload_cv);
    pthread_mutex_unlock(&s->lock);
    pthread_join(s->uploader,NULL);
    s->async_started=0;
}
static int valid(const nopen_shadow *s,uint64_t off,size_t len) {
    return s && len && len<=BLOCK && (off%PAGE)==0 &&
           (len%PAGE)==0 && off/PAGE<s->pages &&
           len/PAGE<=s->pages-off/PAGE;
}
static int full_pwrite(int fd,const void *src,size_t bytes,off_t offset) {
    const unsigned char *p=src;
    while(bytes) {
        ssize_t rc=pwrite(fd,p,bytes,offset);
        if(rc<0 && errno==EINTR)continue;
        if(rc<=0)return -EIO;
        p+=(size_t)rc;bytes-=(size_t)rc;offset+=(off_t)rc;
        /* Partial O_DIRECT writes cannot be retried with unaligned position. */
        if(bytes && (offset%PAGE)!=0)return -EIO;
    }
    return 0;
}
static int full_pread(int fd,void *dst,size_t bytes,off_t offset) {
    unsigned char *p=dst;
    while(bytes) {
        ssize_t rc=pread(fd,p,bytes,offset);
        if(rc<0 && errno==EINTR)continue;
        if(rc<=0)return -EIO;
        p+=(size_t)rc;bytes-=(size_t)rc;offset+=(off_t)rc;
        if(bytes && (offset%PAGE)!=0)return -EIO;
    }
    return 0;
}
static int check(const nopen_shadow *s,const unsigned char *ptr,
                 size_t first,size_t pages) {
    for(size_t i=0;i<pages;i++)
        if(nopen_page_checksum(ptr+i*PAGE,PAGE)!=s->checksums[first+i])
            return -EIO;
    return 0;
}
int nopen_shadow_open(nopen_shadow **out,nopen_backend backend,
    size_t mib,unsigned timeout_ms,
    const char *worker,const char *path) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(!path || path[0]!='/' || !worker || worker[0]!='/' ||
       (backend!=NOPEN_STORE_MOCK && backend!=NOPEN_STORE_OPENCL) ||
       mib<4 || mib>4096 || timeout_ms<50 || timeout_ms>5000)
        return -EINVAL;
    /* Do not let the shadow live in a Linux /dev/shm /run tmpfs. The file
     * must have storage independent from the RAM we're swapping out. */
    nopen_shadow *s=calloc(1,sizeof(*s));
    if(!s)return -ENOMEM;
    s->fd=-1;
    s->pages=mib*(MIB/PAGE);
    s->checksums=calloc(s->pages,sizeof(*s->checksums));
    s->written_bits=calloc((s->pages+7u)/8u,1);
    if(posix_memalign((void **)&s->aligned,PAGE,BLOCK)!=0)
        s->aligned=NULL;
    if(!s->checksums||!s->written_bits||!s->aligned)goto oom;
    if(pthread_mutex_init(&s->lock,NULL)!=0)goto oom;
    s->fd=open(path,O_CREAT|O_EXCL|O_RDWR|O_DIRECT|O_DSYNC|O_CLOEXEC,0600);
    if(s->fd<0)goto open_error;
    struct statfs fs;
    if(fstatfs(s->fd,&fs)!=0)goto open_error;
    if(fs.f_type==TMPFS_SUPER_MAGIC) {
        /* A RAM-backed shadow is not an independent recovery authority.
         * Never silently downgrade to tmpfs, even for mock tests. */
        fprintf(stderr,"NOPEN_SHADOW_REFUSED reason=tmpfs"
                       " independent_storage_required=YES path=%s\n",path);
        errno=EOPNOTSUPP;
        goto open_error;
    }
    if(fs.f_type==BTRFS_SUPER_MAGIC) {
        /* Btrfs normally COWs overwritten extents. That is unacceptable for
         * the GPU worker's disk backing while Linux is reclaiming memory:
         * set NOCOW on the NEW empty O_EXCL inode before fallocate or data.
         * An isolated, non-snapshotted subvolume must be set up by caller.
         */
        struct stat st;
        int flags=0,checked=0;
        if(fstat(s->fd,&st)!=0 || !S_ISREG(st.st_mode) ||
           st.st_size!=0 || st.st_nlink!=1 ||
           ioctl(s->fd,FS_IOC_GETFLAGS,&flags)!=0) {
            errno=EOPNOTSUPP;
            goto open_error;
        }
        flags |= FS_NOCOW_FL;
        if(ioctl(s->fd,FS_IOC_SETFLAGS,&flags)!=0 ||
           ioctl(s->fd,FS_IOC_GETFLAGS,&checked)!=0 ||
           !(checked & FS_NOCOW_FL) || (checked & FS_COMPR_FL)) {
            fprintf(stderr,"NOPEN_SHADOW_REFUSED reason=btrfs_nocow_missing"
                           " path=%s\n",path);
            errno=EOPNOTSUPP;
            goto open_error;
        }
        fprintf(stderr,"NOPEN_SHADOW_BTRFS_NOCOW_VERIFIED path=%s"
                       " empty_inode_before_allocation=YES"
                       " snapshots_must_be_disabled=YES\n",path);
    }
    const int allocation_rc=posix_fallocate(s->fd,0,(off_t)(mib*MIB));
    if(allocation_rc!=0) {
        /* posix_fallocate returns the error code; it needn't set errno. */
        errno=allocation_rc;
        goto open_error;
    }
    memset(s->aligned,0,BLOCK);
    const uint64_t zero_checksum=nopen_page_checksum(s->aligned,PAGE);
    /* v0.8 alpha.1: one-time 1MiB aligned staging buffer reduces 4GiB
     * disk-shadow initialization from 65536 to 4096 O_DIRECT write calls.
     * Every extent is still initialized BEFORE kernel swap activation:
     * never trade reclaim safety for sparse or lazy allocation. The normal
     * I/O staging buffer remains capped at 64KiB after initialization.
     * If 1MiB allocation is unavailable, retain the proven 64KiB path. */
    unsigned char *init_buffer=NULL;
    const size_t fast_chunk=1024u*1024u;
    if(posix_memalign((void **)&init_buffer,PAGE,fast_chunk)==0)
        memset(init_buffer,0,fast_chunk);
    else init_buffer=NULL;
    const size_t init_chunk=init_buffer?fast_chunk:BLOCK;
    const unsigned char *init_source=init_buffer?init_buffer:s->aligned;
    size_t init_calls=0;
    for(size_t first=0;first<s->pages;first+=init_chunk/PAGE) {
        size_t count=s->pages-first;
        if(count>init_chunk/PAGE)count=init_chunk/PAGE;
        if(full_pwrite(s->fd,init_source,count*PAGE,
                       (off_t)(first*PAGE))!=0) {
            free(init_buffer);
            goto open_error;
        }
        init_calls++;
        if(mib>=1024 && (first+count)%(512u*MIB/PAGE)==0) {
            fprintf(stderr,"NOPEN_V080_SHADOW_INIT_PROGRESS"
                           " completed_mib=%zu total_mib=%zu"
                           " kernel_swap=NOT_YET\n",
                    (first+count)*PAGE/MIB,mib);
            fflush(stderr);
        }
    }
    free(init_buffer);
    if(fdatasync(s->fd)!=0)goto open_error;
    fprintf(stderr,"NOPEN_V080_SHADOW_INIT_BATCH chunk_kib=%zu"
                   " write_calls=%zu pages=%zu"
                   " full_preinitialized=YES lazy_allocation=NO\n",
            init_chunk/1024u,init_calls,s->pages);
    for(size_t i=0;i<s->pages;i++)s->checksums[i]=zero_checksum;
    const int worker_rc=nopen_isolated_open(&s->gpu,backend,mib,timeout_ms,worker);
    if(worker_rc!=0) {
        errno=-worker_rc;
        goto open_error;
    }
    s->gpu_active=1;s->stats.gpu_active=1;
    s->stats.backed_bytes=(uint64_t)mib*MIB;
    const char *opt=getenv("NOPEN_SHADOW_ASYNC_GPU");
    if(opt && strcmp(opt,"1")==0) {
        s->page_versions=calloc(s->pages,sizeof(*s->page_versions));
        s->gpu_valid_bits=calloc((s->pages+7u)/8u,1);
        s->upload_jobs=calloc(GPU_QUEUE_SLOTS,sizeof(*s->upload_jobs));
        if(!s->page_versions || !s->gpu_valid_bits || !s->upload_jobs) {
            errno=ENOMEM;goto open_error;
        }
        if(pthread_cond_init(&s->upload_cv,NULL)!=0) {
            errno=ENOMEM;goto open_error;
        }
        s->cv_initialized=1;
        /* Kernel reclaim must not swap out the swap-server's own metadata.
         * Only the privileged live-swap daemon uses this strict mlock gate.
         * User-space mock / GPU-only tests stay unprivileged. This protects
         * bookkeeping but does NOT guarantee GPU driver forward progress. */
        if(geteuid()==0) {
            if(mlock(s,sizeof(*s))!=0 ||
               mlock(s->aligned,BLOCK)!=0 ||
               mlock(s->checksums,s->pages*sizeof(*s->checksums))!=0 ||
               mlock(s->written_bits,(s->pages+7u)/8u)!=0 ||
               mlock(s->page_versions,
                     s->pages*sizeof(*s->page_versions))!=0 ||
               mlock(s->gpu_valid_bits,(s->pages+7u)/8u)!=0 ||
               mlock(s->upload_jobs,
                     GPU_QUEUE_SLOTS*sizeof(*s->upload_jobs))!=0) {
                fprintf(stderr,"NOPEN_ASYNC_GPU_REFUSED reason=metadata_mlock"
                               " errno=%d kernel_swap=NOT_YET\n",errno);
                goto open_error;
            }
            fprintf(stderr,"NOPEN_ASYNC_GPU_MEMLOCK_PASS"
                           " critical_metadata=LOCKED"
                           " worker_driver_forward_progress=UNPROVEN\n");
        }
        s->async_gpu=1;
        if(pthread_create(&s->uploader,NULL,gpu_upload_worker,s)!=0) {
            errno=EAGAIN;goto open_error;
        }
        s->async_started=1;
        s->stats.async_gpu_mode=1;
        fprintf(stderr,"NOPEN_ASYNC_GPU_READY queue_slots=%u"
                       " disk_ack_durable=YES gpu_upload_not_in_ack_path=YES"
                       " read_fallback=SSD\n",GPU_QUEUE_SLOTS);
    }
    *out=s;
    fprintf(stderr,"NOPEN_SHADOW_READY durable_backend=O_DIRECT_O_DSYNC"
                   " pages=%zu bytes=%llu gpu_active=YES\n",
            s->pages,(unsigned long long)s->stats.backed_bytes);
    return 0;
open_error:
    {int rc=errno?errno:EIO;
     stop_async_worker(s);
     nopen_isolated_close(s->gpu);
     if(s->fd>=0){close(s->fd);unlink(path);}
     if(s->cv_initialized)pthread_cond_destroy(&s->upload_cv);
     pthread_mutex_destroy(&s->lock);
     free(s->upload_jobs);free(s->page_versions);free(s->gpu_valid_bits);
     free(s->aligned);free(s->written_bits);free(s->checksums);free(s);
     return -rc;}
oom:
    free(s->aligned);free(s->written_bits);free(s->checksums);free(s);
    return -ENOMEM;
}
int nopen_shadow_write(nopen_shadow *s,uint64_t off,const void *src,size_t len) {
    if(!src||!valid(s,off,len))return -EINVAL;
    pthread_mutex_lock(&s->lock);
    if(s->poisoned) {pthread_mutex_unlock(&s->lock);return -EIO;}
    /* Persist FIRST; then GPU. A GPU timeout may only remove acceleration,
     * never the authoritative data already written to independent storage. */
    memcpy(s->aligned,src,len);
    if(full_pwrite(s->fd,s->aligned,len,(off_t)off)!=0) {
        s->stats.disk_failures++;s->poisoned=1;s->stats.poisoned=1;
        pthread_mutex_unlock(&s->lock);
        return -EIO;
    }
    size_t first=(size_t)(off/PAGE);
    for(size_t i=0;i<len/PAGE;i++) {
        s->checksums[first+i]=nopen_page_checksum(s->aligned+i*PAGE,PAGE);
        /* Only set AFTER successful O_DSYNC authoritative write. */
        const size_t logical=first+i;
        s->written_bits[logical>>3] |=
            (unsigned char)(1u<<(logical&7u));
    }
    s->stats.writes++;
    if(s->async_gpu) {
        for(size_t i=0;i<len/PAGE;i++) {
            const size_t page=first+i;
            s->page_versions[page]++;
            gpu_bit(s,page,0);
        }
        if(s->gpu_active && s->upload_count<GPU_QUEUE_SLOTS) {
            const size_t tail=(s->upload_head+s->upload_count)%GPU_QUEUE_SLOTS;
            gpu_upload_job *job=&s->upload_jobs[tail];
            job->offset=off;job->length=len;
            memcpy(job->payload,src,len);
            for(size_t i=0;i<len/PAGE;i++)
                job->versions[i]=s->page_versions[first+i];
            s->upload_count++;
            s->stats.async_enqueued++;
            pthread_cond_signal(&s->upload_cv);
        } else {
            /* If GPU cannot keep up, preserve correctness and latency:
             * only skip redundant cache upload, NEVER the disk write. */
            s->stats.async_dropped++;
        }
        if(s->stats.writes%8192u==0)
            fprintf(stderr,"NOPEN_ASYNC_SWAP_STATS writes=%llu"
                           " enqueued=%llu skipped=%llu gpu_completed=%llu"
                           " gpu_failures=%llu disk_failures=%llu\n",
                    (unsigned long long)s->stats.writes,
                    (unsigned long long)s->stats.async_enqueued,
                    (unsigned long long)s->stats.async_dropped,
                    (unsigned long long)s->stats.gpu_writes,
                    (unsigned long long)s->stats.gpu_failures,
                    (unsigned long long)s->stats.disk_failures);
        pthread_mutex_unlock(&s->lock);
        return 0;
    }
    if(s->gpu_active) {
        if(nopen_isolated_write(s->gpu,off,src,len)!=0) {
            s->gpu_active=0;s->stats.gpu_active=0;s->stats.gpu_failures++;
            fprintf(stderr,"NOPEN_SHADOW_FAILOVER op=WRITE offset=%llu"
                    " bytes=%zu durable_copy=YES linux_io_error=NO\n",
                    (unsigned long long)off,len);
        } else s->stats.gpu_writes++;
    }
    pthread_mutex_unlock(&s->lock);
    return 0;
}
int nopen_shadow_read(nopen_shadow *s,uint64_t off,void *dst,size_t len) {
    if(!dst||!valid(s,off,len))return -EINVAL;
    pthread_mutex_lock(&s->lock);
    if(s->poisoned){pthread_mutex_unlock(&s->lock);return -EIO;}
    int gpu_ok=0;
    const size_t first=(size_t)(off/PAGE);
    /* v0.8 alpha.1: a range with no acknowledged writes is logically all
     * zero. Return the known value without GPU IPC, SSD page-in, or reclaim
     * dependency. Do NOT infer this from checksums (hash collisions); a
     * dedicated residency bit is set only after the disk write succeeds. */
    int never_written=1;
    for(size_t i=0;i<len/PAGE;i++) {
        const size_t logical=first+i;
        if(s->written_bits[logical>>3] &
           (unsigned char)(1u<<(logical&7u))) {
            never_written=0;
            break;
        }
    }
    if(never_written) {
        memset(dst,0,len);
        s->stats.reads++;
        s->stats.zero_reads++;
        pthread_mutex_unlock(&s->lock);
        return 0;
    }
    if(s->gpu_active &&
       (!s->async_gpu ||
        (!s->gpu_busy && !s->upload_count &&
         all_gpu_valid(s,first,len/PAGE)))) {
        const int gpu_rc=nopen_isolated_read(s->gpu,off,s->aligned,len);
        const int mismatch=gpu_rc==0 &&
                           check(s,s->aligned,first,len/PAGE)!=0;
        if(gpu_rc==0 && !mismatch) {
            gpu_ok=1;
            s->stats.gpu_reads++;
        } else {
            s->gpu_active=0;s->stats.gpu_active=0;
            s->stats.gpu_failures++;
            if(mismatch)s->stats.integrity_mismatches++;
            fprintf(stderr,"NOPEN_SHADOW_FAILOVER op=READ offset=%llu"
                    " bytes=%zu gpu_error=%d integrity_mismatch=%d"
                    " durable_copy=YES\n",
                    (unsigned long long)off,len,gpu_rc,mismatch);
        }
    }
    if(!gpu_ok) {
        if(s->async_gpu)s->stats.async_disk_reads++;
        if(full_pread(s->fd,s->aligned,len,(off_t)off)!=0 ||
           check(s,s->aligned,first,len/PAGE)!=0) {
            s->stats.disk_failures++;s->poisoned=1;s->stats.poisoned=1;
            pthread_mutex_unlock(&s->lock);
            return -EIO;
        }
        s->stats.shadow_reads++;
    }
    memcpy(dst,s->aligned,len);
    s->stats.reads++;
    pthread_mutex_unlock(&s->lock);
    return 0;
}
int nopen_shadow_get_stats(nopen_shadow *s,nopen_shadow_stats *out) {
    if(!s||!out)return -EINVAL;
    pthread_mutex_lock(&s->lock);
    *out=s->stats;
    pthread_mutex_unlock(&s->lock);
    return 0;
}
int nopen_shadow_mock_inject_gpu_hang(nopen_shadow *s) {
    if(!s)return -EINVAL;
    pthread_mutex_lock(&s->lock);
    if(!s->gpu_active){pthread_mutex_unlock(&s->lock);return -EIO;}
    if(s->async_gpu){pthread_mutex_unlock(&s->lock);return -EOPNOTSUPP;}
    const int rc=nopen_isolated_inject_hang(s->gpu);
    if(rc!=0) {
        s->gpu_active=0;s->stats.gpu_active=0;s->stats.gpu_failures++;
    }
    pthread_mutex_unlock(&s->lock);
    return rc;
}
int nopen_shadow_simulate_gpu_loss(nopen_shadow *s) {
    if(!s)return -EINVAL;
    stop_async_worker(s);
    pthread_mutex_lock(&s->lock);
    if(s->poisoned) {
        pthread_mutex_unlock(&s->lock);
        return -EIO;
    }
    if(s->gpu_active) {
        s->gpu_active=0;
        s->stats.gpu_active=0;
        s->stats.gpu_failures++;
        /* Explicit worker termination, NOT a GPU firmware/kernel reset. */
        nopen_isolated_close(s->gpu);
        s->gpu=NULL;
        fprintf(stderr,"NOPEN_SHADOW_SIMULATED_LOSS "
                       "worker_terminated=YES physical_gpu_reset=NO "
                       "durable_copy=YES\n");
    }
    pthread_mutex_unlock(&s->lock);
    return 0;
}
void nopen_shadow_close(nopen_shadow *s) {
    if(!s)return;
    stop_async_worker(s);
    pthread_mutex_lock(&s->lock);
    nopen_isolated_close(s->gpu);
    if(s->fd>=0)close(s->fd);
    pthread_mutex_unlock(&s->lock);
    if(s->cv_initialized)pthread_cond_destroy(&s->upload_cv);
    pthread_mutex_destroy(&s->lock);
    free(s->upload_jobs);free(s->page_versions);free(s->gpu_valid_bits);
    free(s->aligned);free(s->written_bits);free(s->checksums);free(s);
}
