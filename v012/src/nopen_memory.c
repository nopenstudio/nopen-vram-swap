#define _GNU_SOURCE
#include "nopen_memory.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Deliberately *not* Linux page tables / swap entries: cooperative app API. */
typedef enum { PAGE_ZERO=0, PAGE_RESIDENT=1, PAGE_GPU=2 } page_state;
typedef struct {
    page_state state;
    size_t slot;
    uint64_t checksum;
    uint64_t last_used;
} page_meta;
struct nopen_memory {
    nopen_store *store; /* borrowed */
    size_t pages, slots;
    page_meta *meta;
    size_t *owner;
    unsigned char *resident;
    unsigned char *scratch;
    uint64_t clock;
    nopen_memory_stats stats;
    char error[256];
};

static uint64_t digest(const unsigned char *data) {
    /* 64-bit FNV1a is a fast error detector, NOT cryptographic integrity. */
    uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {
        h ^= (uint64_t)data[i];h *= UINT64_C(1099511628211);
    }
    return h;
}
static int err(nopen_memory*m,const char*where,size_t page,const char *why) {
    snprintf(m->error,sizeof(m->error),"%s page=%zu: %s",where,page,why);
    return -1;
}
const char *nopen_memory_last_error(const nopen_memory *m) {
    return m?m->error:"null memory manager";
}
void nopen_memory_get_stats(const nopen_memory *m,nopen_memory_stats *out) {
    if(out) {*out=(nopen_memory_stats){0};if(m)*out=m->stats;}
}
int nopen_memory_open(nopen_memory **out,nopen_store *store,size_t pages,size_t slots) {
    if(!out)return -1;
    *out=NULL;
    if(!store||!pages||slots==0||slots>pages||pages>nopen_store_page_count(store)||
       slots>SIZE_MAX/NOPEN_PAGE_SIZE)return -1;
    nopen_memory *m=calloc(1,sizeof(*m));
    if(!m)return -1;
    m->store=store;m->pages=pages;m->slots=slots;
    m->meta=calloc(pages,sizeof(*m->meta));
    m->owner=malloc(slots*sizeof(*m->owner));
    if(posix_memalign((void**)&m->scratch,NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE)!=0)m->scratch=NULL;
    m->resident=mmap(NULL,slots*NOPEN_PAGE_SIZE,PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(m->resident==MAP_FAILED)m->resident=NULL;
    if(!m->meta||!m->owner||!m->scratch||!m->resident) {nopen_memory_close(m);return -1;}
    for(size_t i=0;i<pages;i++)m->meta[i].slot=SIZE_MAX;
    for(size_t i=0;i<slots;i++)m->owner[i]=SIZE_MAX;
    m->stats.logical_pages=pages;m->stats.resident_limit_pages=slots;
    m->stats.zero_pages=pages;
    *out=m;return 0;
}
void nopen_memory_close(nopen_memory *m) {
    if(!m)return;
    if(m->resident)munmap(m->resident,m->slots*NOPEN_PAGE_SIZE);
    free(m->scratch);free(m->owner);free(m->meta);free(m);
}
static void touch(nopen_memory*m,size_t page) {
    /* Renormalize counters extremely conservatively, preserving ordering. */
    if(m->clock==UINT64_MAX) {
        uint64_t min=UINT64_MAX;
        for(size_t i=0;i<m->pages;i++)
            if(m->meta[i].state==PAGE_RESIDENT&&m->meta[i].last_used<min)
                min=m->meta[i].last_used;
        for(size_t i=0;i<m->pages;i++)
            if(m->meta[i].state==PAGE_RESIDENT)m->meta[i].last_used-=min;
        m->clock-=min;
    }
    m->meta[page].last_used=++m->clock;
}
static size_t oldest(const nopen_memory*m) {
    size_t victim=SIZE_MAX;uint64_t age=UINT64_MAX;
    for(size_t s=0;s<m->slots;s++) {
        size_t p=m->owner[s];
        if(p!=SIZE_MAX&&m->meta[p].last_used<age) {victim=p;age=m->meta[p].last_used;}
    }
    return victim;
}
static int evict(nopen_memory *m,size_t page) {
    page_meta *p=&m->meta[page];
    if(p->state!=PAGE_RESIDENT)return err(m,"evict",page,"not a resident page");
    size_t slot=p->slot;
    unsigned char *src=m->resident+slot*NOPEN_PAGE_SIZE;
    /* Commit metadata ONLY after successful transfer. On failure, RAM is valid. */
    uint64_t sum=digest(src);
    if(nopen_store_write(m->store,page,src)!=0) {
        m->stats.failed_writes++;
        return err(m,"evict",page,nopen_store_last_error(m->store));
    }
    p->checksum=sum;p->slot=SIZE_MAX;p->state=PAGE_GPU;
    m->owner[slot]=SIZE_MAX;
    m->stats.resident_pages--;m->stats.vram_pages++;m->stats.page_outs++;
    /* Reclaim physical CPU pages; lack of madvise success does not invalidate data. */
    if(madvise(src,NOPEN_PAGE_SIZE,MADV_DONTNEED)!=0)m->stats.madvise_errors++;
    return 0;
}
static int find_slot(nopen_memory *m,size_t *slot) {
    for(size_t i=0;i<m->slots;i++) {
        if(m->owner[i]==SIZE_MAX) {*slot=i;return 0;}
    }
    size_t victim=oldest(m);
    if(victim==SIZE_MAX)return err(m,"internal",0,"no LRU victim");
    size_t reuse=m->meta[victim].slot;
    if(evict(m,victim)!=0)return -1;
    *slot=reuse;return 0;
}
static int ensure_resident(nopen_memory*m,size_t page) {
    page_meta *p=&m->meta[page];
    if(p->state==PAGE_RESIDENT) {touch(m,page);return 0;}
    /* Read and validate GPU data BEFORE displacing a live CPU page.
     * On read/corruption failure, the current resident working set is unchanged. */
    if(p->state==PAGE_GPU) {
        if(nopen_store_read(m->store,page,m->scratch)!=0) {
            m->stats.failed_reads++;
            return err(m,"restore",page,nopen_store_last_error(m->store));
        }
        if(digest(m->scratch)!=p->checksum) {
            m->stats.checksum_errors++;
            return err(m,"restore",page,"VRAM checksum mismatch; refused corrupt page");
        }
    }
    size_t slot=SIZE_MAX;
    if(find_slot(m,&slot)!=0)return -1;
    unsigned char *dst=m->resident+slot*NOPEN_PAGE_SIZE;
    if(p->state==PAGE_GPU) {
        memcpy(dst,m->scratch,NOPEN_PAGE_SIZE);
        m->stats.vram_pages--;m->stats.page_ins++;
    } else {
        memset(dst,0,NOPEN_PAGE_SIZE);
        m->stats.zero_pages--;
    }
    p->state=PAGE_RESIDENT;p->slot=slot;m->owner[slot]=page;
    m->stats.resident_pages++;
    touch(m,page);
    return 0;
}
int nopen_memory_read(nopen_memory*m,size_t page,void *out_4k) {
    if(!m||!out_4k||page>=m->pages)return -1;
    if(ensure_resident(m,page)!=0)return -1;
    memcpy(out_4k,m->resident+m->meta[page].slot*NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE);
    m->stats.reads++;return 0;
}
int nopen_memory_write(nopen_memory*m,size_t page,const void *in_4k) {
    if(!m||!in_4k||page>=m->pages)return -1;
    page_meta *p=&m->meta[page];
    if(p->state!=PAGE_RESIDENT) {
        /* Entire-page overwrite: no need to read the old GPU copy first.
         * If evicting the LRU victim fails, the original target state and
         * the RAM-resident victim remain valid and untouched. */
        size_t slot=SIZE_MAX;
        if(find_slot(m,&slot)!=0)return -1;
        if(p->state==PAGE_GPU)m->stats.vram_pages--;
        else m->stats.zero_pages--;
        p->state=PAGE_RESIDENT;p->slot=slot;m->owner[slot]=page;
        m->stats.resident_pages++;
    }
    memcpy(m->resident+p->slot*NOPEN_PAGE_SIZE,in_4k,NOPEN_PAGE_SIZE);
    touch(m,page);
    m->stats.writes++;return 0;
}
int nopen_memory_trim(nopen_memory*m,size_t keep_resident) {
    if(!m||keep_resident>m->slots)return -1;
    while(m->stats.resident_pages>keep_resident) {
        size_t victim=oldest(m);
        if(victim==SIZE_MAX||evict(m,victim)!=0)return -1;
    }
    return 0;
}