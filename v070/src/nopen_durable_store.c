#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/* IE-03: transactional COW disk prototype, NOT a kernel SWAP backend.
 * Data and journal are independent files on ext4/XFS. A transaction writes
 * all inactive page slots, fdatasyncs data, then publishes ONE checksummed
 * journal record and fdatasyncs journal BEFORE returning success.
 * This is NOT certified for GPU/reclaim/ublk, power-cut, encryption or
 * untrusted hardware. No GC or bounded-latency runtime guarantee yet.
 */
#include "nopen_durable_store.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <linux/magic.h>
#include <unistd.h>

enum { PAGE=4096, MAX_PAGES_IO=16, MAX_TXN=65536 };
#define MIB UINT64_C(1048576)
#define HDR_MAGIC UINT64_C(0x4e4f50454e443331)
#define LOG_MAGIC UINT64_C(0x4e4f50454e545831)
#define VERSION UINT64_C(1)

typedef struct {uint64_t generation,digest;unsigned char slot,valid;} page_meta;
typedef struct {
    uint64_t magic,version,boot_epoch,pages,record_bytes,seal;
} disk_header;
typedef struct {
    uint64_t magic,request_id,sequence,first_page,count,payload_digest;
    uint64_t generations[MAX_PAGES_IO],digests[MAX_PAGES_IO];
    uint64_t seal;
} journal_record;

struct nopen_durable_store {
    int data_fd,log_fd;
    uint64_t epoch;
    size_t pages;
    size_t record_count;
    page_meta *map;
    unsigned char *scratch;
    int poisoned,inject,lock_initialized;
    pthread_mutex_t lock;
};

static uint64_t hash_bytes(const void *ptr,size_t bytes) {
    const unsigned char *p=ptr;
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<bytes;i++) {
        h^=(uint64_t)p[i];
        h*=UINT64_C(1099511628211);
    }
    return h;
}
static int write_full(int fd,const void *src,size_t n,off_t offset) {
    const unsigned char *p=src;
    while(n) {
        ssize_t k=pwrite(fd,p,n,offset);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)return -(errno?errno:EIO);
        p+=(size_t)k;n-=(size_t)k;offset+=(off_t)k;
    }
    return 0;
}
static int read_full(int fd,void *dest,size_t n,off_t offset) {
    unsigned char *p=dest;
    while(n) {
        ssize_t k=pread(fd,p,n,offset);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)return -EIO;
        p+=(size_t)k;n-=(size_t)k;offset+=(off_t)k;
    }
    return 0;
}
static int sync_fd(int fd) {
    return fdatasync(fd)==0?0:-(errno?errno:EIO);
}
static int disk_path(char *dst,size_t n,const char *base,const char *suffix) {
    if(!base || base[0]!='/' || strchr(base,'\n'))return -EINVAL;
    int count=snprintf(dst,n,"%s%s",base,suffix);
    if(count<0 || (size_t)count>=n)return -ENAMETOOLONG;
    return 0;
}
static int check_fs(int fd) {
    struct statfs fs;
    if(fstatfs(fd,&fs)!=0)return -EIO;
    /* Btrfs without preconfigured NOCOW can relocate/reallocate extents
     * under reclaim. No silent tmpfs, overlay, FUSE or COW fallback. */
    if((unsigned long)fs.f_type!=(unsigned long)EXT4_SUPER_MAGIC &&
       (unsigned long)fs.f_type!=(unsigned long)XFS_SUPER_MAGIC)
        return -EOPNOTSUPP;
    return 0;
}
static int sync_parent(const char *path) {
    char dir[PATH_MAX];
    size_t n=strlen(path);
    if(n>=sizeof dir)return -ENAMETOOLONG;
    memcpy(dir,path,n+1);
    char *slash=strrchr(dir,'/');
    if(!slash)return -EINVAL;
    if(slash==dir)slash[1]='\0';
    else *slash='\0';
    int fd=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return -errno;
    int rc=fsync(fd)==0?0:-errno;
    close(fd);
    return rc;
}
static off_t slot_offset(size_t page,unsigned slot) {
    return (off_t)(((uint64_t)page*2u+(uint64_t)slot)*(uint64_t)PAGE);
}
static off_t record_offset(size_t idx) {
    return (off_t)(sizeof(disk_header)+(uint64_t)idx*sizeof(journal_record));
}
static int valid_range(const nopen_durable_store *s,uint64_t off,size_t len) {
    if(!s || !len || len>MAX_PAGES_IO*PAGE || (len%PAGE)!=0 ||
       (off%PAGE)!=0 || off/PAGE >= s->pages ||
       len/PAGE > s->pages-off/PAGE)return -EINVAL;
    return 0;
}
static int apply(nopen_durable_store *s,const journal_record *r) {
    if(r->magic!=LOG_MAGIC || r->sequence!=s->record_count+1 ||
       !r->request_id || r->count<1 || r->count>MAX_PAGES_IO ||
       r->first_page >= s->pages || r->count > s->pages-r->first_page ||
       r->seal!=hash_bytes(r,offsetof(journal_record,seal)))
        return -EBADMSG;
    for(size_t i=0;i<(size_t)r->count;i++) {
        const page_meta *m=&s->map[r->first_page+i];
        if(r->generations[i]!=m->generation+1)return -EBADMSG;
    }
    for(size_t i=0;i<(size_t)r->count;i++) {
        page_meta *m=&s->map[r->first_page+i];
        m->slot=m->valid?(unsigned char)(1u-m->slot):0;
        m->valid=1;
        m->generation=r->generations[i];
        m->digest=r->digests[i];
    }
    s->record_count++;
    return 0;
}
/* The log is append-only and finite. An invalid WHOLE record is fatal: it
 * could represent an ACK that must not be quietly forgotten. A partial
 * trailing record was never eligible for ACK and can be truncated. */
static int replay(nopen_durable_store *s) {
    struct stat st;
    if(fstat(s->log_fd,&st)!=0 || st.st_size<(off_t)sizeof(disk_header))
        return -EIO;
    off_t len=st.st_size-(off_t)sizeof(disk_header);
    size_t count=(size_t)(len/(off_t)sizeof(journal_record));
    if(count>MAX_TXN)return -EFBIG;
    for(size_t i=0;i<count;i++) {
        journal_record r;
        int rc=read_full(s->log_fd,&r,sizeof r,record_offset(i));
        if(rc)return rc;
        rc=apply(s,&r);
        if(rc)return rc;
    }
    /* Validate only LIVE slots. Older slots may have been reused by later
     * successfully committed or incomplete COW transactions. */
    for(size_t i=0;i<s->pages;i++) {
        if(!s->map[i].valid)continue;
        int rc=read_full(s->data_fd,s->scratch,PAGE,
                         slot_offset(i,s->map[i].slot));
        if(rc)return rc;
        if(hash_bytes(s->scratch,PAGE)!=s->map[i].digest)return -EBADMSG;
    }
    if(len%(off_t)sizeof(journal_record)) {
        if(ftruncate(s->log_fd,record_offset(count))!=0)return -errno;
        int rc=sync_fd(s->log_fd);
        if(rc)return rc;
    }
    return 0;
}
void nopen_durable_close(nopen_durable_store *s) {
    if(!s)return;
    if(s->log_fd>=0)close(s->log_fd);
    if(s->data_fd>=0)close(s->data_fd);
    if(s->lock_initialized)pthread_mutex_destroy(&s->lock);
    free(s->map);free(s->scratch);free(s);
}
int nopen_durable_open(nopen_durable_store **out,const char *base,
                      unsigned mib,uint64_t epoch,int create_new) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(mib<4 || mib>4096 || epoch==0 || (create_new!=0 && create_new!=1))
        return -EINVAL;
    char data_path[PATH_MAX],log_path[PATH_MAX];
    int rc=disk_path(data_path,sizeof data_path,base,".txdata");
    if(rc)return rc;
    rc=disk_path(log_path,sizeof log_path,base,".txlog");
    if(rc)return rc;
    nopen_durable_store *s=calloc(1,sizeof(*s));
    if(!s)return -ENOMEM;
    s->data_fd=-1;s->log_fd=-1;
    s->pages=(size_t)mib*(size_t)(MIB/PAGE);
    s->epoch=epoch;
    s->map=calloc(s->pages,sizeof(*s->map));
    s->scratch=malloc(MAX_PAGES_IO*PAGE);
    if(!s->map || !s->scratch) {rc=-ENOMEM;goto fail;}
    memset(s->scratch,0,MAX_PAGES_IO*PAGE);
    if(pthread_mutex_init(&s->lock,NULL)!=0){rc=-EIO;goto fail;}
    s->lock_initialized=1;
    const int file_flags=O_RDWR|O_CLOEXEC|O_NOFOLLOW|
                         (create_new?(O_CREAT|O_EXCL):0);
    s->data_fd=open(data_path,file_flags,0600);
    if(s->data_fd<0){rc=-errno;goto fail;}
    rc=check_fs(s->data_fd);
    if(rc)goto fail;
    s->log_fd=open(log_path,file_flags,0600);
    if(s->log_fd<0){rc=-errno;goto fail;}
    if(flock(s->log_fd,LOCK_EX|LOCK_NB)!=0){rc=-EBUSY;goto fail;}
    if(create_new) {
        rc=posix_fallocate(s->data_fd,0,(off_t)((uint64_t)mib*2u*MIB));
        if(rc){rc=-rc;goto fail;}
        disk_header header={HDR_MAGIC,VERSION,epoch,s->pages,
                            sizeof(journal_record),0};
        header.seal=hash_bytes(&header,offsetof(disk_header,seal));
        rc=write_full(s->log_fd,&header,sizeof header,0);
        if(rc)goto fail;
        rc=sync_fd(s->data_fd);
        if(rc)goto fail;
        rc=sync_fd(s->log_fd);
        if(rc)goto fail;
        rc=sync_parent(log_path);
        if(rc)goto fail;
    } else {
        disk_header header;
        rc=read_full(s->log_fd,&header,sizeof header,0);
        if(rc)goto fail;
        if(header.magic!=HDR_MAGIC || header.version!=VERSION ||
           header.boot_epoch!=epoch || header.pages!=s->pages ||
           header.record_bytes!=sizeof(journal_record) ||
           header.seal!=hash_bytes(&header,offsetof(disk_header,seal))) {
            rc=-EPERM;goto fail;
        }
        struct stat st;
        if(fstat(s->data_fd,&st)!=0 ||
           st.st_size!=(off_t)((uint64_t)mib*2u*MIB)) {
            rc=-EIO;goto fail;
        }
        rc=replay(s);
        if(rc)goto fail;
    }
    *out=s;
    fprintf(stderr,"NOPEN_DURABLE_OPEN create=%d mib=%u"
                   " committed_txns=%zu actual_disk_commit=YES"
                   " ublk_recovery=NOT_IMPLEMENTED kernel_swap=BLOCKED\n",
            create_new,mib,s->record_count);
    return 0;
fail:
    nopen_durable_close(s);
    /* If the first create failed, never expose an incomplete empty backend.
     * No deletion is performed on an existing recovery candidate. */
    if(create_new){unlink(log_path);unlink(data_path);}
    return rc;
}
static int find_duplicate(nopen_durable_store *s,uint64_t id,
                          uint64_t first,uint64_t count,uint64_t hash) {
    for(size_t i=0;i<s->record_count;i++) {
        journal_record r;
        int rc=read_full(s->log_fd,&r,sizeof r,record_offset(i));
        if(rc)return rc;
        if(r.request_id!=id)continue;
        return (r.first_page==first && r.count==count &&
                r.payload_digest==hash)?1:-EEXIST;
    }
    return 0;
}
int nopen_durable_write(nopen_durable_store *s,uint64_t request_id,
                        uint64_t off,const void *src,size_t bytes) {
    if(!src || !request_id || valid_range(s,off,bytes)!=0)return -EINVAL;
    pthread_mutex_lock(&s->lock);
    int rc=-EIO;
    if(s->poisoned)goto out;
    const size_t first=(size_t)(off/PAGE),count=bytes/PAGE;
    const uint64_t payload_hash=hash_bytes(src,bytes);
    rc=find_duplicate(s,request_id,first,count,payload_hash);
    if(rc!=0){if(rc==1)rc=0;goto out;}
    if(s->record_count>=MAX_TXN){rc=-ENOSPC;goto out;}
    journal_record r={0};
    r.magic=LOG_MAGIC;r.request_id=request_id;
    r.sequence=s->record_count+1;r.first_page=first;
    r.count=count;r.payload_digest=payload_hash;
    for(size_t i=0;i<count;i++) {
        const size_t idx=first+i;
        const page_meta *m=&s->map[idx];
        const unsigned slot=m->valid?1u-m->slot:0u;
        const unsigned char *p=(const unsigned char *)src+i*PAGE;
        r.generations[i]=m->generation+1;
        r.digests[i]=hash_bytes(p,PAGE);
        rc=write_full(s->data_fd,p,PAGE,slot_offset(idx,slot));
        if(rc)goto out; /* old authoritative slot is unchanged */
    }
    rc=sync_fd(s->data_fd);
    if(rc)goto out; /* do not ACK data that was not synced */
    if(s->inject==NOPEN_DURABLE_FAIL_AFTER_DATA_SYNC) {
        s->inject=0;rc=-EIO;goto out;
    }
    r.seal=hash_bytes(&r,offsetof(journal_record,seal));
    rc=write_full(s->log_fd,&r,sizeof r,record_offset(s->record_count));
    if(rc){s->poisoned=1;goto out;}
    rc=sync_fd(s->log_fd);
    if(rc){s->poisoned=1;goto out;}
    /* This is the only COMMIT point, AFTER the real data + metadata sync. */
    rc=apply(s,&r);
    if(rc){s->poisoned=1;goto out;}
    if(s->inject==NOPEN_DURABLE_FAIL_AFTER_MANIFEST_SYNC) {
        s->inject=0;rc=-EIO;goto out; /* simulate ACK lost, durable commit kept */
    }
    rc=0;
out:
    pthread_mutex_unlock(&s->lock);
    return rc;
}
int nopen_durable_read(nopen_durable_store *s,uint64_t off,void *dest,size_t bytes) {
    if(!dest || valid_range(s,off,bytes)!=0)return -EINVAL;
    pthread_mutex_lock(&s->lock);
    int rc=-EIO;
    if(s->poisoned)goto out;
    size_t first=(size_t)(off/PAGE);
    memset(s->scratch,0,bytes);
    for(size_t i=0;i<bytes/PAGE;i++) {
        const page_meta *m=&s->map[first+i];
        if(!m->valid)continue;
        unsigned char *p=s->scratch+i*PAGE;
        rc=read_full(s->data_fd,p,PAGE,slot_offset(first+i,m->slot));
        if(rc)goto out;
        if(hash_bytes(p,PAGE)!=m->digest){rc=-EBADMSG;goto out;}
    }
    memcpy(dest,s->scratch,bytes); /* only after EVERY page verified */
    rc=0;
out:
    pthread_mutex_unlock(&s->lock);
    return rc;
}
int nopen_durable_inject(nopen_durable_store *s,int phase) {
    if(!s || phase<0 || phase>NOPEN_DURABLE_FAIL_AFTER_MANIFEST_SYNC)
        return -EINVAL;
    pthread_mutex_lock(&s->lock);
    s->inject=phase;
    pthread_mutex_unlock(&s->lock);
    return 0;
}
