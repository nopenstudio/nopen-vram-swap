#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define P NOPEN_PAGE_SIZE
#define CHECK(x) do{if(!(x)){fprintf(stderr, \
    "V080_ZERO_TEST_FAILED line=%d: %s\n",__LINE__,#x);return 1;}}while(0)

int main(int argc,char **argv) {
    CHECK(argc==3 && argv[1][0]=='/' && argv[2][0]=='/');
    char filename[4096];
    int n=snprintf(filename,sizeof filename,"%s/v080-zero-fastpath-XXXXXX",argv[2]);
    CHECK(n>0 && (size_t)n<sizeof filename);
    /* O_EXCL implementation creates the shadow: mkstemp would be wrong. */
    char *x=strstr(filename,"XXXXXX");
    CHECK(x!=NULL);
    snprintf(x,7,"%06ld",(long)getpid()%1000000L);
    CHECK(access(filename,F_OK)!=0);
    nopen_shadow *s=NULL;
    CHECK(nopen_shadow_open(&s,NOPEN_STORE_MOCK,4,500,argv[1],filename)==0);
    unsigned char data[2*P],got[2*P];
    memset(got,0xCC,sizeof got);
    CHECK(nopen_shadow_read(s,0,got,sizeof got)==0);
    for(size_t i=0;i<sizeof got;i++)CHECK(got[i]==0);
    nopen_shadow_stats st={0};
    CHECK(nopen_shadow_get_stats(s,&st)==0);
    CHECK(st.zero_reads==1 && st.gpu_reads==0 && st.shadow_reads==0);
    for(size_t i=0;i<P;i++)data[i]=(unsigned char)(i*13u+99u);
    CHECK(nopen_shadow_write(s,0,data,P)==0);
    memset(got,0xEE,sizeof got);
    CHECK(nopen_shadow_read(s,0,got,2*P)==0);
    CHECK(memcmp(got,data,P)==0);
    for(size_t i=P;i<2*P;i++)CHECK(got[i]==0);
    CHECK(nopen_shadow_get_stats(s,&st)==0);
    CHECK(st.zero_reads==1 && st.gpu_reads==1);
    CHECK(nopen_shadow_simulate_gpu_loss(s)==0);
    /* Unknown pages remain zero even when the GPU is unavailable. */
    memset(got,0xDD,sizeof got);
    CHECK(nopen_shadow_read(s,2*P,got,2*P)==0);
    for(size_t i=0;i<2*P;i++)CHECK(got[i]==0);
    CHECK(nopen_shadow_get_stats(s,&st)==0);
    CHECK(st.zero_reads==2 && st.shadow_reads==0);
    /* Previously written pages must use authoritative disk when GPU lost. */
    memset(got,0xEE,sizeof got);
    CHECK(nopen_shadow_read(s,0,got,P)==0);
    CHECK(memcmp(got,data,P)==0);
    CHECK(nopen_shadow_get_stats(s,&st)==0);
    CHECK(st.shadow_reads==1);
    /* Disk writes after worker loss still change the bitmap and checksum. */
    memset(data,0x7C,P);
    CHECK(nopen_shadow_write(s,5*P,data,P)==0);
    CHECK(nopen_shadow_read(s,5*P,got,P)==0);
    CHECK(memcmp(data,got,P)==0);
    CHECK(nopen_shadow_get_stats(s,&st)==0);
    CHECK(st.shadow_reads==2 && st.zero_reads==2);
    nopen_shadow_close(s);
    CHECK(unlink(filename)==0);
    puts("V080_ZERO_FASTPATH_PASS untouched_range_no_gpu_or_disk=YES"
         " mixed_range_integrity=YES worker_loss=YES"
         " durable_write_stays_authoritative=YES");
    return 0;
}
