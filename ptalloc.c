#define _XOPEN_SOURCE 600

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define FAIL_WITH_PPID
#include "fail.h"


static void handle_stream(int fup, int fdown, int fptmaster);

int main(int argc, char *argv[]) {
    close(0);

    if(argc < 3) fail_err(EINVAL);

    int fptmaster;
    struct stat tmpstat;
    memset(&tmpstat, 0, sizeof(tmpstat));

    fptmaster = ensure(posix_openpt(O_RDWR));
    ensure(grantpt(fptmaster));
    ensure(unlockpt(fptmaster));

    char *slavename = ensure_p(ptsname(fptmaster));
    printf("PTALLOC_TTY=%s\n", slavename);

    int cpid = ensure(fork());
    if(cpid == 0) {
        ensure(setsid());
        if(ensure(fork()) == 0) {
            ensure(setsid());
            int fup, fdown;

            fup = ensure(open(argv[1], O_RDONLY));
            ensure(fstat(fup, &tmpstat));
            if(!(tmpstat.st_mode & S_IFIFO)) fail_err(EINVAL);

            fdown = ensure(open(argv[2], O_WRONLY));
            ensure(fstat(fdown, &tmpstat));
            if(!(tmpstat.st_mode & S_IFIFO)) fail_err(EINVAL);

            handle_stream(fup, fdown, fptmaster);
        }
        exit(0);
    }

    printf("PTALLOC_PID=%d\n", cpid);

    return 0;
}


#define LOCAL_BUF_SIZE 1024

static int restart_upstream(int fup, int fdown, int fptmaster);
static int restart_downstream(int fup, int fdown, int fptmaster);

static volatile int closing = 0;
static void close_handler(int sig) { closing = 1; }

static void handle_stream(int fup, int fdown, int fptmaster) {
    int uppid = restart_upstream(fup, fdown, fptmaster);
    int downpid = restart_downstream(fup, fdown, fptmaster);

    struct sigaction close;
    memset(&close, 0, sizeof(close));
    close.sa_handler = close_handler;

    ensure(sigaction(SIGTERM, &close, NULL));
    ensure(sigaction(SIGINT, &close, NULL));

    while(1) {
        int tmp;
        int rc = waitpid(-1, &tmp, 0);
        IF_err(rc) {
            if(errno == ECHILD) break;
            else if(errno == EINTR && closing) {
                kill(uppid, SIGKILL);
                kill(downpid, SIGKILL);
            } else {
                int tmperrno = errno;
                kill(uppid, SIGKILL);
                waitpid(-1, NULL, WNOHANG);
                kill(downpid, SIGKILL);
                waitpid(-1, NULL, WNOHANG);
                fail_err(tmperrno);
            }
        } else if ((!WIFEXITED(tmp) || WEXITSTATUS(tmp) != 0) && !closing) {
            if(rc == uppid ) uppid = restart_upstream(fup, fdown, fptmaster);
            else if(rc == downpid) downpid = restart_downstream(fup, fdown, fptmaster);
        }
    }

    exit(0);
}

static int restart_upstream(int fup, int fdown, int fptmaster) {
    int ret = ensure(fork());
    if(ret == 0) {
        close(fdown);
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(fup, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(fptmaster, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        exit(0);
    }
    return ret;
}

static int restart_downstream(int fup, int fdown, int fptmaster) {
    int ret = ensure(fork());
    if(ret == 0) {
        close(fup);
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(fptmaster, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(fdown, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        exit(0);
    }
    return ret;
}
