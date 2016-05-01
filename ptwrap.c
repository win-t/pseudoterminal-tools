#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define FAIL_WITH_PPID
#include "fail.h"


static char buf[2048];

int main(int argc, char *argv[]) {
    if(argc < 2) fail_err(EINVAL);

    int ptmaster;
    int ptslave;

    ptmaster = ensure(posix_openpt(O_RDWR));
    ensure(grantpt(ptmaster));
    ensure(unlockpt(ptmaster));
    ptslave = ensure(open(ptsname(ptmaster), O_RDWR));

    int cpid = ensure(fork());
    if(cpid == 0) {
        ensure(close(ptmaster));

        ensure(setsid());
        ensure(ioctl(ptslave, TIOCSCTTY, 0));

        ensure(close(0));
        ensure(dup(ptslave));

        ensure(close(1));
        ensure(dup(ptslave));

        ensure(close(2));
        ensure(dup(ptslave));

        ensure(close(ptslave));

        ensure(execvp(argv[1], &argv[1]));
        _exit(1);
    }

    int uppid = ensure(fork());
    if(uppid == 0) {
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(0, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(ptmaster, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }

    int downpid = ensure(fork());
    if(downpid == 0) {
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(ptmaster, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(1, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }

    int status;
    while(1) {
        int tmp;
        int rc = waitpid(-1, &tmp, 0);
        IF_err(rc) {
            if(errno == ECHILD) break;
            else fail_eno();
        }
        if(rc == cpid) {
            status = WEXITSTATUS(tmp);
            kill(uppid, SIGKILL);
            kill(downpid, SIGKILL);
        }
    }

    return status;
}
