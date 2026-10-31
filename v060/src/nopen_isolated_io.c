#define _POSIX_C_SOURCE 200809L
#include "nopen_isolated_io.h"
#include "nopen_page_core.h"
#include "isolated_protocol.h"
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <inttypes.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
struct nopen_isolated_io {
    int fd;
    pid_t pid;
    size_t pages;
    unsigned timeout_ms;
    nopen_backend backend;
    int alive;
    unsigned char *staging;
    pthread_mutex_t lock;
    nopen_isolated_stats stats;
};
static uint64_t monotonic_ms(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
}
/* Issue #10 diagnostics. Disabled unless explicitly opted in. No page contents. */
static int diag_trace_enabled(void) {
    const char *v=getenv("NOPEN_DIAG_IO_TRACE");
    return v && strcmp(v,"1")==0;
}
static void diag_ipc(nopen_isolated_io *r,const char *phase,
                     uint32_t op,uint64_t off,size_t bytes,
                     const char *stage,int rc,uint64_t elapsed_ms) {
    if(!diag_trace_enabled())return;
    fprintf(stderr,"NOPEN_DIAG_IPC phase=%s t_ms=%" PRIu64
            " worker_pid=%ld op=%s offset=%" PRIu64
            " bytes=%zu stage=%s rc=%d elapsed_ms=%" PRIu64 "\n",
            phase,monotonic_ms(),(long)r->pid,
            op==NOPEN_IPC_READ?"READ":op==NOPEN_IPC_WRITE?"WRITE":"OTHER",
            off,bytes,stage,rc,elapsed_ms);
    fflush(stderr);
}
static int io_wait(int fd,short events,uint64_t deadline) {
    for(;;) {
        uint64_t now=monotonic_ms();
        if(!now || now>=deadline)return -ETIMEDOUT;
        int remaining=(int)(deadline-now);
        struct pollfd p={.fd=fd,.events=events,.revents=0};
        int rc=poll(&p,1,remaining);
        if(rc<0 && errno==EINTR)continue;
        if(rc==0)return -ETIMEDOUT;
        if(rc<0)return -EIO;
        if(p.revents & (POLLNVAL|POLLERR))return -EIO;
        if(p.revents & events)return 0;
        if(p.revents & POLLHUP)return -EPIPE;
    }
}
static int send_exact(int fd,const void *buf,size_t n,uint64_t deadline) {
    const unsigned char *p=buf;
    while(n) {
        int rc=io_wait(fd,POLLOUT,deadline);
        if(rc)return rc;
        ssize_t k=send(fd,p,n,MSG_NOSIGNAL|MSG_DONTWAIT);
        if(k<0 && (errno==EINTR||errno==EAGAIN))continue;
        if(k<=0)return -EPIPE;
        p+=(size_t)k;n-=(size_t)k;
    }
    return 0;
}
static int recv_exact(int fd,void *buf,size_t n,uint64_t deadline) {
    unsigned char *p=buf;
    while(n) {
        int rc=io_wait(fd,POLLIN,deadline);
        if(rc)return rc;
        ssize_t k=recv(fd,p,n,MSG_DONTWAIT);
        if(k<0 && (errno==EINTR||errno==EAGAIN))continue;
        if(k<=0)return -EPIPE;
        p+=(size_t)k;n-=(size_t)k;
    }
    return 0;
}
static void terminate_child(nopen_isolated_io *r) {
    if(!r)return;
    if(r->fd>=0){close(r->fd);r->fd=-1;}
    if(r->pid>0) {
        if(kill(r->pid,SIGKILL)<0 && errno!=ESRCH){}
        /* The kernel can leave a stuck worker uninterruptible in D-state.
         * Never wait indefinitely; the OS will reap it after parent exit.
         * Thus this does NOT prove GPU driver reset safety. */
        for(int i=0;i<10;i++) {
            pid_t got=waitpid(r->pid,NULL,WNOHANG);
            if(got==r->pid || (got<0 && errno==ECHILD))break;
            struct timespec delay={.tv_sec=0,.tv_nsec=10000000L};
            nanosleep(&delay,NULL);
        }
        r->pid=-1;
    }
    r->alive=0;
    r->stats.child_alive=0;
}
static int fail_io(nopen_isolated_io *r,int code,uint32_t op,
                   uint64_t offset,size_t bytes,const char *stage) {
    if(code==-ETIMEDOUT)r->stats.timeouts++;
    else r->stats.child_crashes++;
    r->stats.io_errors++;
    /* The FIRST negative return is the decisive incident. Logging only a
     * classification rather than raw buffer content avoids data disclosure. */
    fprintf(stderr,
        "NOPEN_IPC_FATAL monotonic_ms=%" PRIu64
        " backend=%s worker_pid=%ld op=%s offset=%" PRIu64
        " bytes=%zu stage=%s errno=%d deadline_ms=%u"
        " fail_closed=YES no_global_swap_recovery=YES\n",
        monotonic_ms(),r->backend==NOPEN_STORE_OPENCL?"opencl":"mock",
        (long)r->pid,op==NOPEN_IPC_READ?"READ":
        op==NOPEN_IPC_WRITE?"WRITE":"INJECT",offset,bytes,stage,
        -code,r->timeout_ms);
    fflush(stderr);
    terminate_child(r);
    return code;
}
int nopen_isolated_open(nopen_isolated_io **out,nopen_backend backend,
                       size_t pool_mib,unsigned timeout_ms,
                       const char *absolute_worker_path) {
    if(!out)return -EINVAL;
    *out=NULL;
    if(!absolute_worker_path || *absolute_worker_path!='/' ||
       access(absolute_worker_path,X_OK)!=0 ||
       (backend!=NOPEN_STORE_OPENCL && backend!=NOPEN_STORE_MOCK) ||
       pool_mib<4 || pool_mib>4096 ||
       timeout_ms<50 || timeout_ms>5000)return -EINVAL;
    nopen_isolated_io *r=calloc(1,sizeof(*r));
    if(!r)return -ENOMEM;
    r->fd=-1;
    r->pid=-1;
    r->pages=pool_mib*(1024u*1024u/NOPEN_PAGE_SIZE);
    r->timeout_ms=timeout_ms;
    r->backend=backend;
    r->staging=malloc(NOPEN_BLK_MAX_BYTES);
    if(!r->staging){free(r);return -ENOMEM;}
    if(pthread_mutex_init(&r->lock,NULL)!=0) {
        free(r->staging);free(r);return -ENOMEM;
    }
    int sv[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sv)!=0) {
        pthread_mutex_destroy(&r->lock);free(r->staging);free(r);return -EIO;
    }
    posix_spawn_file_actions_t act;
    if(posix_spawn_file_actions_init(&act)!=0) {
        close(sv[0]);close(sv[1]);
        pthread_mutex_destroy(&r->lock);free(r->staging);free(r);return -EIO;
    }
    /* Close the parent side BEFORE replacing fd 3 to avoid accidental close
     * when socketpair originally returned descriptor 3 for sv[0]. */
    posix_spawn_file_actions_addclose(&act,sv[0]);
    posix_spawn_file_actions_adddup2(&act,sv[1],3);
    if(sv[1]!=3)posix_spawn_file_actions_addclose(&act,sv[1]);
    char mib[16];
    (void)snprintf(mib,sizeof mib,"%zu",pool_mib);
    char *const argv[]={(char*)absolute_worker_path,
                         (char*)(backend==NOPEN_STORE_MOCK?"mock":"opencl"),
                         mib,NULL};
    pid_t pid=-1;
    int spawn_rc=posix_spawn(&pid,absolute_worker_path,&act,NULL,argv,environ);
    posix_spawn_file_actions_destroy(&act);
    close(sv[1]);
    if(spawn_rc!=0) {
        close(sv[0]);
        pthread_mutex_destroy(&r->lock);free(r->staging);free(r);
        return -EIO;
    }
    r->fd=sv[0];r->pid=pid;r->alive=1;r->stats.child_alive=1;
    uint64_t deadline=monotonic_ms()+timeout_ms;
    nopen_ipc_msg ready={0};
    const int rc=recv_exact(r->fd,&ready,sizeof ready,deadline);
    if(rc!=0 || ready.magic!=NOPEN_IPC_MAGIC || ready.op!=NOPEN_IPC_READY ||
       ready.status!=0) {
        terminate_child(r);
        pthread_mutex_destroy(&r->lock);free(r->staging);free(r);
        return rc==-ETIMEDOUT?-ETIMEDOUT:-EIO;
    }
    *out=r;
    return 0;
}
static int transfer(nopen_isolated_io *r,uint32_t op,uint64_t off,
                    const void *src,void *dest,size_t bytes) {
    if(!r || !bytes || bytes>NOPEN_BLK_MAX_BYTES ||
       (bytes%NOPEN_PAGE_SIZE) || (off%NOPEN_PAGE_SIZE) ||
       off/NOPEN_PAGE_SIZE >= r->pages ||
       bytes/NOPEN_PAGE_SIZE > r->pages-off/NOPEN_PAGE_SIZE)return -EINVAL;
    pthread_mutex_lock(&r->lock);
    if(!r->alive) {
        pthread_mutex_unlock(&r->lock);
        return -EIO;
    }
    const uint64_t begin_ms=monotonic_ms();
    diag_ipc(r,"BEGIN",op,off,bytes,"dispatch",0,0);
    uint64_t deadline=begin_ms+r->timeout_ms;
    nopen_ipc_msg req={NOPEN_IPC_MAGIC,op,off,(uint32_t)bytes,0,
                         op==NOPEN_IPC_WRITE?nopen_page_checksum(src,bytes):0};
    const char *stage="send_header";
    int rc=send_exact(r->fd,&req,sizeof(req),deadline);
    if(!rc && op==NOPEN_IPC_WRITE) {
        stage="send_payload";
        rc=send_exact(r->fd,src,bytes,deadline);
    }
    nopen_ipc_msg response={0};
    if(!rc) {
        stage="recv_header";
        rc=recv_exact(r->fd,&response,sizeof response,deadline);
    }
    if(!rc) {
        stage="validate_header";
        if(response.magic!=NOPEN_IPC_MAGIC || response.op!=op ||
           response.offset!=off || response.bytes!=bytes ||
           (op==NOPEN_IPC_WRITE && response.checksum!=req.checksum))rc=-EIO;
        else if(response.status!=0) {
            stage="worker_backend_error";
            rc=response.status<0?response.status:-EIO;
        }
    }
    if(!rc && op==NOPEN_IPC_READ) {
        stage="recv_payload";
        rc=recv_exact(r->fd,r->staging,bytes,deadline);
        if(!rc) {
            stage="validate_checksum";
            if(nopen_page_checksum(r->staging,bytes)!=response.checksum)
                rc=-EBADMSG;
        }
        if(!rc)memcpy(dest,r->staging,bytes);
    }
    /* Log before fail_io terminates the worker, or completion returns. */
    if(diag_trace_enabled())
        diag_ipc(r,rc?"FAIL":"END",op,off,bytes,
                 rc?stage:"complete",rc,monotonic_ms()-begin_ms);
    if(rc)rc=fail_io(r,rc,op,off,bytes,stage);
    else r->stats.operations++;
    pthread_mutex_unlock(&r->lock);
    return rc;
}
int nopen_isolated_read(nopen_isolated_io *r,uint64_t off,void *dest,size_t bytes) {
    if(!dest)return -EINVAL;
    return transfer(r,NOPEN_IPC_READ,off,NULL,dest,bytes);
}
int nopen_isolated_write(nopen_isolated_io *r,uint64_t off,const void *src,size_t bytes) {
    if(!src)return -EINVAL;
    return transfer(r,NOPEN_IPC_WRITE,off,src,NULL,bytes);
}
int nopen_isolated_stats_get(nopen_isolated_io *r,nopen_isolated_stats *stats) {
    if(!r || !stats)return -EINVAL;
    pthread_mutex_lock(&r->lock);
    *stats=r->stats;
    pthread_mutex_unlock(&r->lock);
    return 0;
}
int nopen_isolated_inject_hang(nopen_isolated_io *r) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->lock);
    if(r->backend!=NOPEN_STORE_MOCK || !r->alive) {
        pthread_mutex_unlock(&r->lock);return -EPERM;
    }
    uint64_t deadline=monotonic_ms()+r->timeout_ms;
    nopen_ipc_msg msg={NOPEN_IPC_MAGIC,NOPEN_IPC_HANG_TEST_ONLY,0,0,0,0};
    int rc=send_exact(r->fd,&msg,sizeof msg,deadline);
    if(!rc)rc=recv_exact(r->fd,&msg,sizeof msg,deadline);
    if(!rc)rc=-EIO; /* A hang-injection test is NEVER allowed to pass. */
    rc=fail_io(r,rc,NOPEN_IPC_HANG_TEST_ONLY,0,0,"test_hang");
    pthread_mutex_unlock(&r->lock);
    return rc;
}
/* v0.8.3: deliberately break ONLY mock IPC. No GPU access, no kernel
 * devices, no swap management. Every path terminates the isolated worker and
 * marks the context irrecoverably failed, including an accidentally well-
 * formed response. The read destination remains unmodified after failure. */
static int inject_mock_fault(nopen_isolated_io *r,uint32_t opcode,
                             const char *stage) {
    if(!r)return -EINVAL;
    pthread_mutex_lock(&r->lock);
    if(r->backend!=NOPEN_STORE_MOCK || !r->alive) {
        pthread_mutex_unlock(&r->lock);
        return -EPERM;
    }
    uint64_t deadline=monotonic_ms()+r->timeout_ms;
    nopen_ipc_msg req={NOPEN_IPC_MAGIC,opcode,0,0,0,0};
    nopen_ipc_msg response={0};
    int rc=send_exact(r->fd,&req,sizeof req,deadline);
    if(!rc)rc=recv_exact(r->fd,&response,sizeof response,deadline);
    if(!rc) {
        /* A malformed response is a protocol violation, not an I/O success.
         * A surprising correct response from a fault is also fatal. */
        rc=response.magic!=NOPEN_IPC_MAGIC?-EBADMSG:-EIO;
    }
    rc=fail_io(r,rc,opcode,0,0,stage);
    pthread_mutex_unlock(&r->lock);
    return rc;
}
int nopen_isolated_inject_crash(nopen_isolated_io *r) {
    return inject_mock_fault(r,NOPEN_IPC_EXIT_TEST_ONLY,"test_crash");
}
int nopen_isolated_inject_bad_reply(nopen_isolated_io *r) {
    return inject_mock_fault(r,NOPEN_IPC_BAD_REPLY_TEST_ONLY,"test_bad_reply");
}
void nopen_isolated_close(nopen_isolated_io *r) {
    if(!r)return;
    pthread_mutex_lock(&r->lock);
    terminate_child(r);
    pthread_mutex_unlock(&r->lock);
    pthread_mutex_destroy(&r->lock);
    free(r->staging);free(r);
}
