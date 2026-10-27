#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#ifdef NOPEN_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#endif
/* 0.2 experimental: isolated PCIe transfer ceiling, NOT system SWAP. */
#define MIB (1024u*1024u)
typedef struct {
    unsigned char *mock;
#ifdef NOPEN_OPENCL
    cl_context context;
    cl_command_queue queue;
    cl_mem mem;
#endif
} gpu;
static int number(const char *s, size_t *v) {
    if(!s || !*s || *s=='-') return -1;
    char *end=NULL; errno=0;
    unsigned long long n=strtoull(s,&end,10);
    if(errno || !end || *end || n==0 || n>SIZE_MAX) return -1;
    *v=(size_t)n; return 0;
}
static double clock_sec(void) {
    struct timespec t;
    return clock_gettime(CLOCK_MONOTONIC,&t)==0 ?
        (double)t.tv_sec+(double)t.tv_nsec/1e9 : -1.0;
}
static int unsafe_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f) return 1;
    char line[512]; int bad=0;
    if (!fgets(line,sizeof line,f)) { fclose(f); return 1; }
    while(fgets(line,sizeof line,f)) {
        char name[256]={0};
        if(sscanf(line,"%255s",name)==1 && strncmp(name,"/dev/nbd",8)==0) bad=1;
    }
    fclose(f); return bad;
}
static void cleanup(gpu *g) {
#ifdef NOPEN_OPENCL
    if(g->mem) clReleaseMemObject(g->mem);
    if(g->queue) clReleaseCommandQueue(g->queue);
    if(g->context) clReleaseContext(g->context);
#endif
    free(g->mock);
}
#ifdef NOPEN_OPENCL
static int open_amd(gpu *g,size_t bytes) {
    cl_uint np=0;
    if(clGetPlatformIDs(0,NULL,&np)!=CL_SUCCESS || !np) return -1;
    cl_platform_id *platforms=calloc(np,sizeof *platforms);
    if(!platforms) return -1;
    if(clGetPlatformIDs(np,platforms,NULL)!=CL_SUCCESS) {free(platforms);return -1;}
    cl_device_id selected=NULL;
    cl_ulong memory=0,maxalloc=0;
    char name[128]={0};
    for(cl_uint p=0;p<np && !selected;p++) {
        cl_uint nd=0;
        if(clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_GPU,0,NULL,&nd)!=CL_SUCCESS || !nd) continue;
        cl_device_id *devs=calloc(nd,sizeof *devs);
        if(!devs) continue;
        if(clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_GPU,nd,devs,NULL)==CL_SUCCESS)
        for(cl_uint i=0;i<nd;i++) {
            char vendor[128]={0};
            cl_bool unified=CL_TRUE;
            if(clGetDeviceInfo(devs[i],CL_DEVICE_VENDOR,sizeof vendor,vendor,NULL)!=CL_SUCCESS ||
               clGetDeviceInfo(devs[i],CL_DEVICE_HOST_UNIFIED_MEMORY,sizeof unified,&unified,NULL)!=CL_SUCCESS) continue;
            if(!unified && (strstr(vendor,"AMD")||strstr(vendor,"Advanced Micro Devices"))) {
                if(clGetDeviceInfo(devs[i],CL_DEVICE_GLOBAL_MEM_SIZE,sizeof memory,&memory,NULL)!=CL_SUCCESS ||
                   clGetDeviceInfo(devs[i],CL_DEVICE_MAX_MEM_ALLOC_SIZE,sizeof maxalloc,&maxalloc,NULL)!=CL_SUCCESS) continue;
                selected=devs[i];
                (void)clGetDeviceInfo(selected,CL_DEVICE_NAME,sizeof name,name,NULL);
                break;
            }
        }
        free(devs);
    }
    free(platforms);
    if(!selected) {fputs("OPENCL_REFUSED no discrete AMD GPU\n",stderr);return -1;}
    if(bytes>maxalloc || bytes>memory/4*3 ||
       (memory>=8ull*1024*MIB && memory-(cl_ulong)bytes<4ull*1024*MIB)) {
        fputs("OPENCL_REFUSED VRAM budget limit\n",stderr);return -1;
    }
    cl_int err=CL_SUCCESS;
    g->context=clCreateContext(NULL,1,&selected,NULL,NULL,&err);
    if(err!=CL_SUCCESS || !g->context) return -1;
    g->queue=clCreateCommandQueue(g->context,selected,0,&err);
    if(err!=CL_SUCCESS || !g->queue) return -1;
    g->mem=clCreateBuffer(g->context,CL_MEM_READ_WRITE,bytes,NULL,&err);
    if(err!=CL_SUCCESS || !g->mem) return -1;
    printf("GPU_SELECTED name=%s reserved_mib=%zu discrete_only=YES\n",name,bytes/MIB);
    return 0;
}
#endif
static int transfer(gpu *g,int mock,int to_gpu,unsigned char *host,size_t bytes,size_t chunk) {
    for(size_t offset=0;offset<bytes;offset+=chunk) {
        if(mock) {
            if(to_gpu) memcpy(g->mock+offset,host+offset,chunk);
            else memcpy(host+offset,g->mock+offset,chunk);
        } else {
#ifdef NOPEN_OPENCL
            cl_int rc=to_gpu ?
                clEnqueueWriteBuffer(g->queue,g->mem,CL_FALSE,offset,chunk,host+offset,0,NULL,NULL) :
                clEnqueueReadBuffer(g->queue,g->mem,CL_FALSE,offset,chunk,host+offset,0,NULL,NULL);
            if(rc!=CL_SUCCESS) {fprintf(stderr,"TRANSFER_FAILED rc=%d offset=%zu\n",rc,offset);return -1;}
#else
            (void)g;(void)to_gpu;(void)host;(void)bytes;(void)chunk;
            return -1;
#endif
        }
    }
#ifdef NOPEN_OPENCL
    if(!mock && clFinish(g->queue)!=CL_SUCCESS) return -1;
#endif
    return 0;
}
static void pattern(unsigned char *p,size_t n) {
    for(size_t b=0;b<n;b+=4096) {
        uint64_t x=UINT64_C(0x6a09e667f3bcc909)^(uint64_t)(b/4096);
        for(size_t j=0;j<4096;j++) {
            x^=x<<13;x^=x>>7;x^=x<<17;
            p[b+j]=(unsigned char)(x>>37);
        }
    }
}
int main(int argc,char **argv) {
    int mock=-1; size_t mib=64,kib=256,passes=3;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help")) {
            puts("Nopen v0.2.0-dev: --backend mock|opencl --pool-mib 4..256 --chunk-kib 4|16|64|256|1024|4096 --passes 1..10");
            return 0;
        }
        if(i+1>=argc) return 2;
        const char *key=argv[i++],*val=argv[i];
        if(!strcmp(key,"--backend")) {
            if(!strcmp(val,"mock")) mock=1;
            else if(!strcmp(val,"opencl")) mock=0;
            else return 2;
        } else {
            size_t *dest=NULL;
            if(!strcmp(key,"--pool-mib")) dest=&mib;
            if(!strcmp(key,"--chunk-kib")) dest=&kib;
            if(!strcmp(key,"--passes")) dest=&passes;
            if(!dest || number(val,dest)) return 2;
        }
    }
    if(mock<0 || mib<4 || mib>256 || passes>10 ||
       !(kib==4 || kib==16 || kib==64 || kib==256 || kib==1024 || kib==4096) ||
       kib*1024u>mib*MIB) return 2;
    if(unsafe_swap()) {fputs("REFUSED active NBD or unreadable /proc/swaps\n",stderr);return 3;}
    size_t bytes=mib*MIB,chunk=kib*1024u;
    unsigned char *tx=malloc(bytes),*rx=malloc(bytes);
    if(!tx || !rx) {free(tx);free(rx);return 4;}
    pattern(tx,bytes); memset(rx,0,bytes);
    gpu g={0};int rc=0;
    if(mock) g.mock=malloc(bytes);
    if(mock ? !g.mock : 0) rc=5;
    if(!mock) {
#ifdef NOPEN_OPENCL
        if(open_amd(&g,bytes)) rc=5;
#else
        fputs("OPENCL_REFUSED not compiled; no fallback\n",stderr);rc=5;
#endif
    }
    if(rc) goto done;
    if(transfer(&g,mock,1,tx,bytes,chunk) ||
       transfer(&g,mock,0,rx,bytes,chunk) || memcmp(tx,rx,bytes)) {rc=6;goto done;}
    double write_s=0,read_s=0;
    for(size_t pass=0;pass<passes;pass++) {
        double a=clock_sec();
        if(a<0 || transfer(&g,mock,1,tx,bytes,chunk)) {rc=6;goto done;}
        double b=clock_sec();
        if(b<=a || transfer(&g,mock,0,rx,bytes,chunk)) {rc=6;goto done;}
        double c=clock_sec();
        if(c<=b || memcmp(tx,rx,bytes)) {rc=6;goto done;}
        write_s+=b-a;read_s+=c-b;
    }
    struct rusage usage;
    if(getrusage(RUSAGE_SELF,&usage)) {rc=6;goto done;}
    printf("LINK_RESULT version=0.2.0-dev backend=%s pool_mib=%zu chunk_kib=%zu passes=%zu "
           "h2d_mib_s=%.2f d2h_mib_s=%.2f process_maxrss_kib=%ld "
           "bulk_only=YES no_global_swap=YES\n",mock?"mock":"opencl",mib,kib,passes,
           (double)(mib*passes)/write_s,(double)(mib*passes)/read_s,usage.ru_maxrss);
    printf("BENCH_PASS backend=%s verified_bytes=%zu passes=%zu no_global_swap=YES\n",
           mock?"mock":"opencl",bytes,passes);
done:
    if(rc) fprintf(stderr,"BENCH_FAILED code=%d no_result_claim=YES\n",rc);
    cleanup(&g);free(tx);free(rx);return rc;
}
