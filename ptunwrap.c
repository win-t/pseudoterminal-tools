#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "fail.h"

#define LOCAL_BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if(argc < 3) fail_err(EINVAL);

    int up = ensure(open(argv[1], O_WRONLY));
    int down = ensure(open(argv[2], O_RDONLY));

    int ttyfd = -1;
    struct termios oldterm, newterm;

    for(int i = 0; i < 3; ++i) {
        if(isatty(i)) {
            ttyfd = i;
            break;
        }
    }

    if(ttyfd != -1) {
        ensure(tcgetattr(ttyfd, &oldterm));
        newterm = oldterm;
        cfmakeraw(&newterm);
        ensure(tcsetattr(ttyfd, TCSANOW, &newterm));
    }

    int uppid = ensure(fork());
    if(uppid == 0) {
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(0, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(up, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }

    int downpid = ensure(fork());
    if(downpid == 0) {
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(down, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(1, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }

    while(1) {
        int rc = wait(NULL);
        IF_err(rc) {
            if(errno == ECHILD) break;
            else fail_eno();
        }
        if(rc == downpid) {
            kill(uppid, SIGKILL);
        }
    }

    if(ttyfd != -1) ensure(tcsetattr(ttyfd, TCSANOW, &oldterm));

    return 0;
}
