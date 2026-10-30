#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PAGE 4096u
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"V080_CONCURRENT_IO_FAIL line=%d\n",__LINE__); return 2; } } while(0)
typedef struct {
    nopen_shadow *s;
    atomic_int failed;
} context;
static void *writer(void *arg) {
    context *c=arg;
    unsigned char page[PAGE];
    for(unsigned i=0;i<500;i++) {
        memset(page,(unsigned char)(i+1),PAGE);
        if(nopen_shadow_write(c->s,0,page,PAGE)!=0){atomic_store(&c->failed,1);break;}
    }
    return NULL;
}
static void *reader(void *arg) {
    context *c=arg;
    unsigned char page[PAGE];
    for(unsigned i=0;i<600;i++) {
        if(nopen_shadow_read(c->s,0,page,PAGE)!=0){atomic_store(&c->failed,1);break;}
        for(size_t j=1;j<PAGE;j++)if(page[j]!=page[0]){
            atomic_store(&c->failed,1);return NULL;
        }
    }
    return NULL;
}
int main(int argc,char **argv) {
    CHECK(argc==3 && argv[1][0]=='/' && argv[2][0]=='/');
    CHECK(setenv("NOPEN_SHADOW_ASYNC_GPU","1",1)==0);
    char dir[4096],path[4096];
    int n=snprintf(dir,sizeof dir,"%s/nopen-concurrent-XXXXXX",argv[2]);
    CHECK(n>0 && (size_t)n<sizeof dir);
    CHECK(mkdtemp(dir)!=NULL);
    n=snprintf(path,sizeof path,"%s/store.bin",dir);
    CHECK(n>0 && (size_t)n<sizeof path);
    nopen_shadow *s=NULL;
    CHECK(nopen_shadow_open(&s,NOPEN_STORE_MOCK,4,1000,argv[1],path)==0);
    context state={.s=s,.failed=0};
    pthread_t w,r;
    CHECK(pthread_create(&w,NULL,writer,&state)==0);
    CHECK(pthread_create(&r,NULL,reader,&state)==0);
    CHECK(pthread_join(w,NULL)==0);
    CHECK(pthread_join(r,NULL)==0);
    CHECK(atomic_load(&state.failed)==0);
    CHECK(nopen_shadow_simulate_gpu_loss(s)==0);
    unsigned char page[PAGE];
    CHECK(nopen_shadow_read(s,0,page,PAGE)==0);
    for(size_t j=0;j<PAGE;j++)CHECK(page[j]==(unsigned char)500u);
    nopen_shadow_stats stats={0};
    CHECK(nopen_shadow_get_stats(s,&stats)==0);
    CHECK(stats.writes==500 && stats.reads>=601 &&
          stats.disk_failures==0 && stats.poisoned==0);
    nopen_shadow_close(s);
    CHECK(unlink(path)==0);
    CHECK(rmdir(dir)==0);
    puts("V080_ALPHA8_CONCURRENT_IO_PASS"
         " disk_mutex_serializes=YES gpu_worker_isolated=YES"
         " worker_loss_recovered=YES 500_writes=YES 601_reads=YES"
         " kernel_swap=NO");
    return 0;
}
