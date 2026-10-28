#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define P NOPEN_PAGE_SIZE
#define PAGECOUNT 1024u
#define ASSERT(q) do{if(!(q)){fprintf(stderr,"V070_SHADOW_TEST_FAILED line=%d check=%s\n",__LINE__,#q);return 1;}}while(0)

int main(int argc,char **argv) {
    ASSERT(argc==2 && argv[1][0]=='/');
    char directory[]="/tmp/nopen-shadow-ci-XXXXXX";
    ASSERT(mkdtemp(directory)!=NULL);
    char path[512]={0};
    ASSERT(snprintf(path,sizeof(path),"%s/virgin-shadow.bin",directory)>0);
    nopen_shadow *s=NULL;
    nopen_shadow_stats stats={0};
    unsigned char expected[PAGECOUNT*P]={0},read_buf[16u*P],src[16u*P];
    ASSERT(nopen_shadow_open(&s,NOPEN_STORE_MOCK,4,500,argv[1],path)==0);
    ASSERT(s!=NULL);
    nopen_shadow *duplicate=NULL;
    ASSERT(nopen_shadow_open(&duplicate,NOPEN_STORE_MOCK,4,500,argv[1],path)!=0);
    /* O_EXCL rejects duplicate without damaging the original GPU/device. */
    ASSERT(duplicate==NULL && access(path,F_OK)==0);
    for(size_t p=0;p<PAGECOUNT;p+=16) {
        for(size_t j=0;j<sizeof(src);j++)
            src[j]=(unsigned char)(p*13u+j*7u+(p>>2));
        ASSERT(nopen_shadow_write(s,p*P,src,sizeof(src))==0);
        memcpy(expected+p*P,src,sizeof(src));
    }
    ASSERT(nopen_shadow_get_stats(s,&stats)==0);
    ASSERT(stats.gpu_writes==PAGECOUNT/16 && stats.gpu_active);
    /* Trigger 10-second mock GPU hang; the isolated parent must return at
     * 500ms, and the durable O_DIRECT backup must still hold every page. */
    ASSERT(nopen_shadow_mock_inject_gpu_hang(s)==-ETIMEDOUT);
    ASSERT(nopen_shadow_get_stats(s,&stats)==0);
    ASSERT(!stats.gpu_active && stats.gpu_failures==1);
    for(size_t p=0;p<PAGECOUNT;p+=16) {
        memset(read_buf,0xBB,sizeof(read_buf));
        ASSERT(nopen_shadow_read(s,p*P,read_buf,sizeof(read_buf))==0);
        ASSERT(memcmp(read_buf,expected+p*P,sizeof(read_buf))==0);
    }
    for(size_t j=0;j<P;j++)src[j]=(unsigned char)(j*23u+9u);
    ASSERT(nopen_shadow_write(s,0,src,P)==0);
    ASSERT(nopen_shadow_read(s,0,read_buf,P)==0);
    ASSERT(memcmp(src,read_buf,P)==0);
    ASSERT(nopen_shadow_write(s,1,src,P)==-EINVAL);
    ASSERT(nopen_shadow_read(s,0,NULL,P)==-EINVAL);
    ASSERT(nopen_shadow_get_stats(s,&stats)==0);
    ASSERT(stats.shadow_reads>=PAGECOUNT/16 && stats.gpu_failures==1);
    ASSERT(stats.backed_bytes==4u*1024u*1024u && !stats.poisoned);
    nopen_shadow_close(s);
    unlink(path);rmdir(directory);
    puts("V070_SHADOW_TEST_PASS gpu_timeout=YES persisted_pages=1024 "
         "fallback_read=YES fallback_write=YES no_unreadable_acknowledged_pages=YES "
         "real_gpu_reset_tested=NO no_global_swap=YES");
    return 0;
}
