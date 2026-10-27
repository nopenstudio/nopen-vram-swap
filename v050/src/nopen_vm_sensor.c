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

static int proc_memory(nopen_vm_snapshot *s) {
    FILE *f=fopen("/proc/meminfo","r");
    if(!f)return -1;
    char line[256];
    int got_total=0,got_available=0;
    while(fgets(line,sizeof(line),f)) {
        uint64_t value=0;
        if(sscanf(line,"MemTotal: %"SCNu64" kB",&value)==1) {
            s->mem_total_kib=value;got_total=1;
        } else if(sscanf(line,"MemAvailable: %"SCNu64" kB",&value)==1) {
            s->mem_available_kib=value;got_available=1;
        }
    }
    if(ferror(f)){fclose(f);return -1;}
    fclose(f);
    s->mem_known=(unsigned char)(got_total&&got_available);
    return s->mem_known ? 0 : -1;
}
static int psi_memory(nopen_vm_snapshot *s) {
    FILE *f=fopen("/proc/pressure/memory","r");
    if(!f)return -1;
    char line[512],type[16];
    int some=0,full=0;
    while(fgets(line,sizeof(line),f)) {
        double pct=-1.0;
        if(sscanf(line,"%15s avg10=%lf",type,&pct)!=2)continue;
        if(!isfinite(pct)||pct<0||pct>100){fclose(f);return -1;}
        uint32_t centi=(uint32_t)(pct*100.0+0.5);
        if(strcmp(type,"some")==0) {
            s->memory_psi_some_10_centi_percent=centi;some=1;
        } else if(strcmp(type,"full")==0) {
            s->memory_psi_full_10_centi_percent=centi;full=1;
        }
    }
    if(ferror(f)){fclose(f);return -1;}
    fclose(f);
    s->psi_known=(unsigned char)(some&&full);
    return s->psi_known ? 0 : -1;
}
static int safe_swaps(nopen_vm_snapshot *s) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return -1;
    char line[512];
    if(!fgets(line,sizeof(line),f)){fclose(f);return -1;}
    while(fgets(line,sizeof(line),f)) {
        char name[256]={0};
        if(sscanf(line,"%255s",name)==1 && strncmp(name,"/dev/nbd",8)==0)
            s->nbd_swap_active=1u;
    }
    if(ferror(f)){fclose(f);return -1;}
    fclose(f);
    s->swaps_checked=1u;
    return 0;
}
static int sysfs_u64(const char *path,uint64_t *value) {
    FILE *f=fopen(path,"r");
    if(!f)return -1;
    uint64_t n=0;
    int ok=fscanf(f,"%"SCNu64,&n)==1;
    fclose(f);
    if(!ok)return -1;
    *value=n;return 0;
}
static int sysfs_gpu(nopen_vm_snapshot *s,char *pci_out,size_t capacity) {
    DIR *dir=opendir("/sys/bus/pci/devices");
    if(!dir)return -1;
    char gpu_dir[512]={0},test[640];
    struct dirent *entry;
    int count=0;
    while((entry=readdir(dir))!=NULL) {
        if(entry->d_name[0]=='.')continue;
        int rc=snprintf(test,sizeof(test),"/sys/bus/pci/devices/%s/vendor",entry->d_name);
        if(rc<0||(size_t)rc>=sizeof(test))continue;
        uint64_t vendor=0;
        FILE *f=fopen(test,"r");
        if(!f)continue;
        int got=fscanf(f,"%"SCNx64,&vendor)==1;
        fclose(f);
        if(!got || vendor!=UINT64_C(0x1002))continue;
        rc=snprintf(test,sizeof(test),"/sys/bus/pci/devices/%s/class",entry->d_name);
        if(rc<0||(size_t)rc>=sizeof(test))continue;
        uint64_t dev_class=0;
        f=fopen(test,"r");
        if(!f)continue;
        got=fscanf(f,"%"SCNx64,&dev_class)==1;
        fclose(f);
        if(!got || (dev_class>>16)!=UINT64_C(0x03))continue;
        count++;
        rc=snprintf(gpu_dir,sizeof(gpu_dir),"/sys/bus/pci/devices/%s",entry->d_name);
        if(rc<0||(size_t)rc>=sizeof(gpu_dir)){count=2;break;}
    }
    closedir(dir);
    if(count!=1)return -1; /* refuse mixed/ambiguous GPU topology */
    if(strlen(gpu_dir)+1>capacity)return -1;
    memcpy(pci_out,gpu_dir,strlen(gpu_dir)+1);
    int rc=snprintf(test,sizeof(test),"%s/mem_info_vram_total",gpu_dir);
    if(rc<0||(size_t)rc>=sizeof(test) ||
       sysfs_u64(test,&s->vram_total_bytes)!=0)return -1;
    rc=snprintf(test,sizeof(test),"%s/mem_info_vram_used",gpu_dir);
    if(rc<0||(size_t)rc>=sizeof(test) ||
       sysfs_u64(test,&s->vram_used_bytes)!=0)return -1;
    s->vram_known=1u;
    return 0;
}

int nopen_vm_snapshot_linux(nopen_vm_snapshot *s,char *pci,size_t capacity) {
    if(!s || !pci || capacity<512u)return -1;
    memset(s,0,sizeof(*s));pci[0]=0;
    int a=proc_memory(s);
    int b=psi_memory(s);
    int c=safe_swaps(s);
    int d=sysfs_gpu(s,pci,capacity);
    return a==0&&b==0&&c==0&&d==0?0:-1;
}
