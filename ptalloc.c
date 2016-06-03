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


static void handle_stream(char *up, char *down, int fptmaster);

int main(int argc, char *argv[]) {
    if(argc < 3) fail_err(EINVAL);

    int fptmaster = ensure(posix_openpt(O_RDWR));
    ensure(grantpt(fptmaster));
    ensure(unlockpt(fptmaster));

    char *slavename = ensure_p(ptsname(fptmaster));
    printf("PTALLOC_TTY=%s\n", slavename);
    fprintf(stderr, "Allocated tty is %s\n", slavename);

    int cpid = ensure(fork());
    if(cpid == 0) {
        ensure(setsid());
        int null = ensure(open("/dev/null", O_RDWR));
        ensure(dup2(null, STDIN_FILENO));
        ensure(dup2(null, STDOUT_FILENO));
        if(null > 1) close(null);

        handle_stream(argv[1], argv[2], fptmaster);
        exit(1);
    }

    printf("PTALLOC_PID=%d\n", cpid);
    fprintf(stderr, "The pid is %d\n", cpid);

    return 0;
}


#define LOCAL_BUF_SIZE 1024

static int start_upstream(char *up, int fptmaster);
static int start_downstream(char *down, int fptmaster);

static volatile int closing = 0;
static void close_handler(int sig) { closing = 1; }

static void handle_stream(char *up, char *down, int fptmaster) {
    int uppid = start_upstream(up, fptmaster);
    int downpid = start_downstream(down, fptmaster);

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
                kill(downpid, SIGKILL);
                fail_err(tmperrno);
            }
        } else if (!closing) {
            sleep(1);
            if(rc == uppid ) uppid = start_upstream(up, fptmaster);
            else if(rc == downpid) downpid = start_downstream(down, fptmaster);
        }
    }

    exit(0);
}

static int start_upstream(char *up, int fptmaster) {
    int ret = ensure(fork());
    if(ret == 0) {
        int fup = ensure(open(up, O_RDONLY));
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

static int start_downstream(char *down, int fptmaster) {
    int ret = ensure(fork());
    if(ret == 0) {
        int fdown = ensure(open(down, O_WRONLY));
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int rc = read(fptmaster, bufptr, sizeof(buf));
            IF_err(rc) {
                // master will return EIO if none open the slave
                // https://github.com/torvalds/linux/blob/v4.2/drivers/tty/n_tty.c#L2268-L2274
                if(errno == EIO) break;
                else fail_eno();
            } else {
                int size = rc;
                if(size == 0) break;
                while(size > 0) {
                    int rc = ensure(write(fdown, bufptr, size));
                    bufptr += rc; size -= rc;
                }
            }
        }
        exit(0);
    }
    return ret;
}
