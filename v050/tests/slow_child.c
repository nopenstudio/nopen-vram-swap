#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <errno.h>
int main(void) {
    struct timespec req={.tv_sec=6,.tv_nsec=0};
    while(nanosleep(&req,&req)!=0 && errno==EINTR){}
    return 0;
}
