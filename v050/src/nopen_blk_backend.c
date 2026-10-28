#define _POSIX_C_SOURCE 200809L
#include "nopen_blk_backend.h"
#include "nopen_page_core.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* This code is a bounded transport backend, not a kernel-side reclaim hook.
 * Only the registered context owns mutable state. Single DMA command at a
 * time, serialized; never issues GPU I/O from inside direct kernel reclaim.
 */
typedef struct { uint64_t checksum; unsigned char state; } blk_meta;
enum { BLOCK_ZERO=0,BLOCK_VALID=1,BLOCK_POISONED=2 };
struct nopen_blk_backend {
    nopen_store *store;
    blk_meta *meta;
    unsigned char *staging;
    size_t pages;
    pthread_mutex_t lock;
    const char *last_error;
};
static int fail(nopen_blk_backend *b,const char *msg,int error) {
    if(b)b->last_error=msg;
    return -error;
}
static int range(nopen_blk_backend *b,uint64_t offset,size_t bytes,size_t *first,size_t *count) {
    if(!b || !bytes || bytes>NOPEN_BLK_MAX_BYTES ||
       (offset % NOPEN_PAGE_SIZE) || (bytes % NOPEN_PAGE_SIZE) ||
       offset / NOPEN_PAGE_SIZE >= b->pages ||
       bytes / NOPEN_PAGE_SIZE > b->pages-offset/NOPEN_PAGE_SIZE) return -EINVAL;
    *first=(size_t)(offset/NOPEN_PAGE_SIZE);
    *count=bytes/NOPEN_PAGE_SIZE;
    return 0;
}
int nopen_blk_open(nopen_blk_backend **out,nopen_backend backend,size_t mib) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(mib<1||mib>4096 || (backend!=NOPEN_STORE_MOCK && backend!=NOPEN_STORE_OPENCL))
        return -EINVAL;
    nopen_blk_backend *b=calloc(1,sizeof(*b));
    if(!b)return -ENOMEM;
    b->pages=mib*(1024u*1024u/NOPEN_PAGE_SIZE);
    b->meta=calloc(b->pages,sizeof(*b->meta));
    b->staging=malloc(NOPEN_BLK_MAX_BYTES);
    if(!b->meta || !b->staging) {
        free(b->staging);free(b->meta);free(b);
        return -ENOMEM;
    }
    if(pthread_mutex_init(&b->lock,NULL)!=0) {
        free(b->staging);free(b->meta);free(b);
        return -ENOMEM;
    }
    if(nopen_store_open(&b->store,backend,mib)!=0) {
        pthread_mutex_destroy(&b->lock);
        free(b->staging);free(b->meta);free(b);
        return -EIO;
    }
    b->last_error="ok";
    *out=b;
    return 0;
}
void nopen_blk_close(nopen_blk_backend *b) {
    if(!b)return;
    nopen_store_close(b->store);
    pthread_mutex_destroy(&b->lock);
    free(b->staging);free(b->meta);free(b);
}
size_t nopen_blk_page_count(const nopen_blk_backend *b) {return b?b->pages:0;}
const char *nopen_blk_last_error(const nopen_blk_backend *b) {
    return b?b->last_error:"invalid backend";
}
void nopen_blk_inject_failure(nopen_blk_backend *b,size_t n) {
    if(!b)return;
    pthread_mutex_lock(&b->lock);
    nopen_store_inject_failure(b->store,n);
    pthread_mutex_unlock(&b->lock);
}
int nopen_blk_write(nopen_blk_backend *b,uint64_t off,const void *src,size_t len) {
    size_t first=0,count=0;
    if(!src || range(b,off,len,&first,&count)!=0)
        return fail(b,"unaligned/bounds/invalid write",EINVAL); 
    pthread_mutex_lock(&b->lock);
    /* Atomic metadata only on success. On backend failure GPU content might
     * have changed partially. Fail closed: all targeted pages are POISONED. */
    if(nopen_store_write_pages(b->store,first,src,count)!=0) {
        for(size_t i=0;i<count;i++)b->meta[first+i].state=BLOCK_POISONED;
        pthread_mutex_unlock(&b->lock);
        return fail(b,"GPU write failure; affected range poisoned",EIO);
    }
    for(size_t i=0;i<count;i++) {
        blk_meta *m=&b->meta[first+i];
        const unsigned char *page=(const unsigned char*)src+i*NOPEN_PAGE_SIZE;
        m->checksum=nopen_page_checksum(page,NOPEN_PAGE_SIZE);
        m->state=BLOCK_VALID;
    }
    pthread_mutex_unlock(&b->lock);
    return 0;
}
int nopen_blk_read(nopen_blk_backend *b,uint64_t off,void *dst,size_t len) {
    size_t first=0,count=0;
    if(!dst || range(b,off,len,&first,&count)!=0)
        return fail(b,"unaligned/bounds/invalid read",EINVAL);
    pthread_mutex_lock(&b->lock);
    /* Caller buffer is never touched before every page is checked. */
    for(size_t i=0;i<count;i++) {
        if(b->meta[first+i].state==BLOCK_POISONED) {
            pthread_mutex_unlock(&b->lock);
            return fail(b,"poisoned GPU page",EIO);
        }
    }
    memset(b->staging,0,len);
    for(size_t i=0;i<count;) {
        if(b->meta[first+i].state==BLOCK_ZERO) {i++;continue;}
        size_t start=i;
        while(i<count && b->meta[first+i].state==BLOCK_VALID)i++;
        if(nopen_store_read_pages(b->store,first+start,
                    b->staging+start*NOPEN_PAGE_SIZE,i-start)!=0) {
            pthread_mutex_unlock(&b->lock);
            return fail(b,"GPU read failure; caller unchanged",EIO);
        }
    }
    for(size_t i=0;i<count;i++) {
        if(b->meta[first+i].state==BLOCK_VALID &&
           nopen_page_checksum(b->staging+i*NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE)
                            !=b->meta[first+i].checksum) {
            b->meta[first+i].state=BLOCK_POISONED;
            pthread_mutex_unlock(&b->lock);
            return fail(b,"GPU data checksum mismatch; caller unchanged",EIO);
        }
    }
    memcpy(dst,b->staging,len);
    pthread_mutex_unlock(&b->lock);
    return 0;
}
