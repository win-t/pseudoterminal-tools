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

static volatile int closing = 0;
static void cleanup_handler(int sig);
static int start_upstream(int up, int down);
static int start_downstream(int up, int down);

int main(int argc, char *argv[]) {
    if(argc < 3) fail_err(EINVAL);

    int up = ensure(open(argv[1], O_WRONLY));
    int down = ensure(open(argv[2], O_RDONLY));

    struct termios oldterm, newterm;
    struct sigaction cleanup;

    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.sa_handler = cleanup_handler;

    ensure(sigaction(SIGTERM, &cleanup, NULL));
    ensure(sigaction(SIGINT, &cleanup, NULL));

    if(!isatty(0)) fail_err(EINVAL);

    ensure(tcgetattr(0, &oldterm));
    newterm = oldterm;
    cfmakeraw(&newterm);
    ensure(tcsetattr(0, TCSADRAIN, &newterm));

    int uppid = start_upstream(up, down);
    int downpid = start_downstream(up, down);

    while(1) {
        int tmp;
        int rc = waitpid(-1, &tmp, 0);
        IF_err(rc) {
            if(errno == ECHILD) break;
            else if (errno == EINTR && closing) {
                kill(uppid, SIGKILL);
                kill(downpid, SIGKILL);
            } else {
                kill(uppid, SIGKILL);
                waitpid(-1, NULL, WNOHANG);
                kill(downpid, SIGKILL);
                waitpid(-1, NULL, WNOHANG);
                fail_eno();
            }
        } else {
            if (WEXITSTATUS(tmp) != 0) {
                if (!closing) {
                    if(rc == uppid) uppid = start_upstream(up, down);
                    else if(rc == downpid) downpid = start_downstream(up, down);
                }
            } else {
                closing = 1;
                if(rc == uppid) kill(downpid, SIGKILL);
                else if(rc == downpid) kill(uppid, SIGKILL);
            }
        }
    }

    ensure(tcsetattr(0, TCSADRAIN, &oldterm));

    return 0;
}

static void cleanup_handler(int sig) {
    closing = 1;
}

static int start_upstream(int up, int down) {
    int ret = ensure(fork());
    if(ret == 0) {
        ensure(close(down));;
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
                int rc = ensure(write(1, bufptr, size));
                bufptr += rc; size -= rc;
            }
        }
        _exit(0);
    }
    return ret;
}
