#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
int main(int argc,char **argv) {
    if(argc!=3 || strcmp(argv[1],"--mode")!=0)return 2;
    if(strcmp(argv[2],"pass")==0) {
        puts("WATCHDOG_FIXTURE_PASS no_global_swap=YES");
        return 0;
    }
    if(strcmp(argv[2],"exitfail")==0)return 9;
    if(strcmp(argv[2],"silent")==0)return 0;
    if(strcmp(argv[2],"stall")==0) {
        struct timespec req={.tv_sec=8,.tv_nsec=0};
        while(nanosleep(&req,&req)!=0 && errno==EINTR){}
        return 0;
    }
    return 2;
}
