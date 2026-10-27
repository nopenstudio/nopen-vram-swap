#define _POSIX_C_SOURCE 200809L
#include "nopen_vm_sensor.h"
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Bounded Linux userfaultfd child-process supervisor.
 * THIS IS NOT A KERNEL MODULE, GLOBAL SWAP OR DIRECT RECLAIM INTEGRATION.
 * All GPU operations are in a separate ordinary user process, not VM reclaim.
 * Periodic read-only PSI/AMDGPU samples are advisory and incomplete. */
static double seconds(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static void snooze(void) {
    struct timespec req={.tv_sec=0,.tv_nsec=100000000L};
    while(nanosleep(&req,&req)!=0 && errno==EINTR){}
}
static int positive(const char *p,size_t *out) {
    if(!p||!*p||*p=='-'||*p=='+')return -1;
    errno=0;char *end=NULL;
    unsigned long long value=strtoull(p,&end,10);
    if(errno||end==p||*end||value==0||value>SIZE_MAX)return -1;
    *out=(size_t)value;return 0;
}
static const char *decision_name(nopen_vm_decision d) {
    switch(d) {
    case NOPEN_VM_NORMAL:return "normal";
    case NOPEN_VM_CONSERVATIVE:return "conservative";
    case NOPEN_VM_PAUSE:return "pause";
    default:return "refuse";
    }
}
static void terminate_child(pid_t pid) {
    if(kill(pid,SIGTERM)<0 && errno!=ESRCH)
        fprintf(stderr,"VM_GATE_TERMINATE_WARN errno=%d\n",errno);
    for(int i=0;i<10;i++) {
        pid_t rc=waitpid(pid,NULL,WNOHANG);
        if(rc==pid || (rc<0&&errno==ECHILD))return;
        snooze();
    }
    if(kill(pid,SIGKILL)<0 && errno!=ESRCH)
        fprintf(stderr,"VM_GATE_KILL_WARN errno=%d\n",errno);
    while(waitpid(pid,NULL,0)<0 && errno==EINTR){}
}
int main(int argc,char **argv) {
    const char *pager=NULL,*backend=NULL;
    size_t pool=64,logical=1024,ram=32,ops=1000,seconds_limit=15;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("nopen-vm-gate --pager /path/nopen-vram-demand --backend mock|opencl "
                 "--pool-mib 4..64 --logical-pages 128..16384 --ram-pages 8..64 "
                 "--operations 100..10000 --timeout-seconds 5..60");
            puts("Controls only child userfaultfd process, no kernel reclaim or swap writes.");
            return 0;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*v=argv[i];
        if(!strcmp(key,"--pager"))pager=v;
        else if(!strcmp(key,"--backend"))backend=v;
        else {
            size_t *dest=NULL;
            if(!strcmp(key,"--pool-mib"))dest=&pool;
            else if(!strcmp(key,"--logical-pages"))dest=&logical;
            else if(!strcmp(key,"--ram-pages"))dest=&ram;
            else if(!strcmp(key,"--operations"))dest=&ops;
            else if(!strcmp(key,"--timeout-seconds"))dest=&seconds_limit;
            if(!dest||positive(v,dest)!=0)return 2;
        }
    }
    if(!pager||pager[0]!='/'||!backend ||
       (strcmp(backend,"mock")&&strcmp(backend,"opencl")) ||
       pool<4||pool>64||logical<128||logical>16384 ||
       logical>pool*256||ram<8||ram>64||ram>=logical||
       ops<100||ops>10000||seconds_limit<5||seconds_limit>60)
        return 2;
    if(geteuid()==0) {
        fputs("VM_GATE_REFUSED root not required\n",stderr);
        return 3;
    }
    nopen_vm_snapshot sample={0};
    char pci[512]={0};
    if(nopen_vm_snapshot_linux(&sample,pci,sizeof(pci))!=0) {
        fputs("VM_GATE_REFUSED sensor unavailable\n",stderr);
        return 3;
    }
    nopen_vm_advice advice=nopen_vm_evaluate(&sample);
    if(advice.decision<NOPEN_VM_CONSERVATIVE) {
        fprintf(stderr,"VM_GATE_REFUSED reason=%s\n",advice.reason);
        return 3;
    }
    /* Strictly no kernel hooks. The child is the already regression-tested
     * UFFD handler. A PSI drop causes process termination, NOT sleeping inside
     * kernel reclaim or introducing a synchronous GPU dependency. */
    printf("VM_GATE_ALLOW backend=%s decision=%s reason=%s "
           "recommended_prefetch_pages=%u prefetch_applied=NO "
           "kernel_reclaim_hook=NO system_swap=NO\n",
           backend,decision_name(advice.decision),advice.reason,
           advice.prefetch_pages);
    fflush(stdout);
    /* Buffer child output in an anonymous temporary file. It cannot block
     * on a full pipe and the supervisor can REQUIRE a real STRESS_PASS proof. */
    FILE *child_log=tmpfile();
    if(!child_log){perror("tmpfile");return 4;}
    pid_t child=fork();
    if(child<0){perror("fork");fclose(child_log);return 4;}
    if(child==0) {
        if(dup2(fileno(child_log),STDOUT_FILENO)<0 ||
           dup2(fileno(child_log),STDERR_FILENO)<0) _exit(127);
        fclose(child_log);
        char poolbuf[16],logicalbuf[16],rambuf[16],opsbuf[16];
        snprintf(poolbuf,sizeof poolbuf,"%zu",pool);
        snprintf(logicalbuf,sizeof logicalbuf,"%zu",logical);
        snprintf(rambuf,sizeof rambuf,"%zu",ram);
        snprintf(opsbuf,sizeof opsbuf,"%zu",ops);
        char *const args[]={(char*)pager,"--backend",(char*)backend,
            "--pool-mib",poolbuf,"--logical-pages",logicalbuf,
            "--ram-pages",rambuf,"--stress","--operations",opsbuf,
            "--seed","387472","--hot-pages","64","--hot-percent","70",
            "--write-percent","50",NULL};
        execv(pager,args);
        perror("execv pager");
        _exit(127);
    }
    double begin=seconds();
    int code=0,status=0;
    unsigned samples=0;
    for(;;) {
        pid_t rc=waitpid(child,&status,WNOHANG);
        if(rc==child) {
            rewind(child_log);
            char line[1024];
            int proof=0;
            char backend_field[48];
            (void)snprintf(backend_field,sizeof backend_field,"backend=%s",backend);
            while(fgets(line,sizeof line,child_log)) {
                fputs(line,stdout);
                if(strncmp(line,"STRESS_PASS ",12)==0 &&
                   strstr(line,backend_field)!=NULL &&
                   strstr(line,"no_global_swap=YES")!=NULL) {
                    const char *tag=strstr(line," verified_final_pages=");
                    size_t checked=0;
                    if(tag && sscanf(tag," verified_final_pages=%zu",&checked)==1 &&
                       checked==logical)proof=1;
                }
            }
            fclose(child_log);
            if(WIFEXITED(status)&&WEXITSTATUS(status)==0 && proof) {
                printf("VM_GATE_PASS child_exit=0 observations=%u "
                       "verified_by_child=YES no_global_swap=YES "
                       "kernel_reclaim_hook=NO\n",samples);
                return 0;
            }
            fprintf(stderr,"VM_GATE_FAILED child_status=%d proof=%d\n",status,proof);
            return 5;
        }
        if(rc<0) {
            perror("waitpid");
            terminate_child(child);
            fclose(child_log);
            return 5;
        }
        if(begin<0 || seconds()-begin> (double)seconds_limit) {
            fputs("VM_GATE_ABORT deadline exceeded\n",stderr);
            code=6;break;
        }
        nopen_vm_snapshot next={0};
        char current_gpu[512]={0};
        if(nopen_vm_snapshot_linux(&next,current_gpu,sizeof(current_gpu))!=0 ||
           strcmp(current_gpu,pci)!=0) {
            fputs("VM_GATE_ABORT telemetry or GPU device changed\n",stderr);
            code=7;break;
        }
        nopen_vm_advice new_advice=nopen_vm_evaluate(&next);
        if(new_advice.decision<NOPEN_VM_CONSERVATIVE) {
            fprintf(stderr,"VM_GATE_ABORT pressure_or_vram reason=%s\n",
                    new_advice.reason);
            code=8;break;
        }
        samples++;
        snooze();
    }
    terminate_child(child);
    fclose(child_log);
    printf("VM_GATE_STOP isolated_child_terminated=YES kernel_swap_untouched=YES\n");
    return code;
}
