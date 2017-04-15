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
#include <stdlib.h>

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

static volatile int uppid = 0;
static volatile int downpid = 0;
static struct termios oldterm;

static void close_handler(int sig);
static int start_upstream(int up);
static int start_downstream(int down);
static void restore_term();

int main(int argc, char *argv[]) {
    if(argc < 3) fail_err(EINVAL);

    int up = ensure(open(argv[1], O_WRONLY));
    int down = ensure(open(argv[2], O_RDONLY));

    struct termios newterm;
    if(isatty(STDIN_FILENO)) {
        ensure(tcgetattr(STDIN_FILENO, &oldterm));
        newterm = oldterm;
        cfmakeraw(&newterm);
        ensure(tcsetattr(STDIN_FILENO, TCSADRAIN, &newterm));
    }

    uppid = start_upstream(up);
    downpid = start_downstream(down);
    ensure(close(up));
    ensure(close(down));

    if(atexit(restore_term) != 0) {
        kill(uppid, SIGKILL);
        kill(downpid, SIGKILL);
        fail_err(EBADSLT);
    }

    struct sigaction close_act;
    memset(&close_act, 0, sizeof(close_act));
    close_act.sa_handler = close_handler;
    for (int i = 0; i < sizeof(siglist) / sizeof(siglist[0]); ++i) {
        ensure(sigaction(siglist[i], &close_act, NULL));
    }

    while(1) {
        int rc = waitpid(-1, NULL, 0);
        IF_err(rc) {
            if(errno == ECHILD) break;
        } else if(rc == uppid) {
            uppid = 0;
            if(downpid != 0) kill(downpid, SIGKILL);
        } else if(rc == downpid) {
            downpid = 0;
            if(uppid != 0) kill(uppid, SIGKILL);
        }
    }

    return 0;
}

static void close_handler(int sig) {
    if(uppid != 0) kill(uppid, SIGKILL);
    if(downpid != 0) kill(downpid, SIGKILL);
}

static void restore_term() {
    if(isatty(STDIN_FILENO)) ensure(tcsetattr(STDIN_FILENO, TCSADRAIN, &oldterm));
}

static int start_upstream(int up) {
    int ret = ensure(fork());
    if(ret == 0) {
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

static int start_downstream(int down) {
    int ret = ensure(fork());
    if(ret == 0) {
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
