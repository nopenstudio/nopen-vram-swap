#define _POSIX_C_SOURCE 200809L
/* Offline 1/2/4GiB scale + latency sweep. Never installs a block device. */
#include "nopen_shadow.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define PAGE NOPEN_PAGE_SIZE
#define MIB (1024u*1024u)
#define SAMPLES 256u
static double microseconds(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return t.tv_sec*1000000.0+t.tv_nsec/1000.0;
}
static int ascending(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
static double percentile(double values[SAMPLES],unsigned pct) {
    double ordered[SAMPLES];
    memcpy(ordered,values,sizeof ordered);
    qsort(ordered,SAMPLES,sizeof(double),ascending);
    size_t at=((size_t)pct*SAMPLES+99u)/100u;
    if(at<1)at=1;
    if(at>SAMPLES)at=SAMPLES;
    return ordered[at-1];
}
static int swap_is_inactive(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char dev[256]={0};
        if(sscanf(line,"%255s",dev)!=1)continue;
        if(strncmp(dev,"/dev/ublkb",11)==0 ||
           strncmp(dev,"/dev/nbd",8)==0) {
            fclose(f);
            return 0;
        }
    }
    int good=!ferror(f);
    fclose(f);
    return good;
}
static void patterned(unsigned char *dst,uint64_t page) {
    for(size_t i=0;i<PAGE;i++)
        dst[i]=(unsigned char)((page*131u+i*17u+(i>>7))&255u);
}
static int all_zero(const unsigned char *buf) {
    for(size_t i=0;i<PAGE;i++)
        if(buf[i]!=0)return 0;
    return 1;
}
static int mem_available_mib(void) {
    FILE *f=fopen("/proc/meminfo","r");
    if(!f)return -1;
    char line[256];
    int result=-1;
    while(fgets(line,sizeof line,f)) {
        unsigned long long kib=0;
        if(sscanf(line,"MemAvailable: %llu kB",&kib)==1) {
            result=(int)(kib/1024ull);
            break;
        }
    }
    fclose(f);
    return result;
}
int main(int argc,char **argv) {
    /* Mock only in alpha.1; OpenCL capacity pilots are a future explicit
     * non-swap stage, not silently enabled by changing an environment var. */
    if(argc!=4) {
        fputs("Usage: nopen-v080-scale /absolute/mock-worker /absolute/new-shadow-file 1024|2048|4096\n",stderr);
        return 2;
    }
    char *end=NULL;
    errno=0;
    unsigned long capacity=strtoul(argv[3],&end,10);
    if(errno || end==argv[3] || *end ||
       (capacity!=1024 && capacity!=2048 && capacity!=4096) ||
       argv[1][0]!='/' || argv[2][0]!='/' ||
       geteuid()==0 || !swap_is_inactive()) {
        fputs("V080_SCALE_REFUSED guard=capacity_or_real_swap_or_root\n",stderr);
        return 2;
    }
    /* Require ample disk; this uses a real, fully preinitialized shadow.
     * Never thin-provision/overcommit a live OS filesystem. */
    char directory[4096];
    const char *slash=strrchr(argv[2],'/');
    if(!slash || slash==argv[2] ||
       (size_t)(slash-argv[2])>=sizeof directory)return 2;
    memcpy(directory,argv[2],(size_t)(slash-argv[2]));
    directory[slash-argv[2]]=0;
    struct statvfs fs={0};
    if(statvfs(directory,&fs)!=0)return 2;
    uint64_t free_bytes=(uint64_t)fs.f_bavail*fs.f_frsize;
    const uint64_t needed=(uint64_t)(capacity+512u)*MIB;
    if(free_bytes<needed || mem_available_mib()<1024) {
        fprintf(stderr,"V080_SCALE_REFUSED disk_or_ram_headroom"
                " free_bytes=%" PRIu64 " needed=%" PRIu64 "\n",
                free_bytes,needed);
        return 3;
    }
    if(access(argv[2],F_OK)==0) {
        fputs("V080_SCALE_REFUSED output_path_already_exists\n",stderr);
        return 3;
    }
    nopen_shadow *shadow=NULL;
    const size_t pages=(size_t)capacity*(MIB/PAGE);
    const double started=microseconds();
    int rc=nopen_shadow_open(&shadow,NOPEN_STORE_MOCK,(size_t)capacity,
                             2000,argv[1],argv[2]);
    if(rc!=0) {
        fprintf(stderr,"V080_SCALE_REFUSED shadow_open_rc=%d\n",rc);
        return 4;
    }
    int result=5;
    unsigned char expected[PAGE],got[PAGE];
    double write_us[SAMPLES],gpu_read_us[SAMPLES],disk_read_us[SAMPLES];
    const size_t last=pages-1;
    /* Verify untouched distant pages read as zeros; selected pages cross
     * 256MiB GPU segment boundaries and end on the final 4KiB block. */
    for(size_t i=0;i<SAMPLES;i++) {
        const size_t p=(last*i)/(SAMPLES-1);
        memset(got,0xE3,PAGE);
        if(nopen_shadow_read(shadow,(uint64_t)p*PAGE,got,PAGE)!=0 ||
           !all_zero(got)) {
            fprintf(stderr,"V080_SCALE_ZERO_MISMATCH page=%zu\n",p);
            goto finish;
        }
    }
    /* Write sparse, deterministic samples spanning the *full* logical
     * address space without allocating 4GiB of anonymous RAM. */
    for(size_t i=0;i<SAMPLES;i++) {
        const size_t p=(last*i)/(SAMPLES-1);
        patterned(expected,(uint64_t)p);
        const double before=microseconds();
        if(nopen_shadow_write(shadow,(uint64_t)p*PAGE,expected,PAGE)!=0)
            goto finish;
        write_us[i]=microseconds()-before;
    }
    for(size_t i=0;i<SAMPLES;i++) {
        const size_t p=(last*i)/(SAMPLES-1);
        patterned(expected,(uint64_t)p);
        memset(got,0xAA,PAGE);
        const double before=microseconds();
        if(nopen_shadow_read(shadow,(uint64_t)p*PAGE,got,PAGE)!=0 ||
           memcmp(expected,got,PAGE)!=0) {
            fprintf(stderr,"V080_SCALE_GPU_MISMATCH page=%zu\n",p);
            goto finish;
        }
        gpu_read_us[i]=microseconds()-before;
    }
    if(nopen_shadow_simulate_gpu_loss(shadow)!=0)goto finish;
    for(size_t i=0;i<SAMPLES;i++) {
        const size_t p=(last*i)/(SAMPLES-1);
        patterned(expected,(uint64_t)p);
        memset(got,0xAA,PAGE);
        const double before=microseconds();
        if(nopen_shadow_read(shadow,(uint64_t)p*PAGE,got,PAGE)!=0 ||
           memcmp(expected,got,PAGE)!=0) {
            fprintf(stderr,"V080_SCALE_SHADOW_MISMATCH page=%zu\n",p);
            goto finish;
        }
        disk_read_us[i]=microseconds()-before;
    }
    patterned(expected,(uint64_t)last+1);
    if(nopen_shadow_write(shadow,(uint64_t)last*PAGE,expected,PAGE)!=0 ||
       nopen_shadow_read(shadow,(uint64_t)last*PAGE,got,PAGE)!=0 ||
       memcmp(expected,got,PAGE)!=0)goto finish;
    nopen_shadow_stats stats={0};
    if(nopen_shadow_get_stats(shadow,&stats)!=0 ||
       stats.gpu_active || stats.poisoned || stats.disk_failures ||
       stats.gpu_failures!=1 || stats.backed_bytes!=(uint64_t)capacity*MIB ||
       stats.gpu_reads<SAMPLES || stats.shadow_reads<SAMPLES)
        goto finish;
    printf("V080_SCALE_RESULT capacity_mib=%lu pages=%zu"
           " sampled_written_pages=%u sampled_full_page_verified=%u"
           " last_page_offset=%" PRIu64 " backend=mock"
           " prezero_full_capacity=YES kernel_swap=NO"
           " shadow_worker_loss_verified=YES"
           " write_p50_us=%.1f write_p95_us=%.1f"
           " gpu_read_p50_us=%.1f gpu_read_p95_us=%.1f"
           " fallback_p50_us=%.1f fallback_p95_us=%.1f"
           " elapsed_s=%.3f\n",
           capacity,pages,SAMPLES,SAMPLES,
           (uint64_t)last*PAGE,
           percentile(write_us,50),percentile(write_us,95),
           percentile(gpu_read_us,50),percentile(gpu_read_us,95),
           percentile(disk_read_us,50),percentile(disk_read_us,95),
           (microseconds()-started)/1000000.0);
    printf("V080_SCALE_PASS capacity_mib=%lu page_integrity=YES"
           " SSD_fallback=YES zram_untouched=YES actual_gpu=NO\n",
           capacity);
    result=0;
finish:
    nopen_shadow_close(shadow);
    if(result==0) {
        if(unlink(argv[2])!=0)
            fprintf(stderr,"V080_SCALE_WARNING couldn't unlink completed test shadow: %s\n",argv[2]);
    } else {
        fprintf(stderr,"V080_SCALE_FAILED capacity_mib=%lu preserved_file=%s\n",
                capacity,argv[2]);
    }
    return result;
}
