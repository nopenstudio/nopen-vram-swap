#include "nopen_vm_policy.h"
#include <stdio.h>
#define ASSERT(x) do { if(!(x)){fprintf(stderr,"VM_POLICY_FAIL line=%d: %s\n",__LINE__,#x);return 1;} }while(0)
#define GiB (1024ULL*1024ULL*1024ULL)
int main(void) {
    nopen_vm_snapshot s={0};
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_REFUSE);
    ASSERT(nopen_vm_evaluate(0).decision==NOPEN_VM_REFUSE);
    s.mem_known=1;s.psi_known=1;s.vram_known=1;s.swaps_checked=1;
    s.mem_total_kib=8u*1024u*1024u;
    s.mem_available_kib=6u*1024u*1024u;
    s.vram_total_bytes=16u*GiB;
    s.vram_used_bytes=GiB;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_NORMAL);
    ASSERT(nopen_vm_evaluate(&s).prefetch_pages==16u);
    s.memory_psi_some_10_centi_percent=250u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_CONSERVATIVE);
    ASSERT(nopen_vm_evaluate(&s).prefetch_pages==4u);
    s.memory_psi_some_10_centi_percent=1100u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_PAUSE);
    s.memory_psi_some_10_centi_percent=0u;
    s.memory_psi_full_10_centi_percent=210u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_REFUSE); /* full > some contradictory */
    s.memory_psi_some_10_centi_percent=500u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_PAUSE);
    s.memory_psi_some_10_centi_percent=0u;s.memory_psi_full_10_centi_percent=0u;
    s.mem_available_kib=1024u*1024u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_PAUSE);
    s.mem_available_kib=6u*1024u*1024u;
    s.vram_used_bytes=13u*GiB;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_PAUSE);
    s.vram_used_bytes=GiB;s.nbd_swap_active=1u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_REFUSE);
    s.nbd_swap_active=0u;s.vram_used_bytes=s.vram_total_bytes+1u;
    ASSERT(nopen_vm_evaluate(&s).decision==NOPEN_VM_REFUSE);
    puts("VM_POLICY_PASS freestanding=YES thresholds=YES nbd_guard=YES fail_closed=YES");
    return 0;
}
