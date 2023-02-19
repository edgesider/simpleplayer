#include "utils.h"

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
