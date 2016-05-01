#ifndef _FAIL_H_
#define _FAIL_H_

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

#define R_err(what)       { errno = what; return -1; }
#define R_err_p(what)     { errno = what; return NULL; }
#define R_eno             return -1
#define R_eno_p           return NULL
#define R_ok              return 0
#define R_ok_p(what)      { return what; }

#define IF_err(what)      if(what < 0)
#define IF_err_p(what)    if(what == NULL)

#define p_eno()           _p(__LINE__, "E", NULL)
#define p_err(eno)        _p_err(__LINE__, eno)
#define p_log(...)        _p(__LINE__, "I", ##__VA_ARGS__)
#ifdef DEBUG
#define p_debug(...)      _p(__LINE__, "D", ##__VA_ARGS__)
#else
#define p_debug(...)
#endif

#define fail_eno()        _fail_eno(__LINE__)
#define fail_err(what)    _fail_err(__LINE__,  what)

#define ensure(what)      _ensure(__LINE__,  what)
#define ensure_p(what)    _ensure_p(__LINE__,  what)


static FILE *_fail_output;
#define set_error_output(to) _fail_output = to

static void _p(int line, const char *tag, const char *format, ...) {
    char msg[512];
    if(_fail_output == NULL) _fail_output = stderr;
    struct timespec tm;
    clock_gettime(CLOCK_MONOTONIC, &tm);
    if(format == NULL) {
        snprintf(msg, sizeof(msg), strerror(errno));
    } else {
        va_list arglist;
        va_start(arglist, format);
        vsnprintf(msg, sizeof(msg), format, arglist);
        va_end(arglist);
    }
#ifdef FAIL_WITH_PPID
    fprintf(_fail_output, "%08d.%09d:%04d:%d,%d:%s:%s\n", (int) tm.tv_sec, (int) tm.tv_nsec, line, getpid(), getppid(), tag, msg);
#else
    fprintf(_fail_output, "%08d.%09d:%04d:%d:%s:%s\n", (int) tm.tv_sec, (int) tm.tv_nsec, line, getpid(), tag, msg);
#endif
}

static inline void _p_err(int line, int err) {
    errno = err;
    _p(line, "E", NULL);
}

static void _fail_eno(int line) {
    _p(line, "E", NULL);
    _exit(1);
}

static inline void _fail_err(int line, int err) {
    errno = err;
    _fail_eno(line);
}

static inline int _ensure(int line, int rc) {
    IF_err(rc) _fail_eno(line);
    return rc;
}

static inline void *_ensure_p(int line, void *p) {
    IF_err_p(p) _fail_eno(line);
    return p;
}

#endif
