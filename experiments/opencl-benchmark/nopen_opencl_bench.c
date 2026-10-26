/* SPDX-License-Identifier: MIT
 * Nopen OpenCL isolated VRAM transfer benchmark v0.10.0-alpha.2
 * NO NBD, NO SWAP, NO mlockall, NO kernel modifications.
 */
#define CL_TARGET_OPENCL_VERSION 120
#define _POSIX_C_SOURCE 200809L
#include <CL/cl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>

#define MiB (1024ULL * 1024ULL)
#define PAGE 4096ULL
#define MAX_DEVICES 32
#define MAX_SEGS 64
#define MAX_PAGES 2048

#define CLCHK(expr) do { cl_int _e=(expr); if(_e!=CL_SUCCESS) { \
 fprintf(stderr,"ERROR %s:%d: %s returned OpenCL %d\n", __FILE__, __LINE__, #expr,(int)_e); goto cleanup; } } while(0)

typedef struct {
    cl_platform_id platform;
    cl_device_id device;
    char name[256];
    char vendor[256];
    char platform_name[256];
    cl_bool unified;
    cl_ulong global, max_alloc;
} Device;

typedef struct {
    uint32_t mib, test_mib, chunk_mib, seg_mib, pages;
    int device_index, host_mode, full_verify;
    uint32_t cycles;
} Options;

typedef struct {
    cl_context ctx;
    cl_command_queue q;
    cl_mem src_mem, dst_mem;
    unsigned char *src, *dst;
    int is_pinned;
    size_t len;
} Staging;

typedef struct {
    double wall_ns;
    double gpu_ns;
    uint64_t calls;
} Meter;

static uint64_t now_ns(void) {
    struct timespec ts;
    return clock_gettime(CLOCK_MONOTONIC,&ts) ? 0 : (uint64_t)ts.tv_sec*1000000000ULL+ts.tv_nsec;
}
static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27; x *= UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
static void fill(unsigned char *dest, size_t n, uint64_t seed) {
    size_t k=0;
    uint64_t state=seed;
    while(k<n) {
        state=mix64(state+UINT64_C(0x9e3779b97f4a7c15));
        for(unsigned j=0;j<8 && k<n;j++,k++) dest[k]=(unsigned char)(state>>(8*j));
    }
}
static int parse_num(const char *s, uint32_t *v) {
    if(!s || !*s || *s=='-') return -1;
    char *end=NULL;errno=0;unsigned long x=strtoul(s,&end,10);
    if(errno || !end || *end || x>UINT32_MAX) return -1;
    *v=(uint32_t)x; return 0;
}
static void usage(const char *app) {
    printf("Nopen OpenCL VRAM Benchmark v0.10.0-alpha.2 (NO SWAP / NO NBD)\n");
    printf("Usage: %s [--list] [--mib 256] [--test-mib 64] [--chunk-mib 8] [--seg-mib 256] [--pages 512] [--host both|pageable|pinned] [--device-index N] [--full-verify] [--cycles 1]\n",app);
    puts("Tests separate sequential host->VRAM and VRAM->host throughput and randomized 4KiB page latency.");
    puts("RAM safety: staging limited to <=32 MiB per buffer; pinned test can be skipped if unavailable.");
    puts("Optional --full-verify reads and verifies EVERY byte (possibly slow at 2 GiB). --cycles 1..3.");
    puts("NOTE: successful memory copy does not imply NBD or swap reliability.");
}
static void dev_text(cl_device_id d,cl_device_info key,char *out,size_t max) {
    if(clGetDeviceInfo(d,key,max,out,NULL)!=CL_SUCCESS) snprintf(out,max,"(unknown)");
    out[max-1]='\0';
}
static void platform_text(cl_platform_id p,cl_platform_info key,char *out,size_t max) {
    if(clGetPlatformInfo(p,key,max,out,NULL)!=CL_SUCCESS) snprintf(out,max,"(unknown)");
    out[max-1]='\0';
}
static int discover(Device *list, int cap) {
    cl_uint pcount=0;
    cl_int e=clGetPlatformIDs(0,NULL,&pcount);
    if(e!=CL_SUCCESS || !pcount) {fprintf(stderr,"OpenCL platforms unavailable (%d). Check 'clinfo'.\n",(int)e);return -1;}
    cl_platform_id *platforms=calloc(pcount,sizeof(*platforms));
    if(!platforms)return -1;
    if(clGetPlatformIDs(pcount,platforms,NULL)!=CL_SUCCESS){free(platforms);return -1;}
    int n=0;
    for(cl_uint p=0;p<pcount;p++) {
        cl_uint count=0;
        e=clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_GPU,0,NULL,&count);
        if(e==CL_DEVICE_NOT_FOUND)continue;
        if(e!=CL_SUCCESS || !count)continue;
        cl_device_id *devices=calloc(count,sizeof(*devices));
        if(!devices)continue;
        if(clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_GPU,count,devices,NULL)!=CL_SUCCESS){free(devices);continue;}
        for(cl_uint i=0;i<count && n<cap;i++) {
            Device *d=&list[n++]; memset(d,0,sizeof(*d));
            d->platform=platforms[p];d->device=devices[i];
            dev_text(d->device,CL_DEVICE_NAME,d->name,sizeof(d->name));
            dev_text(d->device,CL_DEVICE_VENDOR,d->vendor,sizeof(d->vendor));
            platform_text(d->platform,CL_PLATFORM_NAME,d->platform_name,sizeof(d->platform_name));
            if(clGetDeviceInfo(d->device,CL_DEVICE_HOST_UNIFIED_MEMORY,sizeof(d->unified),&d->unified,NULL)!=CL_SUCCESS)
                d->unified=CL_TRUE; /* cannot prove discrete VRAM -> refuse */
            clGetDeviceInfo(d->device,CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(d->global),&d->global,NULL);
            clGetDeviceInfo(d->device,CL_DEVICE_MAX_MEM_ALLOC_SIZE,sizeof(d->max_alloc),&d->max_alloc,NULL);
        }
        free(devices);
    }
    free(platforms);
    return n;
}
static void list_devices(const Device *list,int n) {
    for(int i=0;i<n;i++){
        const Device *d=&list[i];
        printf("GPU[%d] name=%s vendor=%s platform=%s unified=%u global_mib=%"PRIu64" max_alloc_mib=%"PRIu64"\n",
               i,d->name,d->vendor,d->platform_name,(unsigned)d->unified,
               (uint64_t)(d->global/MiB),(uint64_t)(d->max_alloc/MiB));
    }
}
static void stage_free(Staging *s) {
    if(s->is_pinned) {
        if(s->src && s->src_mem)clEnqueueUnmapMemObject(s->q,s->src_mem,s->src,0,NULL,NULL);
        if(s->dst && s->dst_mem)clEnqueueUnmapMemObject(s->q,s->dst_mem,s->dst,0,NULL,NULL);
        if(s->q)clFinish(s->q);
        if(s->src_mem)clReleaseMemObject(s->src_mem);
        if(s->dst_mem)clReleaseMemObject(s->dst_mem);
    } else {free(s->src);free(s->dst);}
    memset(s,0,sizeof(*s));
}
static int stage_make(Staging *s,cl_context ctx,cl_command_queue q,size_t len,int pinned) {
    memset(s,0,sizeof(*s));s->ctx=ctx;s->q=q;s->len=len;s->is_pinned=pinned;
    cl_int err=CL_SUCCESS;
    if(pinned) {
        s->src_mem=clCreateBuffer(ctx,CL_MEM_READ_WRITE|CL_MEM_ALLOC_HOST_PTR,len,NULL,&err);
        if(err!=CL_SUCCESS || !s->src_mem)goto fail;
        s->dst_mem=clCreateBuffer(ctx,CL_MEM_READ_WRITE|CL_MEM_ALLOC_HOST_PTR,len,NULL,&err);
        if(err!=CL_SUCCESS || !s->dst_mem)goto fail;
        s->src=clEnqueueMapBuffer(q,s->src_mem,CL_TRUE,CL_MAP_READ|CL_MAP_WRITE,0,len,0,NULL,NULL,&err);
        if(err!=CL_SUCCESS || !s->src)goto fail;
        s->dst=clEnqueueMapBuffer(q,s->dst_mem,CL_TRUE,CL_MAP_READ|CL_MAP_WRITE,0,len,0,NULL,NULL,&err);
        if(err!=CL_SUCCESS || !s->dst)goto fail;
    } else {
        s->src=malloc(len);s->dst=malloc(len);
        if(!s->src||!s->dst)goto fail;
    }
    memset(s->dst,0xCD,len);
    return 0;
fail:
    fprintf(stderr,"WARN: host staging allocation failed: %s (%d)\n",pinned?"pinned":"pageable",(int)err);
    stage_free(s);return -1;
}
static void record_event(Meter *m,cl_event ev) {
    if(!ev)return;
    cl_ulong a=0,b=0;
    if(clGetEventProfilingInfo(ev,CL_PROFILING_COMMAND_START,sizeof(a),&a,NULL)==CL_SUCCESS &&
       clGetEventProfilingInfo(ev,CL_PROFILING_COMMAND_END,sizeof(b),&b,NULL)==CL_SUCCESS && b>=a)
        m->gpu_ns += (double)(b-a);
    clReleaseEvent(ev);
}
static int copy_timed(cl_command_queue q,cl_mem buf,int writing,size_t offset,size_t size,
                      unsigned char *host,Meter *m,double *duration_ns) {
    cl_event ev=NULL;uint64_t t0=now_ns();
    cl_int e=writing ? clEnqueueWriteBuffer(q,buf,CL_TRUE,offset,size,host,0,NULL,&ev)
                     : clEnqueueReadBuffer(q,buf,CL_TRUE,offset,size,host,0,NULL,&ev);
    uint64_t t1=now_ns();
    if(e!=CL_SUCCESS){fprintf(stderr,"ERROR %s offset=%zu size=%zu cl=%d\n",writing?"write":"read",offset,size,(int)e);
        if(ev)clReleaseEvent(ev);
        return -1;
    }
    const double dt=(double)(t1-t0);
    if(duration_ns)*duration_ns=dt;
    m->wall_ns+=dt;m->calls++;
    record_event(m,ev);
    return 0;
}
static void report_speed(const char *name,size_t bytes,const Meter *m) {
    double ms=m->wall_ns/1e6;
    double mibps=m->wall_ns>0 ? (double)bytes/MiB/(m->wall_ns/1e9):0;
    printf("%s calls=%"PRIu64" bytes=%zu wall_ms=%.3f MiB_s=%.2f",name,m->calls,bytes,ms,mibps);
    if(m->gpu_ns>0)printf(" gpu_event_ms=%.3f",m->gpu_ns/1e6);
    putchar('\n');
}
static int comparator(const void *a,const void *b) {
    double x=*(const double*)a,y=*(const double*)b;
    return (x>y)-(x<y);
}
static void report_latency(const char *name,double *lat,int n) {
    if(n<=0)return;
    qsort(lat,n,sizeof(*lat),comparator);
    printf("%s p50_us=%.2f p95_us=%.2f p99_us=%.2f worst_us=%.2f\n",name,
           lat[n/2]/1000.,lat[(int)((n-1)*0.95)]/1000.,lat[(int)((n-1)*0.99)]/1000.,lat[n-1]/1000.);
}
static uint64_t page_at(uint32_t i,uint32_t n,uint64_t total) {
    return n==1?0:((uint64_t)i*(total-1))/(n-1);
}
static void page_shuffle(uint64_t *pages,uint32_t n) {
    uint64_t state=UINT64_C(0xf12e3a7190d89abb);
    for(uint32_t k=n;k>1;k--) {
        state=mix64(state+UINT64_C(0x9e3779b97f4a7c15));
        uint32_t j=(uint32_t)(state%k);
        uint64_t t=pages[k-1]; pages[k-1]=pages[j];pages[j]=t;
    }
}
static int run_trial(cl_command_queue q,cl_mem *segments,size_t segbytes,size_t totalbytes,
                     size_t sequentialbytes,size_t chunkbytes,uint32_t pages,
                     Staging *s,const char *label) {
    Meter w={0},r={0},rw={0},rr={0};
    size_t chunks=(sequentialbytes+chunkbytes-1)/chunkbytes;
    if(chunks==0){fprintf(stderr,"ERROR: no sequential data\n");return -1;}
    /* warm-up transfer; not included in timed counters */
    fill(s->src,4096,UINT64_C(0x1234abcdef));
    if(clEnqueueWriteBuffer(q,segments[0],CL_TRUE,0,4096,s->src,0,NULL,NULL)!=CL_SUCCESS ||
       clEnqueueReadBuffer(q,segments[0],CL_TRUE,0,4096,s->dst,0,NULL,NULL)!=CL_SUCCESS ||
       memcmp(s->src,s->dst,4096)!=0){fprintf(stderr,"ERROR: OpenCL warmup copy failed\n");return -1;}
    for(size_t k=0;k<chunks;k++) {
        size_t pos=k*chunkbytes;
        size_t n=sequentialbytes-pos<chunkbytes ? sequentialbytes-pos:chunkbytes;
        uint64_t seed=UINT64_C(0xcafe1000beef0000)+k;
        fill(s->src,n,seed);
        if(copy_timed(q,segments[pos/segbytes],1,pos%segbytes,n,s->src,&w,NULL))return -1;
    }
    for(size_t k=0;k<chunks;k++) {
        size_t pos=k*chunkbytes;
        size_t n=sequentialbytes-pos<chunkbytes?sequentialbytes-pos:chunkbytes;
        if(copy_timed(q,segments[pos/segbytes],0,pos%segbytes,n,s->dst,&r,NULL))return -1;
        fill(s->src,n,UINT64_C(0xcafe1000beef0000)+k);
        if(memcmp(s->src,s->dst,n)!=0) {
            fprintf(stderr,"FAIL: sequential data corruption at offset=%zu length=%zu host=%s\n",pos,n,label);return -1;
        }
    }
    printf("BENCH mode=%s sequential verified=YES\n",label);
    report_speed("  H2D_write",sequentialbytes,&w);
    report_speed("  D2H_read ",sequentialbytes,&r);

    uint64_t *offsets=calloc(pages,sizeof(*offsets));
    double *wlat=calloc(pages,sizeof(*wlat)),*rlat=calloc(pages,sizeof(*rlat));
    if(!offsets||!wlat||!rlat){free(offsets);free(wlat);free(rlat);return -1;}
    uint64_t total_pages=totalbytes/PAGE;
    for(uint32_t i=0;i<pages;i++)offsets[i]=page_at(i,pages,total_pages);
    page_shuffle(offsets,pages);
    for(uint32_t i=0;i<pages;i++)fill(s->src+(size_t)i*PAGE,PAGE,UINT64_C(0xabcdeead12340000)^offsets[i]);
    for(uint32_t i=0;i<pages;i++) {
        size_t offset=(size_t)offsets[i]*PAGE;
        if(copy_timed(q,segments[offset/segbytes],1,offset%segbytes,(size_t)PAGE,
                      s->src+(size_t)i*PAGE,&rw,&wlat[i]))goto random_fail;
    }
    for(uint32_t i=0;i<pages;i++) {
        size_t offset=(size_t)offsets[i]*PAGE;
        if(copy_timed(q,segments[offset/segbytes],0,offset%segbytes,(size_t)PAGE,
                      s->dst+(size_t)i*PAGE,&rr,&rlat[i]))goto random_fail;
    }
    for(uint32_t i=0;i<pages;i++) {
        if(memcmp(s->src+(size_t)i*PAGE,s->dst+(size_t)i*PAGE,(size_t)PAGE)!=0){
            fprintf(stderr,"FAIL: 4KiB random mismatch logical_page=%"PRIu64" host=%s\n",offsets[i],label);goto random_fail;
        }
    }
    printf("BENCH mode=%s random_4k verified=YES pages=%u span_mib=%zu\n",label,pages,totalbytes/(size_t)MiB);
    report_speed("  H2D_4K_write",pages*PAGE,&rw);
    report_latency("  H2D_4K_latency",wlat,(int)pages);
    report_speed("  D2H_4K_read ",pages*PAGE,&rr);
    report_latency("  D2H_4K_latency",rlat,(int)pages);
    printf("PASS mode=%s no_swap=YES\n",label);
    free(offsets);free(wlat);free(rlat);return 0;
random_fail:
    free(offsets);free(wlat);free(rlat);return -1;
}
/* Full-coverage integrity test across every segment. Not a performance measurement. */
static int full_coverage(cl_command_queue q, cl_mem *segments,size_t segbytes,size_t totalbytes,
                         size_t chunkbytes,Staging *s,uint32_t cycles) {
    for(uint32_t cycle=0;cycle<cycles;cycle++) {
        uint64_t start=now_ns();
        for(size_t pos=0;pos<totalbytes;) {
            size_t remseg=segbytes-(pos%segbytes);
            size_t n=totalbytes-pos;
            if(n>chunkbytes)n=chunkbytes;
            if(n>remseg)n=remseg;
            fill(s->src,n,UINT64_C(0x991837541abb0000)^(pos/PAGE)^((uint64_t)cycle<<40));
            cl_int e=clEnqueueWriteBuffer(q,segments[pos/segbytes],CL_TRUE,pos%segbytes,n,s->src,0,NULL,NULL);
            if(e!=CL_SUCCESS){fprintf(stderr,"FULL VERIFY write failed position=%zu cl=%d\n",pos,(int)e);return -1;}
            pos+=n;
        }
        for(size_t pos=0;pos<totalbytes;) {
            size_t remseg=segbytes-(pos%segbytes);
            size_t n=totalbytes-pos;
            if(n>chunkbytes)n=chunkbytes;
            if(n>remseg)n=remseg;
            cl_int e=clEnqueueReadBuffer(q,segments[pos/segbytes],CL_TRUE,pos%segbytes,n,s->dst,0,NULL,NULL);
            if(e!=CL_SUCCESS){fprintf(stderr,"FULL VERIFY read failed position=%zu cl=%d\n",pos,(int)e);return -1;}
            fill(s->src,n,UINT64_C(0x991837541abb0000)^(pos/PAGE)^((uint64_t)cycle<<40));
            if(memcmp(s->src,s->dst,n)){
                size_t at=0;while(at<n && s->src[at]==s->dst[at])at++;
                fprintf(stderr,"FULL VERIFY MISMATCH at byte=%zu cycle=%u expected=%u got=%u\n",
                        pos+at,cycle+1,(unsigned)s->src[at],(unsigned)s->dst[at]);return -1;
            }
            pos+=n;
        }
        printf("FULL_VERIFY PASS cycle=%u verified_mib=%zu elapsed_s=%.3f\n",
               cycle+1,totalbytes/(size_t)MiB,(double)(now_ns()-start)/1e9);
        fflush(stdout);
    }
    return 0;
}

int main(int argc,char **argv) {
    Options o={.mib=256,.test_mib=64,.chunk_mib=8,.seg_mib=256,.pages=512,.device_index=-1,.host_mode=2,.full_verify=0,.cycles=1};
    int listing=0;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){usage(argv[0]);return 0;}
        if(!strcmp(argv[i],"--list")){listing=1;continue;}
        if(!strcmp(argv[i],"--full-verify")){o.full_verify=1;continue;}
        if(i+1>=argc){usage(argv[0]);return 2;}
        const char *flag=argv[i],*v=argv[++i];
        uint32_t tmp=0;
        if(!strcmp(flag,"--host")){
            if(!strcmp(v,"pageable"))o.host_mode=0;
            else if(!strcmp(v,"pinned"))o.host_mode=1;
            else if(!strcmp(v,"both"))o.host_mode=2;
            else {fprintf(stderr,"Invalid --host value\n");return 2;}
            continue;
        }
        if(parse_num(v,&tmp)){fprintf(stderr,"Invalid value for %s: %s\n",flag,v);return 2;}
        if(!strcmp(flag,"--mib"))o.mib=tmp;
        else if(!strcmp(flag,"--test-mib"))o.test_mib=tmp;
        else if(!strcmp(flag,"--chunk-mib"))o.chunk_mib=tmp;
        else if(!strcmp(flag,"--seg-mib"))o.seg_mib=tmp;
        else if(!strcmp(flag,"--pages"))o.pages=tmp;
        else if(!strcmp(flag,"--cycles"))o.cycles=tmp;
        else if(!strcmp(flag,"--device-index"))o.device_index=(int)tmp;
        else {fprintf(stderr,"Unknown option %s\n",flag);return 2;}
    }
    if(o.cycles<1||o.cycles>3||!o.mib||o.mib>8192||!o.test_mib||o.test_mib>o.mib||
       !o.chunk_mib||o.chunk_mib>32||o.chunk_mib>o.test_mib||
       !o.seg_mib||o.seg_mib>o.mib||o.seg_mib<o.chunk_mib||
       o.test_mib>o.seg_mib||!o.pages||o.pages>MAX_PAGES||
       (uint64_t)o.pages*PAGE>(uint64_t)o.mib*MiB||
       (o.mib+o.seg_mib-1)/o.seg_mib > MAX_SEGS) {
        fprintf(stderr,"Invalid limits: --mib 1..2048, test 1..min(mib,seg), chunk 1..min(test,32), segments <=64; pages 1..2048.\n");return 2;
    }
    Device devices[MAX_DEVICES]={0};
    int count=discover(devices,MAX_DEVICES);
    if(count<=0){fprintf(stderr,"No OpenCL GPU device found; run 'clinfo'.\n");return 1;}
    list_devices(devices,count);
    if(listing)return 0;
    int selection=o.device_index;
    if(selection==-1){
        for(int i=0;i<count;i++){
            if(devices[i].unified==CL_FALSE &&
                (strstr(devices[i].vendor,"AMD")||strstr(devices[i].vendor,"Advanced Micro Devices")||
                 strstr(devices[i].vendor,"ATI"))){selection=i;break;}
        }
    }
    if(selection<0||selection>=count||devices[selection].unified!=CL_FALSE) {
        fprintf(stderr,"REFUSED: no confirmed discrete AMD OpenCL GPU selected. No fallback to system RAM.\n");return 1;
    }
    Device *d=&devices[selection];
    printf("SELECTED gpu=%d name=%s vendor=%s unified=%u total_mib=%u test_mib=%u segment_mib=%u pages=%u\n",
           selection,d->name,d->vendor,(unsigned)d->unified,o.mib,o.test_mib,o.seg_mib,o.pages);
    if((uint64_t)o.seg_mib*MiB > d->max_alloc) {
        fprintf(stderr,"REFUSED: OpenCL max allocation is %"PRIu64" MiB; use lower --seg-mib.\n",
                (uint64_t)(d->max_alloc/MiB));return 1;
    }
    if((uint64_t)o.mib*MiB > d->global*9/10) {
        fprintf(stderr,"REFUSED: requested allocation exceeds 90%% GPU global memory.\n");return 1;
    }
    int ret=1;cl_int err=CL_SUCCESS;
    cl_context ctx=NULL;cl_command_queue q=NULL;
    cl_mem segments[MAX_SEGS]={0};
    const size_t segbytes=(size_t)o.seg_mib*(size_t)MiB;
    const size_t totalbytes=(size_t)o.mib*(size_t)MiB;
    const uint32_t nsegs=(uint32_t)((totalbytes+segbytes-1)/segbytes);
    ctx=clCreateContext(NULL,1,&d->device,NULL,NULL,&err);
    if(err!=CL_SUCCESS||!ctx) {fprintf(stderr,"clCreateContext failed %d\n",err);goto cleanup;}
    q=clCreateCommandQueue(ctx,d->device,CL_QUEUE_PROFILING_ENABLE,&err);
    if(err!=CL_SUCCESS||!q) {fprintf(stderr,"clCreateCommandQueue failed %d\n",err);goto cleanup;}
    for(uint32_t i=0;i<nsegs;i++){
        size_t len=(i==nsegs-1)?totalbytes-(size_t)i*segbytes:segbytes;
        segments[i]=clCreateBuffer(ctx,CL_MEM_READ_WRITE,len,NULL,&err);
        if(err!=CL_SUCCESS||!segments[i]){
            fprintf(stderr,"ERROR: VRAM segment %u/%u allocation %zu MiB failed: CL %d\n",
                    i+1,nsegs,len/(size_t)MiB,(int)err);goto cleanup;
        }
    }
    printf("ALLOCATED vram_mib=%u segments=%u (GPU OpenCL buffers, not a block device)\n",o.mib,nsegs);
    const size_t hostlen=(size_t)o.chunk_mib*(size_t)MiB > (size_t)o.pages*PAGE ?
            (size_t)o.chunk_mib*(size_t)MiB : (size_t)o.pages*PAGE;
    int successes=0;
    for(int mode=0;mode<2;mode++){
        if(o.host_mode!=2&&o.host_mode!=mode)continue;
        Staging stage;
        if(stage_make(&stage,ctx,q,hostlen,mode)){
            if(o.host_mode==2){fprintf(stderr,"WARN: skip unavailable %s staging\n",mode?"pinned":"pageable");continue;}
            goto cleanup;
        }
        int result=run_trial(q,segments,segbytes,totalbytes,(size_t)o.test_mib*(size_t)MiB,
                             (size_t)o.chunk_mib*(size_t)MiB,o.pages,&stage,mode?"pinned":"pageable");
        if(result){stage_free(&stage);goto cleanup;}
        if(o.full_verify && (o.host_mode!=2||mode==0)) {
            puts("FULL_VERIFY starting; this may be slow. GPU and system swap remain untouched.");
            fflush(stdout);
            if(full_coverage(q,segments,segbytes,totalbytes,(size_t)o.chunk_mib*(size_t)MiB,&stage,o.cycles)) {
                stage_free(&stage);goto cleanup;
            }
        }
        stage_free(&stage);
        successes++;
    }
    if(!successes){fprintf(stderr,"FAIL: no host staging benchmark succeeded\n");goto cleanup;}
    puts("BENCHMARK COMPLETE: data verified; NO SWAP, NO NBD, NO mlockall");
    ret=0;
cleanup:
    if(q)clFinish(q);
    for(int i=0;i<MAX_SEGS;i++)if(segments[i])clReleaseMemObject(segments[i]);
    if(q)clReleaseCommandQueue(q);
    if(ctx)clReleaseContext(ctx);
    return ret;
}