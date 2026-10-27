#define _POSIX_C_SOURCE 200809L
#include "nopen_vm_sensor.h"
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Linux-zen VM sensor, strictly read-only (no swap/reclaim/IOCTL/kernel writes).
 * Full VM integration is a future isolated test, NEVER on a production host.
 * Device-wide VRAM accounting cannot prove a specific buffer's residency. */

static const char *decision_name(nopen_vm_decision d) {
    switch(d){
    case NOPEN_VM_NORMAL:return "normal";
    case NOPEN_VM_CONSERVATIVE:return "conservative";
    case NOPEN_VM_PAUSE:return "pause";
    default:return "refuse";
    }
}
static int sample(unsigned index) {
    nopen_vm_snapshot s={0};
    char pci[512]={0};
    /* Preserve actual sensor failures as a visible refused result. */
    int status=nopen_vm_snapshot_linux(&s,pci,sizeof(pci));
    nopen_vm_advice a=nopen_vm_evaluate(&s);
    printf("VM_SAMPLE version=0.5.0-dev sample=%u "
           "mem_available_mib=%"PRIu64" mem_total_mib=%"PRIu64" "
           "psi_some_avg10_centipct=%"PRIu32" psi_full_avg10_centipct=%"PRIu32" "
           "vram_total_mib=%"PRIu64" vram_used_mib=%"PRIu64" "
           "nbd_swap_active=%u decision=%s recommended_prefetch_pages=%u "
           "reason=%s proc_memory_ok=%d psi_ok=%d swaps_ok=%d gpu_ok=%d "
           "policy_only=YES no_global_swap=YES\n",
           index,s.mem_available_kib/1024,s.mem_total_kib/1024,
           s.memory_psi_some_10_centi_percent,
           s.memory_psi_full_10_centi_percent,
           s.vram_total_bytes/(1024u*1024u),
           s.vram_used_bytes/(1024u*1024u),
           (unsigned)s.nbd_swap_active,decision_name(a.decision),
           a.prefetch_pages,a.reason,(int)s.mem_known,(int)s.psi_known,
           (int)s.swaps_checked,(int)s.vram_known);
    fflush(stdout);
    return status==0?0:3;
}
int main(int argc,char **argv) {
    unsigned n=1;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--help")==0) {
            puts("Nopen v0.5 VM read-only sensor: [--watch 1..10]; never modifies swap/VM");
            return 0;
        }
        if(strcmp(argv[i],"--watch")!=0 || i+1>=argc)return 2;
        char *end=NULL;errno=0;
        unsigned long x=strtoul(argv[++i],&end,10);
        if(errno || end==argv[i] || *end || x<1 || x>10)return 2;
        n=(unsigned)x;
    }
    if(geteuid()==0){
        fputs("VM_PROBE_REFUSED root execution not required\n",stderr);return 2;
    }
    for(unsigned i=0;i<n;i++){
        if(sample(i+1)!=0)return 3;
        if(i+1<n){
            struct timespec req={.tv_sec=1,.tv_nsec=0};
            while(nanosleep(&req,&req)!=0)if(errno!=EINTR)return 3;
        }
    }
    puts("VM_PROBE_PASS read_only=YES kernel_swap_untouched=YES");
    return 0;
}
