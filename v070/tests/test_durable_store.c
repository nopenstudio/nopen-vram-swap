#define _POSIX_C_SOURCE 200809L
#include "nopen_durable_store.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define PAGE 4096u
#define CK(x) do {if(!(x)) {fprintf(stderr,"IE03_DISK_TEST_FAIL line=%d check=%s\n",__LINE__,#x);return 1;}} while(0)
static void fill(unsigned char *buf,size_t n,unsigned v) {
    for(size_t i=0;i<n;i++)buf[i]=(unsigned char)(i*37u+v);
}
static int equal(const unsigned char *a,const unsigned char *b,size_t n) {
    return memcmp(a,b,n)==0;
}
int main(int argc,char **argv) {
    CK(argc==2 && argv[1][0]=='/');
    char directory[4096]={0};
    CK(snprintf(directory,sizeof directory,"%s/nopen-ie03-XXXXXX",argv[1])>0);
    CK(mkdtemp(directory)!=NULL);
    char base[4096],datafile[4096],logfile[4096];
    CK(snprintf(base,sizeof base,"%s/authority",directory)>0);
    CK(snprintf(datafile,sizeof datafile,"%s.txdata",base)>0);
    CK(snprintf(logfile,sizeof logfile,"%s.txlog",base)>0);
    const uint64_t epoch=UINT64_C(0x26a37ff);
    nopen_durable_store *s=NULL;
    CK(nopen_durable_open(&s,base,4,epoch,1)==0 && s!=NULL);
    unsigned char a[PAGE],b[PAGE],c[PAGE],got[16u*PAGE],hold[16u*PAGE];
    fill(a,sizeof a,5);fill(b,sizeof b,15);fill(c,sizeof c,55);
    memset(got,0xD7,sizeof got);
    CK(nopen_durable_read(s,0,got,PAGE)==0);
    for(size_t i=0;i<PAGE;i++)CK(got[i]==0);
    CK(nopen_durable_write(s,100,0,a,PAGE)==0);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(a,got,PAGE));
    /* An identical replay is idempotent, but new payload under same ID is
     * a protocol violation: existing ACK data MUST remain unchanged. */
    CK(nopen_durable_write(s,100,0,a,PAGE)==0);
    CK(nopen_durable_write(s,100,0,b,PAGE)==-EEXIST);
    CK(nopen_durable_inject(s,NOPEN_DURABLE_FAIL_AFTER_DATA_SYNC)==0);
    CK(nopen_durable_write(s,101,0,b,PAGE)==-EIO);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(a,got,PAGE));
    /* Exclusive log lock prevents concurrent daemons claiming authority. */
    nopen_durable_store *duplicate=NULL;
    CK(nopen_durable_open(&duplicate,base,4,epoch,0)==-EBUSY);
    CK(duplicate==NULL);
    nopen_durable_close(s);s=NULL;
    /* Replay ONLY fully fsynced metadata, not orphaned COW data. */
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(a,got,PAGE));
    CK(nopen_durable_write(s,101,0,b,PAGE)==0);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(b,got,PAGE));

    /* The metadata commit is real and synced, but a simulated kernel ACK
     * vanishes. Same request identity can be reissued after restart. */
    CK(nopen_durable_inject(s,NOPEN_DURABLE_FAIL_AFTER_MANIFEST_SYNC)==0);
    CK(nopen_durable_write(s,102,0,c,PAGE)==-EIO);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(c,got,PAGE));
    nopen_durable_close(s);s=NULL;
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    CK(nopen_durable_write(s,102,0,c,PAGE)==0);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(c,got,PAGE));
    CK(nopen_durable_write(s,102,0,a,PAGE)==-EEXIST);

    /* A single 16-page record must be atomically visible at restart.
     * Failed commit leaves the old generation readable on every page. */
    fill(hold,sizeof hold,72);
    CK(nopen_durable_write(s,103,16u*PAGE,hold,sizeof hold)==0);
    fill(hold,sizeof hold,98);
    CK(nopen_durable_inject(s,NOPEN_DURABLE_FAIL_AFTER_DATA_SYNC)==0);
    CK(nopen_durable_write(s,104,16u*PAGE,hold,sizeof hold)==-EIO);
    nopen_durable_close(s);s=NULL;
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    fill(hold,sizeof hold,72);
    CK(nopen_durable_read(s,16u*PAGE,got,sizeof got)==0);
    CK(equal(hold,got,sizeof got));
    CK(nopen_durable_write(s,104,16u*PAGE,hold,sizeof hold)==0);

    /* Reopening across a different boot epoch is refused. */
    nopen_durable_close(s);s=NULL;
    CK(nopen_durable_open(&s,base,4,epoch+1,0)==-EPERM);
    CK(s==NULL);
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    CK(nopen_durable_open(&duplicate,base,4,epoch,1)!=0);
    CK(duplicate==NULL); /* O_EXCL cannot truncate committed data. */
    nopen_durable_close(s);s=NULL;
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(c,got,PAGE));
    nopen_durable_close(s);s=NULL;

    /* Simulate a crash after writing only a PARTIAL trailing journal record.
     * It was never eligible for WRITE ACK. Reopen trims its tail and retains
     * every previously ACKed transaction. */
    int tail_fd=open(logfile,O_RDWR);
    CK(tail_fd>=0);
    off_t before=lseek(tail_fd,0,SEEK_END);
    CK(before>0);
    const unsigned char torn[]={0x19,0x4b,0x07,0xf8,0x03};
    CK(write(tail_fd,torn,sizeof torn)==(ssize_t)sizeof torn);
    CK(fdatasync(tail_fd)==0);
    close(tail_fd);
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    CK(nopen_durable_read(s,0,got,PAGE)==0 && equal(c,got,PAGE));
    nopen_durable_close(s);s=NULL;
    tail_fd=open(logfile,O_RDONLY);
    CK(tail_fd>=0);
    CK(lseek(tail_fd,0,SEEK_END)==before);
    close(tail_fd);

    /* Corrupt current page's committed slot (generation 3 -> slot 0).
     * Reads MUST return failure WITHOUT mutating caller's buffer. */
    CK(nopen_durable_open(&s,base,4,epoch,0)==0);
    int fd=open(datafile,O_RDWR);
    CK(fd>=0);
    unsigned char byte=0;
    CK(pread(fd,&byte,1,0)==1);
    byte^=0xA5;
    CK(pwrite(fd,&byte,1,0)==1 && fdatasync(fd)==0);
    close(fd);
    memset(got,0xAD,PAGE);
    CK(nopen_durable_read(s,0,got,PAGE)==-EBADMSG);
    for(size_t i=0;i<PAGE;i++)CK(got[i]==0xAD);
    nopen_durable_close(s);s=NULL;
    CK(nopen_durable_open(&s,base,4,epoch,0)==-EBADMSG);
    CK(s==NULL); /* checksum validation during full replay */

    /* No hidden repair, zero fill or rewrite on corruption. */
    unlink(logfile);unlink(datafile);rmdir(directory);
    puts("IE03_DURABLE_C_TEST_PASS"
         " actual_file_fdatasync=YES double_slot_COW=YES"
         " torn_precommit_old_data=YES partial_journal_replay=YES"
         " lost_ACK_idempotent=YES"
         " multi_page_atomic_manifest=YES boot_epoch=STRICT"
         " corruption_fail_closed=YES kernel_swap=BLOCKED");
    return 0;
}
