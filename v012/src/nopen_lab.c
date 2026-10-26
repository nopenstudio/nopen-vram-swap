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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define MIB (1024u*1024u)
#define MAX_TEST_PAGES 8192u
#define UFFD_FAIL "userfaultfd unavailable; nothing was changed. Check kernel support/security policy."

typedef struct {
    unsigned char *region;
    size_t capacity_pages;
    uint8_t *backed;
    unsigned char *scratch;
    nopen_store *store;
    int uffd;
    pthread_t thread;
    int thread_started;
    atomic_int stopping, fatal;
    atomic_uint_fast64_t faults, restored;
    pthread_mutex_t io_lock;
} nopen_arena;

static double monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec*1000.0+(double)ts.tv_nsec/1000000.0;
}
static void pattern(unsigned char *page,size_t index,unsigned round) {
    uint64_t state=(uint64_t)(index+1)*UINT64_C(0x9E3779B97F4A7C15) ^ ((uint64_t)(round+1)*UINT64_C(0xD6E8FEB86659FD93));
    for(size_t i=0;i<NOPEN_PAGE_SIZE;i++) {
        state ^= state>>12;state ^= state<<25;state ^= state>>27;
        page[i]=(unsigned char)((state*UINT64_C(0x2545F4914F6CDD1D))>>56);
    }
}
static int unsafe_existing_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 1;
    char line[512];
    if(!fgets(line,sizeof(line),f)){fclose(f);return 1;}
    while(fgets(line,sizeof(line),f)) {
        char path[384]={0};
        if(sscanf(line,"%383s",path)!=1)continue;
        /* Ensure that no legacy VRAM-NBD swap is active. Unrelated swaps are not modified. */
        if(strstr(path,"/nbd")!=NULL) {
            fprintf(stderr,"REFUSED: active NBD swap detected: %s. Stop before using v0.12.\n",path);
            fclose(f);return 1;
        }
    }
    fclose(f);return 0;
}
static int make_uffd(void) {
#ifdef __NR_userfaultfd
    int fd=(int)syscall(__NR_userfaultfd,(long)(O_CLOEXEC|O_NONBLOCK|UFFD_USER_MODE_ONLY));
    if(fd<0)return -1;
    struct uffdio_api api={.api=UFFD_API,.features=0};
    if(ioctl(fd,UFFDIO_API,&api)<0){int err=errno;close(fd);errno=err;return -1;}
    return fd;
#else
    errno=ENOSYS;return -1;
#endif
}
static void fatal_pager(nopen_arena *a,const char *msg) {
    fprintf(stderr,"FATAL: isolated application pager %s (errno=%d). Exiting test process; system swap unaffected.\n",msg,errno);
    atomic_store(&a->fatal,1);
    /* A page fault cannot be safely restored after a failed GPU read. Fail closed.
       SIGTERM ends ONLY this research process, not a kernel service/PID 1. */
    kill(getpid(),SIGTERM);
}
static void *pagefault_thread(void *arg) {
    nopen_arena *a=arg;
    while(!atomic_load(&a->stopping)) {
        struct pollfd pfd={.fd=a->uffd,.events=POLLIN};
        int n=poll(&pfd,1,100);
        if(n==0 || (n<0&&errno==EINTR))continue;
        if(n<0 || (pfd.revents&(POLLERR|POLLHUP|POLLNVAL))) {fatal_pager(a,"poll failure");break;}
        if(!(pfd.revents&POLLIN))continue;
        struct uffd_msg msg;
        ssize_t len=read(a->uffd,&msg,sizeof(msg));
        if(len<0&&(errno==EAGAIN||errno==EINTR))continue;
        if(len!=(ssize_t)sizeof(msg)||msg.event!=UFFD_EVENT_PAGEFAULT) {fatal_pager(a,"unexpected uffd message");break;}
        uintptr_t start=(uintptr_t)a->region;
        uintptr_t address=(uintptr_t)msg.arg.pagefault.address & ~((uintptr_t)NOPEN_PAGE_SIZE-1u);
        if(address<start||address-start>=a->capacity_pages*NOPEN_PAGE_SIZE) {fatal_pager(a,"fault address outside arena");break;}
        size_t page=(address-start)/NOPEN_PAGE_SIZE;
        pthread_mutex_lock(&a->io_lock);
        if(a->backed[page]) {
            if(nopen_store_read(a->store,page,a->scratch)!=0) {
                fprintf(stderr,"STORE READ: %s page=%zu\n",nopen_store_last_error(a->store),page);
                pthread_mutex_unlock(&a->io_lock);fatal_pager(a,"GPU page-in failed");break;
            }
            atomic_fetch_add(&a->restored,1);
        } else memset(a->scratch,0,NOPEN_PAGE_SIZE);
        struct uffdio_copy cp={.dst=(unsigned long)address,.src=(unsigned long)(uintptr_t)a->scratch,.len=NOPEN_PAGE_SIZE,.mode=0};
        int ok=ioctl(a->uffd,UFFDIO_COPY,&cp);
        pthread_mutex_unlock(&a->io_lock);
        if(ok<0) {fatal_pager(a,"UFFDIO_COPY failed");break;}
        atomic_fetch_add(&a->faults,1);
    }
    return NULL;
}
static int arena_open(nopen_arena *a,nopen_store *s,size_t pages) {
    memset(a,0,sizeof(*a));a->uffd=-1;a->store=s;
    a->capacity_pages=pages;
    if(pthread_mutex_init(&a->io_lock,NULL)!=0)return -1;
    a->region=mmap(NULL,pages*NOPEN_PAGE_SIZE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(a->region==MAP_FAILED) {a->region=NULL;return -1;}
    a->backed=calloc(pages,1);
    if(posix_memalign((void**)&a->scratch,NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE)!=0 || !a->backed)return -1;
    a->uffd=make_uffd();
    if(a->uffd<0)return -1;
    struct uffdio_register r={.range={.start=(unsigned long)(uintptr_t)a->region,.len=pages*NOPEN_PAGE_SIZE},.mode=UFFDIO_REGISTER_MODE_MISSING};
    if(ioctl(a->uffd,UFFDIO_REGISTER,&r)<0)return -1;
    if(!(r.ioctls & (1ull<<_UFFDIO_COPY))) {errno=ENOTSUP;return -1;}
    if(pthread_create(&a->thread,NULL,pagefault_thread,a)!=0)return -1;
    a->thread_started=1;
    return 0;
}
static void arena_close(nopen_arena *a) {
    atomic_store(&a->stopping,1);
    if(a->thread_started)pthread_join(a->thread,NULL);
    if(a->uffd>=0)close(a->uffd);
    if(a->region)munmap(a->region,a->capacity_pages*NOPEN_PAGE_SIZE);
    free(a->backed);free(a->scratch);
    pthread_mutex_destroy(&a->io_lock);
}
static int arena_evict(nopen_arena *a,size_t page) {
    if(page>=a->capacity_pages)return -1;
    /* Cooperative eviction: caller MUST guarantee no other thread is reading,
       writing or faulting on this region while this function runs. This is NOT
       a transparent concurrent application pager. */
    pthread_mutex_lock(&a->io_lock);
    unsigned char *p=a->region+page*NOPEN_PAGE_SIZE;
    if(nopen_store_write(a->store,page,p)!=0) {
        fprintf(stderr,"STORE WRITE: %s page=%zu\n",nopen_store_last_error(a->store),page);
        pthread_mutex_unlock(&a->io_lock);return -1;
    }
    a->backed[page]=1;
    int rc=madvise(p,NOPEN_PAGE_SIZE,MADV_DONTNEED);
    pthread_mutex_unlock(&a->io_lock);
    return rc;
}
static int parse_unsigned(const char*s,size_t*out) {
    if(!s||!*s||*s=='-'||*s=='+')return -1;
    char *end=NULL;errno=0;unsigned long long v=strtoull(s,&end,10);
    if(errno||end==s||*end||v==0||v>SIZE_MAX)return -1;
    *out=(size_t)v;return 0;
}
static void usage(void) {
    puts("Nopen VRAM-SWAP v0.12.0-alpha.1 — isolated userspace GPU page lab");
    puts("  --self-audit                     Display non-destructive architecture controls");
    puts("  --probe                          Check unprivileged userfaultfd only");
    puts("  --demo --backend mock|opencl     Userfaultfd page-in + cooperative GPU eviction");
    puts("          [--mib 64] [--pages 256] [--rounds 3] [--inject-on-pagein (mock only)]");
    puts("  --raw --backend mock|opencl      Direct 4KiB put/get without userfaultfd");
    puts("          [--mib 64] [--pages 256] [--rounds 3]");
    puts("  OpenCL backend refuses missing discrete AMD GPU. No fallback. NO NBD/SWAPON.");
}
int main(int argc,char **argv) {
    int demo=0,raw=0,probe=0,audit=0,inject_on_pagein=0;
    nopen_backend backend=NOPEN_STORE_MOCK;
    size_t mib=64,sample_pages=256,rounds=3;
    for(int i=1;i<argc;i++) {
        const char *arg=argv[i];
        if(strcmp(arg,"--demo")==0)demo=1;
        else if(strcmp(arg,"--raw")==0)raw=1;
        else if(strcmp(arg,"--probe")==0)probe=1;
        else if(strcmp(arg,"--self-audit")==0)audit=1;
        else if(strcmp(arg,"--inject-on-pagein")==0)inject_on_pagein=1;
        else if(strcmp(arg,"--help")==0||strcmp(arg,"-h")==0){usage();return 0;}
        else if(strcmp(arg,"--backend")==0&&i+1<argc) {
            const char *v=argv[++i];
            if(strcmp(v,"mock")==0)backend=NOPEN_STORE_MOCK;
            else if(strcmp(v,"opencl")==0)backend=NOPEN_STORE_OPENCL;
            else {fprintf(stderr,"Invalid backend: %s\n",v);return 2;}
        } else if(i+1<argc && (!strcmp(arg,"--mib")||!strcmp(arg,"--pages")||!strcmp(arg,"--rounds"))) {
            size_t *target=!strcmp(arg,"--mib")?&mib:!strcmp(arg,"--pages")?&sample_pages:&rounds;
            if(parse_unsigned(argv[++i],target)!=0){fprintf(stderr,"Invalid numeric option: %s\n",arg);return 2;}
        } else {fprintf(stderr,"Unknown option: %s\n",arg);usage();return 2;}
    }
    if(audit) {
        puts("AUDIT: this executable does not open /dev/nbd*, issue SWAPON/SWAPOFF or require root.");
        puts("AUDIT: only this process' explicit anonymous mmap is registered with UFFD_USER_MODE_ONLY.");
        puts("AUDIT: eviction is cooperative; no transparent handling of unmodified game memory.");
        return 0;
    }
    if(probe) {
        int fd=make_uffd();
        if(fd<0){fprintf(stderr,"UFFD_UNAVAILABLE errno=%d (%s): %s\n",errno,strerror(errno),UFFD_FAIL);return 3;}
        close(fd);puts("UFFD_AVAILABLE: user-mode-only page faults supported (no swap enabled)");return 0;
    }
    if((demo+raw)!=1 || (inject_on_pagein && (!demo || backend!=NOPEN_STORE_MOCK)) || !mib||mib>NOPEN_MAX_MIB||!rounds||rounds>20||!sample_pages||sample_pages>MAX_TEST_PAGES||sample_pages>mib*MIB/NOPEN_PAGE_SIZE) {
        usage();return 2;
    }
    if(unsafe_existing_swap())return 4;
    nopen_store *s=NULL;
    if(nopen_store_open(&s,backend,mib)!=0){fputs("REFUSED: storage backend initialization failed\n",stderr);return 4;}
    unsigned char *reference=malloc(NOPEN_PAGE_SIZE),*tmp=malloc(NOPEN_PAGE_SIZE);
    if(!reference||!tmp){fprintf(stderr,"Failed scratch allocation\n");free(reference);free(tmp);nopen_store_close(s);return 4;}
    nopen_arena arena={0};
    if(demo) {
        if(arena_open(&arena,s,nopen_store_page_count(s))!=0) {
            fprintf(stderr,"UFFD_UNAVAILABLE errno=%d (%s): %s\n",errno,strerror(errno),UFFD_FAIL);
            arena_close(&arena);free(reference);free(tmp);nopen_store_close(s);return 3;
        }
    }
    double start=monotonic_ms();
    size_t total_pages=nopen_store_page_count(s),step=total_pages/sample_pages;
    if(step==0)step=1;
    for(size_t round=0;round<rounds;round++) {
        for(size_t i=0;i<sample_pages;i++) {
            size_t index=i*step;
            pattern(reference,index,(unsigned)round);
            if(demo) {
                memcpy(arena.region+index*NOPEN_PAGE_SIZE,reference,NOPEN_PAGE_SIZE);
                if(arena_evict(&arena,index)!=0) {fputs("FAIL: eviction refused, no global swap involved\n",stderr);goto failure;}
            } else {
                if(nopen_store_write(s,index,reference)!=0){fputs("FAIL: raw store write\n",stderr);goto failure;}
            }
        }
        if(inject_on_pagein && round==0) {
            fputs("INJECTED: next mock GPU page-in read will fail; only this process may terminate.\n",stderr);
            nopen_store_inject_failure(s,1);
        }
        for(size_t i=sample_pages;i>0;) {
            i--;size_t index=i*step;
            pattern(reference,index,(unsigned)round);
            if(demo)memcpy(tmp,arena.region+index*NOPEN_PAGE_SIZE,NOPEN_PAGE_SIZE); /* UFFD traps page-in */
            else if(nopen_store_read(s,index,tmp)!=0){fputs("FAIL: raw store read\n",stderr);goto failure;}
            if(memcmp(reference,tmp,NOPEN_PAGE_SIZE)!=0) {
                fprintf(stderr,"FAIL: silent data corruption index=%zu round=%zu\n",index,round);goto failure;
            }
        }
        printf("ROUND_PASS round=%zu verified_pages=%zu bytes=%zu\n",round+1,sample_pages,sample_pages*NOPEN_PAGE_SIZE);
    }
    {
        double elapsed=(monotonic_ms()-start)/1000.0;
        printf("TEST_PASS version=0.12.0-alpha.1 mode=%s backend=%s pages_per_round=%zu rounds=%zu verified_mib=%.2f seconds=%.4f io_mib_s=%.2f faults=%"PRIuFAST64" restored=%"PRIuFAST64" no_global_swap=YES\n",
            demo?"userfaultfd":"raw",backend==NOPEN_STORE_OPENCL?"opencl":"mock",sample_pages,rounds,
            (double)sample_pages*(double)rounds*(double)NOPEN_PAGE_SIZE/MIB,elapsed,
            elapsed>0?(2.0*(double)sample_pages*(double)rounds*(double)NOPEN_PAGE_SIZE/MIB)/elapsed:0,
            demo?atomic_load(&arena.faults):0,demo?atomic_load(&arena.restored):0);
        if(demo)arena_close(&arena);
        free(reference);free(tmp);nopen_store_close(s);return 0;
    }
failure:
    if(demo)arena_close(&arena);
    free(reference);free(tmp);nopen_store_close(s);
    return 5;
}