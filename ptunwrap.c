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

static int siglist[] = {
    SIGHUP,
    SIGINT,
    SIGQUIT,
    SIGTERM,
};

static volatile int closing = 0;
static void close_handler(int sig);
static int start_upstream(int up, int down);
static int start_downstream(int up, int down);

int main(int argc, char *argv[]) {
    if(argc < 3) fail_err(EINVAL);

    int up = ensure(open(argv[1], O_WRONLY));
    int down = ensure(open(argv[2], O_RDONLY));

    struct sigaction close;
    memset(&close, 0, sizeof(close));
    close.sa_handler = close_handler;

    struct termios oldterm, newterm;

    if(isatty(STDIN_FILENO)) {
        ensure(tcgetattr(STDIN_FILENO, &oldterm));
        newterm = oldterm;
        cfmakeraw(&newterm);
        ensure(tcsetattr(STDIN_FILENO, TCSADRAIN, &newterm));
    }

    int uppid = start_upstream(up, down);
    int downpid = start_downstream(up, down);

    for (int i = 0; i < sizeof(siglist) / sizeof(siglist[0]); ++i) {
        ensure(sigaction(siglist[i], &close, NULL));
    }

    while(1) {
        int rc = waitpid(-1, NULL, 0);
        IF_err(rc) {
            if(errno == ECHILD) break;
            else if (errno == EINTR && closing) {
                kill(uppid, SIGKILL);
                kill(downpid, SIGKILL);
            }
        } else if(rc == uppid || rc == downpid) {
            kill(uppid, SIGKILL);
            kill(downpid, SIGKILL);
        }
    }

    if(isatty(STDIN_FILENO)) ensure(tcsetattr(STDIN_FILENO, TCSADRAIN, &oldterm));

    return 0;
}

static void close_handler(int sig) {
    closing = 1;
}

static int start_upstream(int up, int down) {
    int ret = ensure(fork());
    if(ret == 0) {
        ensure(close(down));;
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(STDIN_FILENO, bufptr, sizeof(buf)));
            if(size == 0) break;
            while(size > 0) {
                int rc = ensure(write(up, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        exit(0);
    }
    return ret;
}

static int start_downstream(int up, int down) {
    int ret = ensure(fork());
    if(ret == 0) {
        ensure(close(up));
        char buf[LOCAL_BUF_SIZE];
        while(1) {
            char *bufptr = buf;
            int size = ensure(read(down, bufptr, sizeof(buf)));
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
