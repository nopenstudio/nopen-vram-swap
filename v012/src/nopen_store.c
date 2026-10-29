#define _GNU_SOURCE
#include "nopen_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef NOPEN_HAS_OPENCL
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#endif

#define MIB (1024u * 1024u)
#define SEG_BYTES ((size_t)NOPEN_SEGMENT_MIB * MIB)
#define MAX_SEGS (NOPEN_MAX_MIB/NOPEN_SEGMENT_MIB)
struct nopen_store {
    nopen_backend backend;
    size_t count;
    size_t operations, fail_at;
    char error[256];
    unsigned char *mock;
#ifdef NOPEN_HAS_OPENCL
    cl_context ctx;
    cl_command_queue queue;
    cl_mem pinned_host; /* Experimental opt-in staging; never kernel swap by default. */
    size_t pinned_bytes;
    cl_mem segment[MAX_SEGS];
    size_t seg_count;
#endif
};
static int fail(nopen_store *s, const char *msg) {
    snprintf(s->error,sizeof(s->error),"%s",msg);
    return -1;
}
const char *nopen_store_last_error(const nopen_store *s) {return s ? s->error : "null store";}
size_t nopen_store_page_count(const nopen_store *s) {return s?s->count:0;}
void nopen_store_inject_failure(nopen_store *s,size_t n) {if(s) {s->operations=0;s->fail_at=n;}}
void nopen_store_close(nopen_store *s) {
    if(!s)return;
#ifdef NOPEN_HAS_OPENCL
    for(size_t i=0;i<s->seg_count;i++)if(s->segment[i])clReleaseMemObject(s->segment[i]);
    if(s->pinned_host)clReleaseMemObject(s->pinned_host);
    if(s->queue)clReleaseCommandQueue(s->queue);
    if(s->ctx)clReleaseContext(s->ctx);
#endif
    free(s->mock);free(s);
}
#ifdef NOPEN_HAS_OPENCL
static int open_opencl(nopen_store *s,size_t bytes) {
    cl_uint platform_count=0;
    cl_int rc=clGetPlatformIDs(0,NULL,&platform_count);
    if(rc!=CL_SUCCESS || platform_count==0)return fail(s,"no OpenCL platforms");
    cl_platform_id *platforms=calloc(platform_count,sizeof(*platforms));
    if(!platforms)return fail(s,"platform allocation failed");
    rc=clGetPlatformIDs(platform_count,platforms,NULL);
    if(rc!=CL_SUCCESS){free(platforms);return fail(s,"cannot enumerate OpenCL platforms");}
    cl_device_id gpu=NULL;
    cl_ulong global=0,max_alloc=0;
    char gpu_name[128]={0},vendor[128]={0};
    for(cl_uint p=0;p<platform_count&&!gpu;p++) {
        cl_uint count=0;
        if(clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_GPU,0,NULL,&count)!=CL_SUCCESS||!count)continue;
        cl_device_id *ids=calloc(count,sizeof(*ids));
        if(!ids){free(platforms);return fail(s,"device allocation failed");}
        rc=clGetDeviceIDs(platforms[p],CL_DEVICE_TYPE_GPU,count,ids,NULL);
        if(rc==CL_SUCCESS)for(cl_uint i=0;i<count;i++) {
            cl_bool unified=CL_TRUE;
            char v[128]={0};
            clGetDeviceInfo(ids[i],CL_DEVICE_VENDOR,sizeof(v),v,NULL);
            clGetDeviceInfo(ids[i],CL_DEVICE_HOST_UNIFIED_MEMORY,sizeof(unified),&unified,NULL);
            if(!unified && (strstr(v,"Advanced Micro Devices")||strstr(v,"AMD"))) {
                gpu=ids[i];snprintf(vendor,sizeof(vendor),"%s",v);
                clGetDeviceInfo(gpu,CL_DEVICE_NAME,sizeof(gpu_name),gpu_name,NULL);
                clGetDeviceInfo(gpu,CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(global),&global,NULL);
                clGetDeviceInfo(gpu,CL_DEVICE_MAX_MEM_ALLOC_SIZE,sizeof(max_alloc),&max_alloc,NULL);
                break;
            }
        }
        free(ids);
    }
    free(platforms);
    if(!gpu)return fail(s,"no discrete AMD OpenCL GPU; refuses host RAM fallback");
    /* Do not reserve over 75% of total VRAM. Leave at least 4 GiB on larger GPUs. */
    if(bytes>(size_t)(global*3/4) || (global >= 8ull*1024*MIB && global-bytes<4ull*1024*MIB))
        return fail(s,"VRAM reservation would leave insufficient GPU budget");
    s->ctx=clCreateContext(NULL,1,&gpu,NULL,NULL,&rc);
    if(rc!=CL_SUCCESS||!s->ctx)return fail(s,"clCreateContext failed");
    /* Alpha.1 throughput hygiene: command timing is disabled unless
     * explicitly requested for a profiling experiment. Profiling may
     * incur additional driver/event bookkeeping even with blocking copies.
     * Keep the queue IN ORDER to preserve page write/read sequencing.
     * This is a candidate optimization, not a proven speedup yet. */
    const char *profile=getenv("NOPEN_OPENCL_PROFILE");
    const cl_command_queue_properties props=
        (profile && strcmp(profile,"1")==0)?CL_QUEUE_PROFILING_ENABLE:0;
    s->queue=clCreateCommandQueue(s->ctx,gpu,props,&rc);
    if(rc!=CL_SUCCESS||!s->queue)return fail(s,"clCreateCommandQueue failed");
    fprintf(stderr,"NOPEN_V080_OPENCL_QUEUE profiling=%s in_order=YES\n",
            props?"ENABLED":"DISABLED");
    for(size_t off=0;off<bytes;off+=SEG_BYTES) {
        size_t len=bytes-off<SEG_BYTES?bytes-off:SEG_BYTES;
        if((cl_ulong)len>max_alloc)return fail(s,"segment exceeds max GPU allocation");
        cl_mem m=clCreateBuffer(s->ctx,CL_MEM_READ_WRITE,len,NULL,&rc);
        if(rc!=CL_SUCCESS||!m)return fail(s,"clCreateBuffer failed (insufficient GPU VRAM?)");
        s->segment[s->seg_count++]=m;
    }
    /* Alpha.2 opt-in pinned-host experiment. NOT enabled for normal swap
     * traffic, and never silently falls back to RAM-only OpenCL devices.
     * The 4MiB host buffer bounds locked/pinned host memory use. */
    const char *mode=getenv("NOPEN_V080_IO_MODE");
    if(mode && strcmp(mode,"direct")!=0 && strcmp(mode,"pinned")!=0)
        return fail(s,"unsupported NOPEN_V080_IO_MODE");
    if(mode && strcmp(mode,"pinned")==0) {
        s->pinned_bytes=4u*MIB;
        s->pinned_host=clCreateBuffer(s->ctx,
            CL_MEM_READ_WRITE|CL_MEM_ALLOC_HOST_PTR,s->pinned_bytes,NULL,&rc);
        if(rc!=CL_SUCCESS || !s->pinned_host)
            return fail(s,"CL_MEM_ALLOC_HOST_PTR staging creation failed");
        fprintf(stderr,"NOPEN_V080_PINNED_ENABLED bytes=%zu"
                       " experimental=YES kernel_swap=NO\n",s->pinned_bytes);
    }
    fprintf(stderr,"Nopen v0.12 selected discrete GPU: %s (%s); segments=%zu, GPU reservation=%zu MiB\n",gpu_name,vendor,s->seg_count,bytes/MIB);
    return 0;
}
#endif
int nopen_store_open(nopen_store **out,nopen_backend backend,size_t mib) {
    if(!out)return -1;
    *out=NULL;
    if(mib<1||mib>NOPEN_MAX_MIB|| (backend!=NOPEN_STORE_MOCK&&backend!=NOPEN_STORE_OPENCL))return -1;
    size_t bytes=mib*MIB;
    nopen_store *s=calloc(1,sizeof(*s));
    if(!s)return -1;
    s->backend=backend;s->count=bytes/NOPEN_PAGE_SIZE;
    if(backend==NOPEN_STORE_MOCK) {
        /* Fake storage is strictly opt-in for offline unit tests. */
        s->mock=calloc(1,bytes);
        if(!s->mock){nopen_store_close(s);return -1;}
    } else {
#ifdef NOPEN_HAS_OPENCL
        if(open_opencl(s,bytes)!=0){fprintf(stderr,"OPENCL REFUSED: %s\n",s->error);nopen_store_close(s);return -1;}
#else
        fputs("OPENCL REFUSED: binary built without OpenCL; no RAM fallback\n",stderr);
        nopen_store_close(s);return -1;
#endif
    }
    *out=s;return 0;
}
/* Contiguous batch API: one command per GPU segment, not per 4KiB page.
 * A backend error may leave a partially modified range. Callers must not
 * commit page-state metadata before success + integrity validation. */
static int io_pages(nopen_store *s,size_t page,void *buf,size_t pages,int write_operation) {
    if(!s||!buf||!pages||page>=s->count||pages>s->count-page) return -1;
    s->operations++;
    if(s->fail_at && s->operations>=s->fail_at)return fail(s,"injected GPU I/O fault");
    const size_t bytes=pages*NOPEN_PAGE_SIZE;
    if(s->backend==NOPEN_STORE_MOCK) {
        if(write_operation)memcpy(s->mock+page*NOPEN_PAGE_SIZE,buf,bytes);
        else memcpy(buf,s->mock+page*NOPEN_PAGE_SIZE,bytes);
        return 0;
    }
#ifdef NOPEN_HAS_OPENCL
    size_t pos=page*NOPEN_PAGE_SIZE;
    size_t remaining=bytes;
    unsigned char *host=buf;
    while(remaining) {
        const size_t seg_index=pos/SEG_BYTES;
        const size_t seg_off=pos%SEG_BYTES;
        size_t n=SEG_BYTES-seg_off;
        if(n>remaining)n=remaining;
        cl_mem seg=s->segment[seg_index];
        cl_int rc=CL_SUCCESS;
        if(!s->pinned_host) {
            rc=write_operation
                ?clEnqueueWriteBuffer(s->queue,seg,CL_TRUE,seg_off,n,host,0,NULL,NULL)
                :clEnqueueReadBuffer(s->queue,seg,CL_TRUE,seg_off,n,host,0,NULL,NULL);
        } else {
            /* A mapped host allocation MUST be unmapped before any
             * device copy can use it. Finish each bounded operation:
             * never expose in-flight DMA to a reused staging buffer. */
            if(n>s->pinned_bytes)return fail(s,"pinned staging bounds");
            if(write_operation) {
                void *ptr=clEnqueueMapBuffer(s->queue,s->pinned_host,CL_TRUE,
                    CL_MAP_WRITE,0,n,0,NULL,NULL,&rc);
                if(rc==CL_SUCCESS && ptr) {
                    memcpy(ptr,host,n);
                    rc=clEnqueueUnmapMemObject(s->queue,s->pinned_host,
                                               ptr,0,NULL,NULL);
                    if(rc==CL_SUCCESS)rc=clEnqueueCopyBuffer(s->queue,
                        s->pinned_host,seg,0,seg_off,n,0,NULL,NULL);
                    if(rc==CL_SUCCESS)rc=clFinish(s->queue);
                } else if(rc==CL_SUCCESS)rc=CL_MAP_FAILURE;
            } else {
                rc=clEnqueueCopyBuffer(s->queue,seg,s->pinned_host,
                                       seg_off,0,n,0,NULL,NULL);
                if(rc==CL_SUCCESS) {
                    void *ptr=clEnqueueMapBuffer(s->queue,s->pinned_host,
                        CL_TRUE,CL_MAP_READ,0,n,0,NULL,NULL,&rc);
                    if(rc==CL_SUCCESS && ptr) {
                        memcpy(host,ptr,n);
                        rc=clEnqueueUnmapMemObject(s->queue,s->pinned_host,
                                                   ptr,0,NULL,NULL);
                        if(rc==CL_SUCCESS)rc=clFinish(s->queue);
                    } else if(rc==CL_SUCCESS)rc=CL_MAP_FAILURE;
                }
            }
        }
        if(rc!=CL_SUCCESS) {
            snprintf(s->error,sizeof(s->error),"OpenCL batch %s failed rc=%d",
                     write_operation?"write":"read",rc);
            return -1;
        }
        pos+=n;remaining-=n;host+=n;
    }
    return 0;
#else
    return fail(s,"OpenCL support missing");
#endif
}
int nopen_store_write_pages(nopen_store*s,size_t page,const void*data,size_t pages) {
    return io_pages(s,page,(void*)data,pages,1);
}
int nopen_store_read_pages(nopen_store*s,size_t page,void*data,size_t pages) {
    return io_pages(s,page,data,pages,0);
}
int nopen_store_write(nopen_store*s,size_t p,const void*data) {
    return nopen_store_write_pages(s,p,data,1);
}
int nopen_store_read(nopen_store*s,size_t p,void*data) {
    return nopen_store_read_pages(s,p,data,1);
}
