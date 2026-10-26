#define _GNU_SOURCE
#include "nopen_store.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Isolated process-local UFFD-backed GPU paging laboratory, NOT a swap device.
 * Access contract: precisely ONE application thread touches the arena; the
 * dedicated handler thread alone changes page metadata. No concurrent users,
 * and no storing of arena pointers across cooperative changes of page access.
 * Fault recency, not dirty-bit or full MMU access recency, chooses victims.
 */
#define MiB (1024u*1024u)
#define MAX_LAB_PAGES 16384u
#define ERR_IO 73
#define ERR_CORRUPTION 74
#define ERR_UFFD 75

typedef enum { P_ZERO=0, P_RESIDENT=1, P_GPU=2 } state;
typedef struct {
    state location;
    uint64_t checksum;
    uint64_t fault_age;
} meta;
typedef struct {
    nopen_store *store; /* Owned by main; lifetime outlives handler. */
    unsigned char *region, *scratch;
    meta *pages;
    size_t logical, limit, resident;
    uint64_t clock;
    atomic_uint_fast64_t faults, from_gpu, to_gpu, zero_faults, peak_resident;
    int uffd;
    pthread_t worker;
    int worker_started;
    atomic_int stop;
} arena;

static void fatal(int code, const char *reason, size_t page) {
    /* NEVER write zero/garbage into a failed page. Terminate only this process.
       stderr is intentionally flushed by unbuffered write before _exit. */
    dprintf(STDERR_FILENO,"NOPEN_ISOLATED_FATAL code=%d page=%zu reason=%s; no_global_swap=YES\n",
            code,page,reason);
    _exit(code);
}
static uint64_t checksum(const unsigned char *p) {
    uint64_t v=UINT64_C(14695981039346656037);
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++){
        v^=p[i];v*=UINT64_C(1099511628211);
    }
    return v;
}
static void pattern(unsigned char *p,size_t page,unsigned round) {
    uint64_t n=((uint64_t)page+1)*UINT64_C(0x9E3779B97F4A7C15) ^
               ((uint64_t)round+1)*UINT64_C(0xD6E8FEB86659FD93);
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {
        n^=n>>12;n^=n<<25;n^=n>>27;
        p[i]=(unsigned char)((n*UINT64_C(0x2545F4914F6CDD1D))>>56);
    }
}
static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC,&t);
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int check_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return -1;
    char buf[512];
    if(!fgets(buf,sizeof(buf),f)){fclose(f);return -1;}
    while(fgets(buf,sizeof(buf),f)) {
        char path[380]={0};
        if(sscanf(buf,"%379s",path)==1 && strstr(path,"/nbd")) {
            fprintf(stderr,"REFUSED: active NBD swap: %s\n",path);
            fclose(f);return -1;
        }
    }
    fclose(f);return 0;
}
static int uffd_create(void) {
#ifdef __NR_userfaultfd
    int fd=(int)syscall(__NR_userfaultfd,(long)(O_CLOEXEC|O_NONBLOCK|UFFD_USER_MODE_ONLY));
    if(fd<0)return -1;
    struct uffdio_api api={.api=UFFD_API,.features=0};
    if(ioctl(fd,UFFDIO_API,&api)<0){int e=errno;close(fd);errno=e;return -1;}
    return fd;
#else
    errno=ENOSYS;return -1;
#endif
}
static size_t oldest(const arena *a) {
    size_t victim=SIZE_MAX;
    uint64_t age=UINT64_MAX;
    for(size_t i=0;i<a->logical;i++) {
        if(a->pages[i].location==P_RESIDENT && a->pages[i].fault_age<age) {
            victim=i;age=a->pages[i].fault_age;
        }
    }
    return victim;
}
static void evict_oldest(arena *a) {
    size_t victim=oldest(a);
    if(victim==SIZE_MAX)fatal(ERR_UFFD,"no resident victim",0);
    unsigned char *from=a->region+victim*NOPEN_PAGE_SIZE;
    uint64_t digest=checksum(from);
    if(nopen_store_write(a->store,victim,from)!=0)
        fatal(ERR_IO,"OpenCL page-out failed",victim);
    /* GPU write was completed before removing CPU copy. */
    if(madvise(from,NOPEN_PAGE_SIZE,MADV_DONTNEED)!=0)
        fatal(ERR_UFFD,"MADV_DONTNEED failed",victim);
    a->pages[victim].location=P_GPU;
    a->pages[victim].checksum=digest;
    a->resident--;
    atomic_fetch_add(&a->to_gpu,1);
}
static void on_fault(arena *a,uintptr_t address) {
    uintptr_t start=(uintptr_t)a->region;
    uintptr_t pageaddr=address&~((uintptr_t)NOPEN_PAGE_SIZE-1u);
    if(pageaddr<start || (pageaddr-start)/NOPEN_PAGE_SIZE>=a->logical ||
       (pageaddr-start)%NOPEN_PAGE_SIZE!=0)
        fatal(ERR_UFFD,"unexpected fault address",0);
    size_t p=(pageaddr-start)/NOPEN_PAGE_SIZE;
    meta *m=&a->pages[p];
    if(m->location==P_RESIDENT)fatal(ERR_UFFD,"resident page faulted",p);
    /* Restore GPU data and VERIFY before evicting a valid resident page.
       This avoids losing the resident working set on read/corruption failures. */
    if(m->location==P_GPU) {
        if(nopen_store_read(a->store,p,a->scratch)!=0)
            fatal(ERR_IO,"OpenCL page-in failed",p);
        if(checksum(a->scratch)!=m->checksum)
            fatal(ERR_CORRUPTION,"VRAM page integrity mismatch",p);
    } else {
        memset(a->scratch,0,NOPEN_PAGE_SIZE);
    }
    if(a->resident>=a->limit)evict_oldest(a);
    struct uffdio_copy cp={.dst=(unsigned long)pageaddr,
                           .src=(unsigned long)(uintptr_t)a->scratch,
                           .len=NOPEN_PAGE_SIZE,.mode=0};
    if(ioctl(a->uffd,UFFDIO_COPY,&cp)<0 || cp.copy!=(long long)NOPEN_PAGE_SIZE)
        fatal(ERR_UFFD,"UFFDIO_COPY failed",p);
    if(m->location==P_GPU)atomic_fetch_add(&a->from_gpu,1);
    else atomic_fetch_add(&a->zero_faults,1);
    m->location=P_RESIDENT;
    m->fault_age=++a->clock;
    a->resident++;
    if(a->resident>atomic_load(&a->peak_resident))
        atomic_store(&a->peak_resident,a->resident);
    atomic_fetch_add(&a->faults,1);
}
static void *worker(void *ptr) {
    arena *a=ptr;
    while(!atomic_load(&a->stop)) {
        struct pollfd pfd={.fd=a->uffd,.events=POLLIN};
        int rc=poll(&pfd,1,50);
        if(rc==0 || (rc<0 && errno==EINTR))continue;
        if(rc<0 || (pfd.revents&(POLLERR|POLLHUP|POLLNVAL)))
            fatal(ERR_UFFD,"userfaultfd poll failed",0);
        if(!(pfd.revents&POLLIN))continue;
        struct uffd_msg msg;
        ssize_t len=read(a->uffd,&msg,sizeof(msg));
        if(len<0 && (errno==EAGAIN||errno==EINTR))continue;
        if(len!=(ssize_t)sizeof(msg) || msg.event!=UFFD_EVENT_PAGEFAULT ||
           (msg.arg.pagefault.flags & ~(uint64_t)UFFD_PAGEFAULT_FLAG_WRITE)!=0)
            fatal(ERR_UFFD,"unexpected userfaultfd message",0);
        on_fault(a,(uintptr_t)msg.arg.pagefault.address);
    }
    return NULL;
}
static void destroy(arena *a) {
    atomic_store(&a->stop,1);
    if(a->worker_started)pthread_join(a->worker,NULL);
    if(a->uffd>=0)close(a->uffd);
    if(a->region && a->region!=MAP_FAILED)munmap(a->region,a->logical*NOPEN_PAGE_SIZE);
    free(a->scratch);free(a->pages);
}
static int init(arena *a,nopen_store *s,size_t pages,size_t limit) {
    memset(a,0,sizeof(*a));a->uffd=-1;a->store=s;a->logical=pages;a->limit=limit;
    a->pages=calloc(pages,sizeof(*a->pages));
    if(posix_memalign((void**)&a->scratch,NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE)!=0)a->scratch=NULL;
    a->region=mmap(NULL,pages*NOPEN_PAGE_SIZE,PROT_READ|PROT_WRITE,
                   MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    if(!a->pages||!a->scratch||a->region==MAP_FAILED)return -1;
    a->uffd=uffd_create();
    if(a->uffd<0)return -1;
    struct uffdio_register reg={.range={.start=(unsigned long)(uintptr_t)a->region,
                                         .len=pages*NOPEN_PAGE_SIZE},
                                .mode=UFFDIO_REGISTER_MODE_MISSING};
    if(ioctl(a->uffd,UFFDIO_REGISTER,&reg)<0 ||
       !(reg.ioctls&(1ull<<_UFFDIO_COPY)))return -1;
    if(pthread_create(&a->worker,NULL,worker,a)!=0)return -1;
    a->worker_started=1;
    return 0;
}
static int positive(const char *s,size_t *v) {
    if(!s||!*s||*s=='-'||*s=='+')return -1;
    errno=0;char *end=NULL;unsigned long long n=strtoull(s,&end,10);
    if(errno||end==s||*end||n==0||n>SIZE_MAX)return -1;
    *v=(size_t)n;return 0;
}
static void help(void) {
    puts("Nopen 0.12.0-alpha.3: process-only bounded userfaultfd VRAM tier (NOT Linux swap)");
    puts("--probe | --backend mock|opencl --pool-mib 64 --logical-pages 512 --ram-pages 16 --rounds 2");
    puts("--inject-read-failure | --inject-write-failure | --inject-corruption (mock only)");
    puts("One application thread only; no NBD, swapon, system services or root required.");
}
int main(int argc,char **argv) {
    nopen_backend backend=NOPEN_STORE_MOCK;
    size_t pool=64,logical=512,ram=16,rounds=2;
    int probe=0,inject_read=0,inject_write=0,inject_corrupt=0;
    for(int i=1;i<argc;i++) {
        const char *arg=argv[i];
        if(!strcmp(arg,"--help")||!strcmp(arg,"-h")){help();return 0;}
        if(!strcmp(arg,"--probe")){probe=1;continue;}
        if(!strcmp(arg,"--inject-read-failure")){inject_read=1;continue;}
        if(!strcmp(arg,"--inject-write-failure")){inject_write=1;continue;}
        if(!strcmp(arg,"--inject-corruption")){inject_corrupt=1;continue;}
        if(i+1>=argc){help();return 2;}
        const char *val=argv[++i];
        if(!strcmp(arg,"--backend")) {
            if(!strcmp(val,"mock"))backend=NOPEN_STORE_MOCK;
            else if(!strcmp(val,"opencl"))backend=NOPEN_STORE_OPENCL;
            else {help();return 2;}
        } else {
            size_t *d=NULL;
            if(!strcmp(arg,"--pool-mib"))d=&pool;
            else if(!strcmp(arg,"--logical-pages"))d=&logical;
            else if(!strcmp(arg,"--ram-pages"))d=&ram;
            else if(!strcmp(arg,"--rounds"))d=&rounds;
            if(!d||positive(val,d)!=0){help();return 2;}
        }
    }
    if(probe) {
        int fd=uffd_create();
        if(fd<0){fprintf(stderr,"UFFD_UNAVAILABLE errno=%d (%s)\n",errno,strerror(errno));return 3;}
        close(fd);puts("UFFD_AVAILABLE user-mode-only; no global swap touched");return 0;
    }
    if(inject_read+inject_write+inject_corrupt>1 ||
       ((inject_read||inject_write||inject_corrupt)&&backend!=NOPEN_STORE_MOCK)||
       !pool||pool>NOPEN_MAX_MIB||!logical||logical>MAX_LAB_PAGES||
       logical>pool*(MiB/NOPEN_PAGE_SIZE)||!ram||ram>=logical||!rounds||rounds>20){
        help();return 2;
    }
    if(check_swap()!=0)return 4;
    nopen_store *s=NULL;
    if(nopen_store_open(&s,backend,pool)!=0){fputs("STORE_INIT_FAILED\n",stderr);return 4;}
    arena a;
    if(init(&a,s,logical,ram)!=0) {
        fprintf(stderr,"UFFD_INIT_FAILED errno=%d (%s)\n",errno,strerror(errno));
        destroy(&a);nopen_store_close(s);return 3;
    }
    unsigned char *want=malloc(NOPEN_PAGE_SIZE),*overwrite=calloc(1,NOPEN_PAGE_SIZE);
    if(!want||!overwrite)fatal(ERR_UFFD,"cannot allocate pattern buffer",0);
    const double began=seconds();
    for(size_t r=0;r<rounds;r++) {
        if(inject_write && r==0)nopen_store_inject_failure(s,1);
        for(size_t p=0;p<logical;p++) {
            pattern(want,p,(unsigned)r);
            memcpy(a.region+p*NOPEN_PAGE_SIZE,want,NOPEN_PAGE_SIZE);
        }
        if(inject_read && r==0)nopen_store_inject_failure(s,1);
        if(inject_corrupt && r==0) {
            /* Page zero must have been evicted since logical > ram. */
            if(nopen_store_write(s,0,overwrite)!=0)
                fatal(ERR_IO,"mock corruption injection write failed",0);
        }
        for(size_t p=logical;p>0;) {
            p--;
            pattern(want,p,(unsigned)r);
            if(memcmp(want,a.region+p*NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE)!=0)
                fatal(ERR_CORRUPTION,"application page mismatch",p);
        }
        /* Every completed user fault is synchronous with caller reads/writes. */
        printf("ROUND_PASS round=%zu logical_pages=%zu ram_peak=%"PRIu64" faults=%"PRIu64
               " pageouts=%"PRIu64" pageins=%"PRIu64"\n",
               r+1,logical,(uint64_t)atomic_load(&a.peak_resident),
               (uint64_t)atomic_load(&a.faults),(uint64_t)atomic_load(&a.to_gpu),
               (uint64_t)atomic_load(&a.from_gpu));
        fflush(stdout);
    }
    const double elapsed=seconds()-began;
    printf("DEMAND_PASS version=0.12.0-alpha.3 backend=%s pages=%zu ram_page_limit=%zu "
           "verified_mib=%.2f elapsed_s=%.4f faults=%"PRIu64" pageouts=%"PRIu64
           " pageins=%"PRIu64" peak_ram_pages=%"PRIu64" no_global_swap=YES\n",
           backend==NOPEN_STORE_MOCK?"mock":"opencl",logical,ram,
           (double)logical*(double)rounds*(double)NOPEN_PAGE_SIZE/MiB,
           elapsed,(uint64_t)atomic_load(&a.faults),(uint64_t)atomic_load(&a.to_gpu),
           (uint64_t)atomic_load(&a.from_gpu),(uint64_t)atomic_load(&a.peak_resident));
    fflush(stdout);
    if(atomic_load(&a.peak_resident)>ram || atomic_load(&a.from_gpu)==0 ||
       atomic_load(&a.to_gpu)==0 || atomic_load(&a.faults)==0)
        fatal(ERR_UFFD,"invalid page accounting",0);
    destroy(&a);nopen_store_close(s);free(want);free(overwrite);
    return 0;
}