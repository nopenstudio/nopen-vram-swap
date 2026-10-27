#include "nopen_vm_sensor.h"
#include <stdlib.h>
#include <string.h>
/* Tests only. Never linked into the deployed nopen-vm-gate target. */
int nopen_vm_snapshot_linux(nopen_vm_snapshot *s,char *pci,size_t capacity) {
    static unsigned calls=0;
    if(!s || !pci || capacity<512u)return -1;
    memset(s,0,sizeof(*s));
    const char *name="/sys/bus/pci/devices/FAKE-TEST-ONLY";
    memcpy(pci,name,strlen(name)+1);
    s->mem_known=1;s->vram_known=1;s->psi_known=1;s->swaps_checked=1;
    s->mem_total_kib=8ull*1024*1024;
    s->mem_available_kib=6ull*1024*1024;
    s->vram_total_bytes=16ull*1024*1024*1024;
    s->vram_used_bytes=1ull*1024*1024*1024;
    calls++;
    const char *e=getenv("NOPEN_TEST_FAIL_AFTER");
    if(e && strcmp(e,"2")==0 && calls>=2)
        s->mem_available_kib=1ull*1024*1024;
    return 0;
}
