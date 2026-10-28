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
#include <linux/fs.h>
#include <unistd.h>

#ifndef O_DIRECT
#define O_DIRECT 040000
#endif
#define PAGE NOPEN_PAGE_SIZE
#define BLOCK NOPEN_BLK_MAX_BYTES
#define MIB (1024u*1024u)
/* Linux tmpfs is not a durable independent store. */
#define TMPFS_SUPER_MAGIC 0x01021994L
#define BTRFS_SUPER_MAGIC 0x9123683eL
struct nopen_shadow {
    nopen_isolated_io *gpu;
    int fd;
    pthread_mutex_t lock;
    unsigned char *aligned;
    uint64_t *checksums;
    size_t pages;
    nopen_shadow_stats stats;
    int gpu_active;
    int poisoned;
};
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
    if(posix_memalign((void **)&s->aligned,PAGE,BLOCK)!=0)
        s->aligned=NULL;
    if(!s->checksums||!s->aligned)goto oom;
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
    /* Convert all preallocated, unwritten extents to initialized extents
     * BEFORE activating swap. No lazy allocation on page fault. */
    for(size_t first=0;first<s->pages;first+=BLOCK/PAGE) {
        size_t count=s->pages-first;
        if(count>BLOCK/PAGE)count=BLOCK/PAGE;
        if(full_pwrite(s->fd,s->aligned,count*PAGE,
                       (off_t)(first*PAGE))!=0)
            goto open_error;
    }
    if(fdatasync(s->fd)!=0)goto open_error;
    for(size_t i=0;i<s->pages;i++)s->checksums[i]=zero_checksum;
    const int worker_rc=nopen_isolated_open(&s->gpu,backend,mib,timeout_ms,worker);
    if(worker_rc!=0) {
        errno=-worker_rc;
        goto open_error;
    }
    s->gpu_active=1;s->stats.gpu_active=1;
    s->stats.backed_bytes=(uint64_t)mib*MIB;
    *out=s;
    fprintf(stderr,"NOPEN_SHADOW_READY durable_backend=O_DIRECT_O_DSYNC"
                   " pages=%zu bytes=%llu gpu_active=YES\n",
            s->pages,(unsigned long long)s->stats.backed_bytes);
    return 0;
open_error:
    {int rc=errno?errno:EIO;
     nopen_isolated_close(s->gpu);
     if(s->fd>=0){close(s->fd);unlink(path);}
     pthread_mutex_destroy(&s->lock);
     free(s->aligned);free(s->checksums);free(s);
     return -rc;}
oom:
    free(s->aligned);free(s->checksums);free(s);
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
    for(size_t i=0;i<len/PAGE;i++)
        s->checksums[first+i]=nopen_page_checksum(s->aligned+i*PAGE,PAGE);
    s->stats.writes++;
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
    if(s->gpu_active) {
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
    const int rc=nopen_isolated_inject_hang(s->gpu);
    if(rc!=0) {
        s->gpu_active=0;s->stats.gpu_active=0;s->stats.gpu_failures++;
    }
    pthread_mutex_unlock(&s->lock);
    return rc;
}
int nopen_shadow_simulate_gpu_loss(nopen_shadow *s) {
    if(!s)return -EINVAL;
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
    pthread_mutex_lock(&s->lock);
    nopen_isolated_close(s->gpu);
    if(s->fd>=0)close(s->fd);
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_destroy(&s->lock);
    free(s->aligned);free(s->checksums);free(s);
}
