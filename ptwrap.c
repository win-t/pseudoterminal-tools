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

    ptmaster = ensure(posix_openpt(O_RDWR));
    ensure(grantpt(ptmaster));
    ensure(unlockpt(ptmaster));

    int cpid = ensure(fork());
    if(cpid == 0) {
        ensure(setsid());

        int ptslave = ensure(open(ensure_p(ptsname(ptmaster)), O_RDWR));
        ensure(close(ptmaster));

        ensure(ioctl(ptslave, TIOCSCTTY, 0));

        ensure(dup2(ptslave, STDIN_FILENO));
        ensure(close(ptslave));
        ensure(dup2(STDIN_FILENO, STDOUT_FILENO));
        ensure(dup2(STDIN_FILENO, STDERR_FILENO));

        char *new_arg[argc];
        for(int i = 1; i < argc; ++i) new_arg[i - 1] = argv[i];
        new_arg[argc - 1] = 0;

        ensure(execvp(new_arg[0], new_arg));
        exit(1);
    }

    int uppid = start_upstream(ptmaster);
    int downpid = start_downstream(ptmaster);
    ensure(close(ptmaster));

    struct sigaction forward;
    memset(&forward, 0, sizeof(forward));
    forward.sa_handler = forward_handler;
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
            if(uppid != 0) kill(uppid, SIGKILL);
            if(downpid != 0) kill(downpid, SIGKILL);
            exit_code = WEXITSTATUS(status);
        } else if(rc == uppid) {
            uppid = 0;
            if(downpid != 0) kill(downpid, SIGKILL);
        } else if(rc == downpid) {
            downpid = 0;
            if(uppid != 0) kill(uppid, SIGKILL);
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
