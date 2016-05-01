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

static char buf[2048];

int main(int argc, char *argv[]) {
    if(argc < 3) fail_err(EINVAL);

    int in = ensure(open(argv[1], O_WRONLY));
    int out = ensure(open(argv[2], O_RDONLY));
    int err = -1;
    int ecode = -1;
    if(argc == 4) err = ensure(open(argv[3], O_RDONLY));

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

    int inpid = ensure(fork());
    if(inpid == 0) {
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(0, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(in, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }

    int outpid = ensure(fork());
    if(outpid == 0) {
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(out, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(1, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }

    int errpid = -1;
    if(err != -1) {
        errpid = ensure(fork());
        if(errpid == 0) {
            while(1) {
                char *bufptr = buf;
                int size = ensure(read(err, bufptr, sizeof(buf)));
                if(size == 0) break;
                while(size > 0) {
                    int rc = ensure(write(2, bufptr, size));
                    bufptr += rc; size -= rc;
                }
            }
            _exit(0);
        }
    }

    while(1) {
        int rc = wait(NULL);
        IF_err(rc) {
            if(errno == ECHILD) break;
            else fail_eno();
        }
        kill(inpid, SIGKILL);
        kill(outpid, SIGKILL);
        if(errpid != -1) kill(errpid, SIGKILL);
    }

    if(ttyfd != -1) ensure(tcsetattr(ttyfd, TCSANOW, &oldterm));

    int status = 0;
    if(ecode != -1) {
        char *bufptr = buf;
        int size = 0;
        while(1) {
            int rc = ensure(read(ecode, bufptr, sizeof(buf) - 1 - size));
            if(rc == 0) break;
            bufptr += rc; size += rc;
            if(memrchr(buf, '\n', size) != NULL) break;
        }
        buf[size] = 0;
        int tmp;
        if(sscanf(buf, "%d", &tmp) > 0) status = tmp;
    }

    return status;
}
