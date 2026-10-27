#include "nopen_vm_policy.h"
#include <stdint.h>
#define GIB_BYTES (UINT64_C(1024)*1024*1024)
#define GIB_KIB (UINT64_C(1024)*1024)
#define REQUIRED_SPARE_VRAM_BYTES (UINT64_C(4)*GIB_BYTES)
#define REQUESTED_VRAM_BYTES (UINT64_C(64)*1024*1024)
static nopen_vm_advice advice(nopen_vm_decision decision,unsigned pages,const char *reason) {
    nopen_vm_advice a={decision,pages,reason};
    return a;
}
nopen_vm_advice nopen_vm_evaluate(const nopen_vm_snapshot *s) {
    if(!s || !s->mem_known || !s->psi_known || !s->vram_known || !s->swaps_checked)
        return advice(NOPEN_VM_REFUSE,0,"telemetry_missing");
    if(s->nbd_swap_active)
        return advice(NOPEN_VM_REFUSE,0,"active_nbd_swap");
    if(s->mem_total_kib==0 || s->mem_available_kib>s->mem_total_kib ||
       s->vram_total_bytes==0 || s->vram_used_bytes>s->vram_total_bytes ||
       s->memory_psi_some_10_centi_percent>10000u ||
       s->memory_psi_full_10_centi_percent>10000u ||
       s->memory_psi_full_10_centi_percent>s->memory_psi_some_10_centi_percent)
        return advice(NOPEN_VM_REFUSE,0,"invalid_telemetry");
    /* Fail closed: do not recommend GPU prefetch during memory pressure. */
    if(s->mem_available_kib<UINT64_C(2)*GIB_KIB ||
       s->mem_available_kib<s->mem_total_kib/5u)
        return advice(NOPEN_VM_PAUSE,0,"low_host_memory");
    uint64_t spare=s->vram_total_bytes-s->vram_used_bytes;
    if(spare < REQUIRED_SPARE_VRAM_BYTES + REQUESTED_VRAM_BYTES)
        return advice(NOPEN_VM_PAUSE,0,"low_gpu_vram");
    if(s->memory_psi_full_10_centi_percent>=200u ||
       s->memory_psi_some_10_centi_percent>=1000u)
        return advice(NOPEN_VM_PAUSE,0,"severe_memory_pressure");
    if(s->memory_psi_full_10_centi_percent>=25u ||
       s->memory_psi_some_10_centi_percent>=200u)
        return advice(NOPEN_VM_CONSERVATIVE,4,"moderate_memory_pressure");
    return advice(NOPEN_VM_NORMAL,16,"healthy_capacity");
}
