#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

#define FAIL_WITH_PPID
#include "fail.h"

#define LOCAL_BUF_SIZE 1024

static int siglist[] = {
    SIGHUP,
    SIGINT,
    SIGQUIT,
    SIGTERM,
    SIGUSR1,
    SIGUSR2,
};

static volatile int cpid = 0;
static void forward_handler(int sig);
static int start_upstream(int ptmaster);
static int start_downstream(int ptmaster);

int main(int argc, char *argv[]) {
    if(argc < 2) fail_err(EINVAL);

    int ptmaster;
    struct sigaction forward;

    memset(&forward, 0, sizeof(forward));
    forward.sa_handler = forward_handler;

    ptmaster = ensure(posix_openpt(O_RDWR));
    ensure(grantpt(ptmaster));
    ensure(unlockpt(ptmaster));

    int cpid = ensure(fork());
    if(cpid == 0) {
        ensure(setsid());

        int ptslave = ensure(open(ensure_p(ptsname(ptmaster)), O_RDWR));
        ensure(close(ptmaster));

        ensure(ioctl(ptslave, TIOCSCTTY, 1));

        ensure(dup2(ptslave, STDIN_FILENO));
        ensure(close(ptslave));
        ensure(dup2(STDIN_FILENO, STDOUT_FILENO));
        ensure(dup2(STDIN_FILENO, STDERR_FILENO));

        ensure(execvp(argv[1], &argv[1]));
        exit(1);
    }

    int uppid = start_upstream(ptmaster);
    int downpid = start_downstream(ptmaster);
    ensure(close(ptmaster));
    ensure(close(STDIN_FILENO));
    ensure(close(STDOUT_FILENO));

    for (int i = 0; i < sizeof(siglist) / sizeof(siglist[0]); ++i) {
        ensure(sigaction(siglist[i], &forward, NULL));
    }

    int status;
    int exit_code;
    while(1) {
        int rc = waitpid(-1, &status, 0);
        IF_err(rc) {
            if(errno == ECHILD) break;
        } else if(rc == cpid) {
            cpid = 0;
            kill(uppid, SIGKILL);
            kill(downpid, SIGKILL);
            exit_code = WEXITSTATUS(status);
        } else if(rc == uppid || rc == downpid) {
            kill(uppid, SIGKILL);
            kill(downpid, SIGKILL);
        }
    }

    return exit_code;
}

static void forward_handler(int sig) {
    if(cpid != 0) kill(cpid, sig);
}

static int start_upstream(int ptmaster) {
    int ret = ensure(fork());
    if(ret == 0) {
        ensure(close(STDOUT_FILENO));
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(STDIN_FILENO, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(ptmaster, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        exit(0);
    }
    return ret;
}

static int start_downstream(int ptmaster) {
    int ret = ensure(fork());
    if(ret == 0) {
        ensure(close(STDIN_FILENO));
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = read(ptmaster, bufptr, sizeof(buf));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(STDOUT_FILENO, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        exit(0);
    }
    return ret;
}
