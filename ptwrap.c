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

static volatile int last_signal = 0;
static void forward_handler(int sig);
static int start_upstream(int ptmaster);
static int start_downstream(int ptmaster);

int main(int argc, char *argv[]) {
    if(argc < 2) fail_err(EINVAL);

    int ptmaster;
    struct sigaction forward;

    memset(&forward, 0, sizeof(forward));
    forward.sa_handler = forward_handler;

    ensure(sigaction(SIGTERM, &forward, NULL));
    ensure(sigaction(SIGINT, &forward, NULL));

    ptmaster = ensure(posix_openpt(O_RDWR));
    ensure(grantpt(ptmaster));
    ensure(unlockpt(ptmaster));

    int cpid = ensure(fork());
    if(cpid == 0) {
        ensure(setsid());

        int ptslave = ensure(open(ensure_p(ptsname(ptmaster)), O_RDWR));
        ensure(close(ptmaster));

        ensure(dup2(ptslave, STDIN_FILENO));
        ensure(close(ptslave));
        ensure(dup2(STDIN_FILENO, STDOUT_FILENO));
        ensure(dup2(STDIN_FILENO, STDERR_FILENO));

        ensure(execvp(argv[1], &argv[1]));
        exit(1);
    }

    int uppid = start_upstream(ptmaster);
    int downpid = start_downstream(ptmaster);

    int status;
    int closing = 0;
    while(1) {
        int tmp;
        int rc;

        if(last_signal != 0) rc = EINTR;
        else rc = waitpid(-1, &tmp, 0);

        IF_err(rc) {
            if(errno == ECHILD) break;
            else if(errno == EINTR && last_signal != 0) {
                int sig_to_deliver = last_signal;
                last_signal = 0;
                kill(cpid, sig_to_deliver);
            } else {
                int tmperrno = errno;
                kill(uppid, SIGKILL);
                kill(downpid, SIGKILL);
                kill(cpid, SIGKILL);
                fail_err(tmperrno);
            }
        } else if(rc == cpid) {
            closing = 1;
            kill(uppid, SIGKILL);
            ensure(close(ptmaster));
            status = WEXITSTATUS(tmp);
        } else if (!closing && (!WIFEXITED(tmp) || WEXITSTATUS(tmp) != 0)) {
            sleep(1);
            if(rc == uppid ) uppid = start_upstream(ptmaster);
            else if(rc == downpid) downpid = start_downstream(ptmaster);
        }
    }

    return status;
}

static void forward_handler(int sig) {
    if(last_signal == 0) last_signal = sig;
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
            int rc = read(ptmaster, bufptr, sizeof(buf));
            IF_err(rc) {
                // master will return EIO if none open the slave
                // https://github.com/torvalds/linux/blob/v4.2/drivers/tty/n_tty.c#L2268-L2274
                if(errno == EIO) break;
                else fail_eno();
            } else {
                int size = rc;
                if(size == 0) break;
                while(size > 0) {
                    int rc = ensure(write(STDOUT_FILENO, bufptr, size));
                    bufptr += rc; size -= rc;
                }
            }
        }
        exit(0);
    }
    return ret;
}
