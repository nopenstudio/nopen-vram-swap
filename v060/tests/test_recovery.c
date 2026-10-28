#include "nopen_recovery.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define P NOPEN_PAGE_SIZE
#define CHECK(x) do {if(!(x)){fprintf(stderr,"RECOVERY_TEST_FAIL line=%d expr=%s\n",__LINE__,#x);return 1;}}while(0)

static void fill(unsigned char *p,size_t len,uint32_t salt) {
    for(size_t i=0;i<len;i++)p[i]=(unsigned char)(salt^(i*7u)^(i>>3u));
}
int main(void) {
    unsigned char src[4u*P],dst[4u*P],src2[4u*P];
    nopen_recovery *r=NULL;
    nopen_recovery_stats s;
    fill(src,sizeof src,42);fill(src2,sizeof src2,88);
    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_NO_MIRROR)==0);
    CHECK(nopen_recovery_write(r,0,src,sizeof src)==0);
    CHECK(nopen_recovery_read(r,0,dst,sizeof dst)==0);
    CHECK(memcmp(dst,src,sizeof src)==0);
    CHECK(nopen_recovery_device_lost(r)==-EIO);
    memset(dst,0xCB,sizeof dst);
    CHECK(nopen_recovery_read(r,0,dst,sizeof dst)==-EIO);
    for(size_t i=0;i<sizeof dst;i++)CHECK(dst[i]==0xCB);
    CHECK(nopen_recovery_write(r,0,src2,sizeof src2)==-EIO);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.state==NOPEN_RECOVERY_FAILED_CLOSED&&s.device_loss_events==1);
    CHECK(nopen_recovery_device_lost(r)==-EIO);
    CHECK(nopen_recovery_get_stats(r,&s)==0&&s.device_loss_events==1);
    nopen_recovery_close(r);r=NULL;

    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_HOST_MIRROR)==0);
    CHECK(nopen_recovery_write(r,0,src,sizeof src)==0);
    CHECK(nopen_recovery_read(r,0,dst,sizeof dst)==0);
    CHECK(memcmp(dst,src,sizeof src)==0);
    /* A GPU read fault must fall back to verified last ACKed mirror. */
    CHECK(nopen_recovery_inject_io_failure(r,1)==0);
    memset(dst,0xAA,sizeof dst);
    CHECK(nopen_recovery_read(r,0,dst,sizeof dst)==0);
    CHECK(memcmp(dst,src,sizeof src)==0);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.state==NOPEN_RECOVERY_CPU_FALLBACK);
    CHECK(s.gpu_read_errors==1&&s.fallback_reads==1);
    CHECK(s.mirror_bytes==4u*1024u*1024u);
    CHECK(nopen_recovery_inject_io_failure(r,1)==-EPERM);
    CHECK(nopen_recovery_write(r,0,src2,sizeof src2)==0);
    CHECK(nopen_recovery_read(r,0,dst,sizeof dst)==0);
    CHECK(memcmp(dst,src2,sizeof src2)==0);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.fallback_writes==1&&s.fallback_reads==2);
    CHECK(nopen_recovery_device_lost(r)==0);
    CHECK(nopen_recovery_get_stats(r,&s)==0&&s.device_loss_events==1);
    CHECK(nopen_recovery_read(r,1,dst,P)==-EINVAL);
    CHECK(nopen_recovery_write(r,0,src,3)==-EINVAL);
    CHECK(nopen_recovery_read(r,0,NULL,P)==-EINVAL);
    CHECK(nopen_recovery_write(r,4u*1024u*1024u,src,P)==-EINVAL);
    CHECK(nopen_recovery_read(r,0,dst,0)==-EINVAL);
    nopen_recovery_close(r);r=NULL;

    /* A failed GPU write must never ACK a write without authoritative copy. */
    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_NO_MIRROR)==0);
    CHECK(nopen_recovery_write(r,P,src,P)==0);
    CHECK(nopen_recovery_inject_io_failure(r,1)==0);
    CHECK(nopen_recovery_write(r,P,src2,P)==-EIO);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.gpu_write_errors==1&&s.state==NOPEN_RECOVERY_FAILED_CLOSED);
    nopen_recovery_close(r);r=NULL;

    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_HOST_MIRROR)==0);
    CHECK(nopen_recovery_write(r,P,src,P)==0);
    CHECK(nopen_recovery_inject_io_failure(r,1)==0);
    CHECK(nopen_recovery_write(r,P,src2,P)==0);
    CHECK(nopen_recovery_read(r,P,dst,P)==0);
    CHECK(memcmp(dst,src2,P)==0);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.gpu_write_errors==1 && s.fallback_writes==1 &&
          s.state==NOPEN_RECOVERY_CPU_FALLBACK);
    nopen_recovery_close(r);r=NULL;
    /* Explicit 1MiB host budget refuses a 4MiB CPU mirror. */
    CHECK(nopen_recovery_open_bounded(&r,NOPEN_STORE_MOCK,4,
           NOPEN_RECOVERY_HOST_MIRROR,1024u*1024u)==-ENOMEM);
    CHECK(r==NULL);
    CHECK(nopen_recovery_open_bounded(&r,NOPEN_STORE_MOCK,4,
           NOPEN_RECOVERY_NO_MIRROR,1024u*1024u)==0);
    nopen_recovery_close(r);r=NULL;

    /* Mirror corruption after device loss must reject read without exposing
     * incorrect bytes. */
    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,NOPEN_RECOVERY_HOST_MIRROR)==0);
    CHECK(nopen_recovery_write(r,0,src,P)==0);
    CHECK(nopen_recovery_device_lost(r)==0);
    CHECK(nopen_recovery_inject_mirror_corruption(r,0)==0);
    memset(dst,0xD3,sizeof dst);
    CHECK(nopen_recovery_read(r,0,dst,P)==-EIO);
    for(size_t i=0;i<P;i++)CHECK(dst[i]==0xD3);
    CHECK(nopen_recovery_get_stats(r,&s)==0);
    CHECK(s.integrity_failures==1 && s.state==NOPEN_RECOVERY_FAILED_CLOSED);
    CHECK(nopen_recovery_write(r,0,src2,P)==-EIO);
    nopen_recovery_close(r);r=NULL;

    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,0,NOPEN_RECOVERY_HOST_MIRROR)==-EINVAL);
    CHECK(nopen_recovery_open(&r,NOPEN_STORE_MOCK,4,(nopen_recovery_mode)17)==-EINVAL);
    puts("RECOVERY_TEST_PASS gpu_failure=YES fail_closed=YES mirror_verified=YES "
         "cpu_fallback=YES no_fake_reset=YES no_global_swap=YES");
    return 0;
}
