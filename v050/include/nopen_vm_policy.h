#ifndef NOPEN_VM_POLICY_H
#define NOPEN_VM_POLICY_H
#include <stdint.h>
/* Freestanding C11 policy only; no Linux, POSIX, GPU, allocations or libc. */
typedef struct {
    uint64_t mem_total_kib;
    uint64_t mem_available_kib;
    uint64_t vram_total_bytes;
    uint64_t vram_used_bytes;
    uint32_t memory_psi_some_10_centi_percent;
    uint32_t memory_psi_full_10_centi_percent;
    unsigned char mem_known;
    unsigned char vram_known;
    unsigned char psi_known;
    unsigned char swaps_checked;
    unsigned char nbd_swap_active;
} nopen_vm_snapshot;
typedef enum {
    NOPEN_VM_REFUSE = 0,
    NOPEN_VM_PAUSE = 1,
    NOPEN_VM_CONSERVATIVE = 2,
    NOPEN_VM_NORMAL = 3
} nopen_vm_decision;
typedef struct {
    nopen_vm_decision decision;
    unsigned prefetch_pages; /* recommendation, never a side effect */
    const char *reason;      /* constant literal */
} nopen_vm_advice;
/* Never actuates kernel memory reclaim. Fail closed if telemetry absent. */
nopen_vm_advice nopen_vm_evaluate(const nopen_vm_snapshot *snapshot);
#endif
