#include "utils.h"

#include <sys/time.h>

int64_t get_time_millisec() {
    struct timeval t;
    if (gettimeofday(&t, NULL) != 0) {
        error("gettimeofday");
    }
    return t.tv_sec * 1000 + t.tv_usec / 1000;
}

__attribute__((noreturn)) void averror(int code, const char *msg) {
    dprintf(2, "%s: [%d] %s\n", msg, code, av_err2str(code));
    exit(-1);
    __builtin_unreachable();
}

__attribute__((noreturn)) void error(const char *msg) {
    dprintf(2, "%s\n", msg);
    exit(-1);
    __builtin_unreachable();
}
