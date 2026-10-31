#define _POSIX_C_SOURCE 200809L
#include "nopen_blk_backend.h"
#include "nopen_page_core.h"
#include "isolated_protocol.h"
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static int send_all(int fd,const void *buf,size_t n) {
    const unsigned char *p=buf;
    while(n) {
        ssize_t k=send(fd,p,n,MSG_NOSIGNAL);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)return -1;
        p+=(size_t)k;n-=(size_t)k;
    }
    return 0;
}
static int recv_all(int fd,void *buf,size_t n) {
    unsigned char *p=buf;
    while(n) {
        ssize_t k=recv(fd,p,n,0);
        if(k<0 && errno==EINTR)continue;
        if(k<=0)return -1;
        p+=(size_t)k;n-=(size_t)k;
    }
    return 0;
}
/* Trace individual IPC/driver phases, opt-in and data-free. */
static uint64_t diag_monotonic_ms(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
static void diag_worker(const char *phase,const nopen_ipc_msg *m,
                        int status,uint64_t began) {
    const char *v=getenv("NOPEN_DIAG_IO_TRACE");
    if(!v || strcmp(v,"1")!=0)return;
    const uint64_t now=diag_monotonic_ms();
    fprintf(stderr,"NOPEN_DIAG_WORKER phase=%s t_ms=%" PRIu64
            " op=%s offset=%" PRIu64 " bytes=%u status=%d elapsed_ms=%" PRIu64 "\n",
            phase,now,m->op==NOPEN_IPC_READ?"READ":"WRITE",
            m->offset,m->bytes,status,now-began);
    fflush(stderr);
}
/* Live kernel swap cannot depend on an easily OOM-killed userspace worker.
 * Best-effort OOM protection and mlock of its 64KiB staging buffer. OpenCL
 * runtime/driver allocations are NOT mlocked or guaranteed forward-progress.
 * Root is only present for the explicitly opted-in v0.7 kernel test. */
static void protect_swap_worker(void) {
    if(geteuid()!=0)return;
    FILE *score=fopen("/proc/self/oom_score_adj","w");
    if(score) {
        if(fputs("-900\n",score)<0)
            fputs("NOPEN_V070_WARNING unable to protect worker from OOM\n",stderr);
        fclose(score);
    }
}
int main(int argc,char **argv) {
    /* Private binary: called only through posix_spawn with fd=3. */
    if(argc!=3)return 2;
    nopen_backend backend=strcmp(argv[1],"mock")==0?
        NOPEN_STORE_MOCK:strcmp(argv[1],"opencl")==0?NOPEN_STORE_OPENCL:0;
    char *end=NULL;
    errno=0;
    unsigned long mib=strtoul(argv[2],&end,10);
    if(!backend||errno||end==argv[2]||*end||mib<4||mib>4096)return 2;
    protect_swap_worker();
    const int fd=3;
    nopen_blk_backend *b=NULL;
    int open_rc=nopen_blk_open(&b,backend,(size_t)mib);
    nopen_ipc_msg msg={NOPEN_IPC_MAGIC,NOPEN_IPC_READY,0,0,
                       (int32_t)open_rc,0};
    if(send_all(fd,&msg,sizeof(msg))!=0 || open_rc!=0) {
        nopen_blk_close(b);return 3;
    }
    unsigned char *buffer=malloc(NOPEN_BLK_MAX_BYTES);
    if(!buffer){nopen_blk_close(b);return 4;}
    if(mlock(buffer,NOPEN_BLK_MAX_BYTES)!=0) {
        const char *direct=getenv("NOPEN_VRAM_DIRECT_PREFILL");
        if(geteuid()==0 && direct && strcmp(direct,"1")==0) {
            fprintf(stderr,"NOPEN_DIRECT_WORKER_MLOCK_REFUSED errno=%d\n",errno);
            free(buffer);nopen_blk_close(b);return 4;
        }
        fputs("NOPEN_V070_WARNING worker staging not mlocked; reclaim risk remains\n",stderr);
    } else if(geteuid()==0) {
        fprintf(stderr,"NOPEN_DIRECT_WORKER_STAGING_MLOCK_PASS bytes=%u\n",
                (unsigned)NOPEN_BLK_MAX_BYTES);
    }
    for(;;) {
        if(recv_all(fd,&msg,sizeof(msg))!=0)break;
        if(msg.magic!=NOPEN_IPC_MAGIC || msg.bytes>NOPEN_BLK_MAX_BYTES ||
           (msg.op!=NOPEN_IPC_READ && msg.op!=NOPEN_IPC_WRITE &&
            msg.op!=NOPEN_IPC_HANG_TEST_ONLY))break;
        if(msg.op==NOPEN_IPC_HANG_TEST_ONLY) {
            if(backend!=NOPEN_STORE_MOCK)break;
            struct timespec sleep_time={.tv_sec=10,.tv_nsec=0};
            while(nanosleep(&sleep_time,&sleep_time)<0 && errno==EINTR){}
            break; /* Parent should have reached timeout and terminated us. */
        }
        const int invalid=msg.bytes==0 ||
                          (msg.bytes%NOPEN_PAGE_SIZE)!=0 ||
                          (msg.offset%NOPEN_PAGE_SIZE)!=0;
        int status=invalid?-EINVAL:0;
        const uint64_t began=diag_monotonic_ms();
        diag_worker("BEGIN",&msg,status,began);
        const uint64_t incoming_checksum=msg.checksum;
        if(msg.op==NOPEN_IPC_WRITE) {
            if(!status && recv_all(fd,buffer,msg.bytes)!=0)break;
            if(!status && nopen_page_checksum(buffer,msg.bytes)!=incoming_checksum)
                status=-EBADMSG;
            if(!status) {
                diag_worker("GPU_ENTER",&msg,status,began);
                status=nopen_blk_write(b,msg.offset,buffer,msg.bytes);
                diag_worker("GPU_RETURN",&msg,status,began);
            }
        } else if(!status) {
            diag_worker("GPU_ENTER",&msg,status,began);
            status=nopen_blk_read(b,msg.offset,buffer,msg.bytes);
            diag_worker("GPU_RETURN",&msg,status,began);
        }
        if(status<0) {
            fprintf(stderr,
              "NOPEN_WORKER_IO_FATAL backend=%s op=%s"
              " offset=%" PRIu64 " bytes=%u errno=%d"
              " backend_error=%s failed_page_recovery=NO\n",
              backend==NOPEN_STORE_OPENCL?"opencl":"mock",
              msg.op==NOPEN_IPC_READ?"READ":"WRITE",
              msg.offset,msg.bytes,-status,
              nopen_blk_last_error(b));
            fflush(stderr);
        }
        msg.status=status;
        msg.checksum=(status==0 && msg.op==NOPEN_IPC_READ)
                     ?nopen_page_checksum(buffer,msg.bytes):incoming_checksum;
        if(send_all(fd,&msg,sizeof(msg))!=0)break;
        if(status==0 && msg.op==NOPEN_IPC_READ &&
           send_all(fd,buffer,msg.bytes)!=0)break;
        diag_worker("RESPONSE_SENT",&msg,status,began);
        if(status<0)break; /* No silent fallback after any corrupted I/O. */
    }
    free(buffer);
    nopen_blk_close(b);
    close(fd);
    return 0;
}
