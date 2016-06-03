#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define FAIL_WITH_PPID
#include "fail.h"

static volatile int sizechanged = 1;
static void sizechange_handler(int sig) { sizechanged = 1; }

static volatile int closing = 0;
static void close_handler(int sig) { closing = 1; }

int main(int argc, char *argv[]) {
    struct sigaction tmp;
    memset(&tmp, 0, sizeof(tmp));
    if(!isatty(0)) fail_err(EINVAL);

    tmp.sa_handler = sizechange_handler;
    ensure(sigaction(SIGWINCH, &tmp, NULL));

    tmp.sa_handler = close_handler;
    ensure(sigaction(SIGINT, &tmp, NULL));
    ensure(sigaction(SIGTERM, &tmp, NULL));

    while(1) {
        if(sizechanged) {
            struct winsize sz;
            ensure(ioctl(0, TIOCGWINSZ, &sz));
            printf("row=%d,col=%d\n", sz.ws_row, sz.ws_col);
            sizechanged = 0;
        }
        pause();
        if(closing) break;
    }

}
