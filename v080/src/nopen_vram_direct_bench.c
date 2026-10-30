#define _POSIX_C_SOURCE 200809L
/*
 * Nopen v0.8 alpha.10 VRAM-DIRECT test.
 * No SSD shadow, no Btrfs, no fallocate(), no O_DIRECT/O_DSYNC,
 * no disk write-through, no kernel SWAP and no block device creation.
 * The existing isolated OpenCL GPU worker is the ONLY page-data store.
 * Every WRITE ACK waits for the GPU write result, never for an SSD write.
 */
#include "nopen_isolated_io.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define MIB ((size_t)1024u*1024u)
#define KIB ((size_t)1024u)
#define MAX_SLOTS 1024u
#define SEGMENT_MIB 256u
static size_t slot_index(size_t index,size_t count,size_t capacity,int spread) {
    if(!spread)return index;
    if(count<=1)return 0;
    /* Monotone unique locations spanning first through last GPU page. */
    return (index*(capacity-1u))/(count-1u);
}

static double now_us(void) {
    struct timespec t={0};
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return (double)t.tv_sec*1.0e6+(double)t.tv_nsec/1000.0;
}
static int cmp_d(const void *a,const void *b) {
    const double x=*(const double*)a,y=*(const double*)b;
    return (x>y)-(x<y);
}
static int kernel_gpu_swap_active(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 1;
    char line[512];
    int bad=0;
    if(!fgets(line,sizeof line,f)){fclose(f);return 1;}
    while(fgets(line,sizeof line,f)) {
        if(!strncmp(line,"/dev/ublkb",10) || !strncmp(line,"/dev/nbd",8))
            bad=1;
    }
    if(ferror(f))bad=1;
    fclose(f);
    return bad;
}
static void pattern(unsigned char *p,size_t bytes,size_t op,size_t index) {
    for(size_t j=0;j<bytes;j++)
        p[j]=(unsigned char)((op*37u + index*11u + j*19u) & 255u);
}
int main(int argc,char **argv) {
    const char *worker=NULL;
    nopen_backend backend=0;
    size_t mib=4,ops=2000,io_kib=4;
    int spread=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("Nopen VRAM-DIRECT: --backend mock|opencl"
                 " --capacity-mib 4|256|4096 --io-kib 4|64"
                 " --operations 100..10000 --worker /absolute/binary."
                 " [--spread-across-vram]"
                 " NO SSD shadow, NO kernel SWAP, NO root.");
            return 0;
        }
        if(!strcmp(argv[i],"--spread-across-vram")) {
            spread=1;continue;
        }
        if(i+1>=argc)return 2;
        const char *key=argv[i++],*v=argv[i];
        if(!strcmp(key,"--worker"))worker=v;
        else if(!strcmp(key,"--backend")) {
            if(!strcmp(v,"mock"))backend=NOPEN_STORE_MOCK;
            else if(!strcmp(v,"opencl"))backend=NOPEN_STORE_OPENCL;
            else return 2;
        } else if(!strcmp(key,"--capacity-mib")||
                  !strcmp(key,"--operations")||!strcmp(key,"--io-kib")) {
            char *end=NULL;errno=0;
            const unsigned long number=strtoul(v,&end,10);
            if(errno||end==v||*end)return 2;
            if(!strcmp(key,"--capacity-mib"))mib=(size_t)number;
            else if(!strcmp(key,"--operations"))ops=(size_t)number;
            else io_kib=(size_t)number;
        } else return 2;
    }
    if(geteuid()==0 || kernel_gpu_swap_active() ||
       !backend || !worker || worker[0]!='/' ||
       (mib!=4 && mib!=256 && mib!=4096) ||
       (io_kib!=4 && io_kib!=64) || ops<100 || ops>10000)
    {
        fputs("V080_VRAM_DIRECT_REFUSED no_root_no_active_ublk"
              " exact_capacity_and_worker_required\n",stderr);
        return 2;
    }
    const size_t block=io_kib*KIB;
    const size_t slots_capacity=mib*MIB/block;
    const size_t slots=slots_capacity<MAX_SLOTS?slots_capacity:MAX_SLOTS;
    if(!slots)return 2;
    unsigned char *expected=calloc(slots,block);
    unsigned char *data=malloc(block),*got=malloc(block);
    unsigned char *written=calloc(slots,1);
    double *write_latency=calloc(ops,sizeof(*write_latency));
    double *read_latency=calloc(slots+ops/97u+1u,sizeof(*read_latency));
    nopen_isolated_io *gpu=NULL;
    int result=3;
    size_t reads=0;
    if(!expected||!data||!got||!written||!write_latency||!read_latency)
        goto end;
    printf("NOPEN_VRAM_DIRECT_START backend=%s capacity_mib=%zu"
           " io_kib=%zu operations=%zu shadow_file=NONE"
           " disk_reads=0 disk_writes=0 kernel_swap=NO"
           " spread_across_vram=%s\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",mib,io_kib,ops,
           spread?"YES":"NO");
    fflush(stdout);
    double init_begin=now_us();
    if(nopen_isolated_open(&gpu,backend,mib,2000,worker)!=0) {
        fputs("NOPEN_VRAM_DIRECT_FAIL phase=gpu_open\n",stderr);
        goto end;
    }
    const double init_s=(now_us()-init_begin)/1e6;
    const double ops_begin=now_us();
    for(size_t i=0;i<ops;i++) {
        const size_t index=(i*131u+i/7u)%slots;
        const uint64_t off=(uint64_t)slot_index(index,slots,slots_capacity,spread)
                            *(uint64_t)block;
        pattern(data,block,i,index);
        const double begun=now_us();
        if(nopen_isolated_write(gpu,off,data,block)!=0) {
            fprintf(stderr,"NOPEN_VRAM_DIRECT_FAIL phase=gpu_write op=%zu\n",i);
            goto end;
        }
        write_latency[i]=now_us()-begun;
        memcpy(expected+index*block,data,block);
        written[index]=1;
        if(i%97u==0) {
            const double rb=now_us();
            if(nopen_isolated_read(gpu,off,got,block)!=0 ||
               memcmp(got,expected+index*block,block)!=0) {
                fprintf(stderr,"NOPEN_VRAM_DIRECT_FAIL phase=read_after_write"
                               " op=%zu\n",i);
                goto end;
            }
            read_latency[reads++]=now_us()-rb;
        }
    }
    const double transfer_s=(now_us()-ops_begin)/1e6;
    size_t verified=0;
    for(size_t k=0;k<slots;k++) {
        const uint64_t off=(uint64_t)slot_index(k,slots,slots_capacity,spread)
                            *(uint64_t)block;
        memset(got,0xA5,block);
        const double begun=now_us();
        if(nopen_isolated_read(gpu,off,got,block)!=0 ||
           memcmp(got,expected+k*block,block)!=0) {
            fprintf(stderr,"NOPEN_VRAM_DIRECT_FAIL phase=verify slot=%zu\n",k);
            goto end;
        }
        read_latency[reads++]=now_us()-begun;
        verified++;
    }
    /* A page OUTSIDE our written window must read zero. The worker's
     * per-page metadata enforces zero, not undefined raw GPU bytes. */
    if(slots<slots_capacity) {
        /* Find an actually UNWRITTEN block even in full-span mode. */
        size_t untouched=0;
        for(size_t probe=0;probe<slots_capacity;probe++) {
            int seen=0;
            for(size_t k=0;k<slots;k++)
                if(slot_index(k,slots,slots_capacity,spread)==probe){
                    seen=1;break;
                }
            if(!seen){untouched=probe;break;}
        }
        const uint64_t off=(uint64_t)untouched*(uint64_t)block;
        memset(got,0xA5,block);
        if(nopen_isolated_read(gpu,off,got,block)!=0)goto end;
        for(size_t k=0;k<block;k++)if(got[k]!=0) {
            fputs("NOPEN_VRAM_DIRECT_FAIL phase=unwritten_zero\n",stderr);
            goto end;
        }
    }
    size_t edge_checks=0;
    if(spread) {
        /* 256MiB OpenCL segment boundaries, including last block in
         * each segment and first block in the next. No SSD dependency. */
        const size_t seg_blocks=(SEGMENT_MIB*MIB)/block;
        for(size_t start=0;start<slots_capacity;start+=seg_blocks) {
            const size_t offsets[3]={
                start,
                start+seg_blocks-1u<slots_capacity?
                    start+seg_blocks-1u:slots_capacity-1u,
                start+seg_blocks<slots_capacity?
                    start+seg_blocks:slots_capacity-1u
            };
            for(size_t e=0;e<3;e++) {
                const size_t page=offsets[e];
                if(page>=slots_capacity)continue;
                pattern(data,block,ops+edge_checks,page);
                if(nopen_isolated_write(gpu,(uint64_t)page*block,data,block)!=0||
                   nopen_isolated_read(gpu,(uint64_t)page*block,got,block)!=0||
                   memcmp(data,got,block)!=0) {
                    fprintf(stderr,"NOPEN_VRAM_DIRECT_FAIL phase=segment_edge"
                                   " block=%zu\n",page);
                    goto end;
                }
                edge_checks++;
            }
        }
    }
    nopen_isolated_stats status={0};
    if(nopen_isolated_stats_get(gpu,&status)!=0||
       status.io_errors||status.timeouts||status.child_crashes)
        goto end;
    qsort(write_latency,ops,sizeof(*write_latency),cmp_d);
    qsort(read_latency,reads,sizeof(*read_latency),cmp_d);
    printf("NOPEN_VRAM_DIRECT_PASS backend=%s capacity_mib=%zu io_kib=%zu"
           " operations=%zu verified_slots=%zu"
           " init_s=%.3f transfer_s=%.3f"
           " write_p50_us=%.2f write_p95_us=%.2f"
           " read_p50_us=%.2f read_p95_us=%.2f"
           " effective_write_mib_s=%.2f"
           " shadow_file=NONE ssd_write_through=NO"
           " GPU_ACK=YES kernel_swap=NO"
           " spread_across_vram=%s segment_edge_checks=%zu"
           " full_capacity_bytes_written=NO\n",
           backend==NOPEN_STORE_OPENCL?"opencl":"mock",mib,io_kib,
           ops,verified,init_s,transfer_s,
           write_latency[ops/2u],write_latency[(ops*95u)/100u],
           read_latency[reads/2u],read_latency[(reads*95u)/100u],
           (double)(ops*block)/(double)MIB/transfer_s,
           spread?"YES":"NO",edge_checks);
    result=0;
end:
    nopen_isolated_close(gpu);
    free(expected);free(data);free(got);free(written);
    free(write_latency);free(read_latency);
    return result;
}
