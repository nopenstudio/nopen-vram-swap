#define _POSIX_C_SOURCE 200809L
#include "nopen_shadow.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#define P NOPEN_PAGE_SIZE
#define PAGECOUNT 1024u
#define ASSERT(q) do{if(!(q)){fprintf(stderr,"V070_SHADOW_TEST_FAILED line=%d check=%s\n",__LINE__,#q);return 1;}}while(0)

int main(int argc,char **argv) {
    /* Garuda often mounts /tmp as tmpfs. A swap shadow must NEVER be on
     * tmpfs, even for the mock test: test it under the CMake build directory
     * on real disk, as the already-passing nopen-shadow-smoke does. */
    ASSERT(argc==3 && argv[1][0]=='/' && argv[2][0]=='/');
    char directory[4096]={0};
    int n=snprintf(directory,sizeof directory,"%s/nopen-shadow-ci-XXXXXX",argv[2]);
    ASSERT(n>0 && (size_t)n<sizeof directory);
    ASSERT(mkdtemp(directory)!=NULL);
    char path[4096]={0};
    n=snprintf(path,sizeof path,"%s/virgin-shadow.bin",directory);
    ASSERT(n>0 && (size_t)n<sizeof path);
    nopen_shadow *s=NULL;
    nopen_shadow_stats stats={0};
    unsigned char expected[PAGECOUNT*P]={0},read_buf[16u*P],src[16u*P];
    ASSERT(nopen_shadow_open(&s,NOPEN_STORE_MOCK,4,500,argv[1],path)==0);
    ASSERT(s!=NULL);
    struct statfs filesystem={0};
    ASSERT(statfs(path,&filesystem)==0);
    if((unsigned long)filesystem.f_type==0x9123683eUL) {
        /* Runtime proof, not just source-code string search: the actual
         * new inode on Btrfs must have COW/compression disabled. */
        int fd=open(path,O_RDONLY|O_CLOEXEC);
        ASSERT(fd>=0);
        int flags=0;
        ASSERT(ioctl(fd,FS_IOC_GETFLAGS,&flags)==0);
        ASSERT((flags&FS_NOCOW_FL)!=0);
        ASSERT((flags&FS_COMPR_FL)==0);
        close(fd);
        puts("V070_BTRFS_NOCOW_FLAG_PASS actual_inode_nocow=YES compression=NO");
    }
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
    /* Corrupt the independent SSD backup deliberately: checksum must reject
     * bad bytes and leave caller's memory completely unchanged. */
    int fd=open(path,O_RDWR);
    ASSERT(fd>=0);
    unsigned char b=0;
    ASSERT(pread(fd,&b,1,0)==1);
    b^=0xA5u;
    ASSERT(pwrite(fd,&b,1,0)==1);
    ASSERT(fsync(fd)==0);
    close(fd);
    memset(read_buf,0xDA,P);
    ASSERT(nopen_shadow_read(s,0,read_buf,P)==-EIO);
    for(size_t i=0;i<P;i++)ASSERT(read_buf[i]==0xDA);
    ASSERT(nopen_shadow_get_stats(s,&stats)==0);
    ASSERT(stats.poisoned && stats.disk_failures==1);
    ASSERT(nopen_shadow_write(s,0,src,P)==-EIO);
    nopen_shadow_close(s);
    unlink(path);rmdir(directory);
    puts("V070_SHADOW_TEST_PASS gpu_timeout=YES persisted_pages=1024 "
         "fallback_read=YES fallback_write=YES backup_corruption_rejected=YES no_unreadable_acknowledged_pages=YES "
         "real_gpu_reset_tested=NO no_global_swap=YES");
    return 0;
}
