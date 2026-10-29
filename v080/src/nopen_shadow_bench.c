#define _POSIX_C_SOURCE 200809L
/* Non-swap shadow path latency: O_DSYNC SSD + 4KiB/64KiB GPU worker IPC.
 * Do not mistake raw GPU bandwidth for secure Linux swap throughput. */
#include "nopen_shadow.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PAGE NOPEN_PAGE_SIZE
#define MIB (1024u*1024u)
#define MAXOPS 1024u
static double clock_ns(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return -1;
    return (double)t.tv_sec*1e9+(double)t.tv_nsec;
}
static int cmp_double(const void *a,const void *b) {
    const double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
static double p95(const double *a,size_t n) {
    double sorted[MAXOPS];
    memcpy(sorted,a,n*sizeof(double));
    qsort(sorted,n,sizeof(double),cmp_double);
    return sorted[((95u*n+99u)/100u)-1]/1000.0;
}
static void pattern(unsigned char *p,size_t n,size_t offset,size_t iter) {
    for(size_t i=0;i<n;i++)
        p[i]=(unsigned char)((offset/PAGE*17u)+(i*31u)+(i>>8)+(iter*13u));
}
static int no_gpu_swap(void) {
    FILE *f=fopen("/proc/swaps","r");
    if(!f)return 0;
    char line[512];
    if(!fgets(line,sizeof line,f)){fclose(f);return 0;}
    while(fgets(line,sizeof line,f)) {
        char device[256]={0};
        if(sscanf(line,"%255s",device)==1 &&
           (!strncmp(device,"/dev/ublkb",10) ||
            !strncmp(device,"/dev/nbd",8))) {
            fclose(f);return 0;
        }
    }
    int good=!ferror(f);
    fclose(f);
    return good;
}
int main(int argc,char **argv) {
    const char *worker=NULL,*file=NULL,*mode=NULL,*ack=NULL;
    size_t count=128;
    for(int i=1;i<argc;i++) {
        if(i+1>=argc)return 2;
        const char *arg=argv[i++],*val=argv[i];
        if(!strcmp(arg,"--worker"))worker=val;
        else if(!strcmp(arg,"--shadow-file"))file=val;
        else if(!strcmp(arg,"--mode"))mode=val;
        else if(!strcmp(arg,"--ack"))ack=val;
        else if(!strcmp(arg,"--operations")) {
            if(val[0]=='+'||val[0]=='-')return 2;
            char *end=NULL;errno=0;
            unsigned long n=strtoul(val,&end,10);
            if(errno||end==val||*end)return 2;
            count=(size_t)n;
        } else return 2;
    }
    if(geteuid()==0 || !no_gpu_swap() || !worker || worker[0]!='/' ||
       !file || file[0]!='/' ||
       !mode || (strcmp(mode,"direct")&&strcmp(mode,"pinned")) ||
       !ack || strcmp(ack,"I_ACCEPT_REAL_GPU_SHADOW_NO_SWAP") ||
       count<16 || count>MAXOPS) {
        fputs("V080_SHADOW_BENCH_REFUSED requires normal user,"
              " no kernel GPU swap and explicit GPU-only shadow ack\n",stderr);
        return 2;
    }
    if(setenv("NOPEN_V080_IO_MODE",mode,1)!=0)return 2;
    nopen_shadow *shadow=NULL;
    printf("V080_SHADOW_BENCH_START mode=%s capacity_mib=64"
           " disk_write_through=YES kernel_swap=NO\n",mode);
    fflush(stdout);
    int rc=nopen_shadow_open(&shadow,NOPEN_STORE_OPENCL,64,2000,worker,file);
    if(rc!=0) {
        fprintf(stderr,"V080_SHADOW_BENCH_OPEN_FAILED rc=%d\n",rc);
        return 3;
    }
    unsigned char src[16u*PAGE],got[16u*PAGE];
    int result=5;
    const size_t sizes[]={PAGE,16u*PAGE};
    for(size_t c=0;c<2;c++) {
        const size_t len=sizes[c];
        const size_t slots=64u*MIB/len;
        double w[MAXOPS],rd[MAXOPS];
        double total_w=0,total_r=0;
        for(size_t i=0;i<count;i++) {
            const size_t offset=((i*137u+13u)%slots)*len;
            pattern(src,len,offset,i);
            double begin=clock_ns();
            if(begin<0 || nopen_shadow_write(shadow,offset,src,len)!=0)
                goto finish;
            double middle=clock_ns();
            if(middle<0 || nopen_shadow_read(shadow,offset,got,len)!=0)
                goto finish;
            double end=clock_ns();
            if(end<0 || memcmp(src,got,len)!=0) {
                fprintf(stderr,"V080_SHADOW_BENCH_DATA_MISMATCH"
                        " size=%zu offset=%zu\n",len,offset);
                goto finish;
            }
            w[i]=middle-begin;rd[i]=end-middle;
            total_w+=w[i];total_r+=rd[i];
        }
        if(total_w<=0 || total_r<=0)goto finish;
        double moved_mib=(double)(len*count)/MIB;
        printf("V080_SHADOW_BENCH_RESULT backend=opencl mode=%s"
               " chunk_kib=%zu operations=%zu"
               " write_mib_s=%.2f read_mib_s=%.2f"
               " write_p95_us=%.2f read_p95_us=%.2f"
               " gpu_worker_ipc=YES disk_write_through=YES"
               " all_bytes_verified=YES kernel_swap=NO\n",
               mode,len/1024u,count,
               moved_mib/(total_w/1e9),moved_mib/(total_r/1e9),
               p95(w,count),p95(rd,count));
        fflush(stdout);
    }
    /* Validate previously acknowledged data and NEW writes after
     * explicit GPU worker termination; never physically reset the GPU. */
    pattern(src,PAGE,0,123);
    if(nopen_shadow_write(shadow,0,src,PAGE)!=0 ||
       nopen_shadow_simulate_gpu_loss(shadow)!=0 ||
       nopen_shadow_read(shadow,0,got,PAGE)!=0 ||
       memcmp(src,got,PAGE)!=0)goto finish;
    pattern(src,PAGE,PAGE,124);
    if(nopen_shadow_write(shadow,PAGE,src,PAGE)!=0 ||
       nopen_shadow_read(shadow,PAGE,got,PAGE)!=0 ||
       memcmp(src,got,PAGE)!=0)goto finish;
    nopen_shadow_stats st={0};
    if(nopen_shadow_get_stats(shadow,&st)!=0 ||
       st.poisoned || st.disk_failures || st.gpu_failures!=1 ||
       st.gpu_active || st.shadow_reads<2 ||
       st.gpu_reads<2*count || st.gpu_writes<2*count)
        goto finish;
    printf("V080_SHADOW_BENCH_PASS mode=%s real_gpu=YES"
           " disk_backup_after_worker_loss=YES"
           " data_integrity=YES kernel_swap=NO\n",mode);
    result=0;
finish:
    nopen_shadow_close(shadow);
    if(result==0) {
        if(unlink(file)!=0)
            fprintf(stderr,"V080_SHADOW_BENCH_WARNING preserved_file=%s\n",file);
    } else
        fprintf(stderr,"V080_SHADOW_BENCH_FAILED preserved_file=%s\n",file);
    return result;
}
