#ifndef _UTILS_H_
#define _UTILS_H_

#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_CODEC 1
#define LOG_RENDER 1
#define LOG_AUDIO 1

#define assert(EXPR)                                                           \
    do {                                                                       \
        if (!(EXPR)) {                                                         \
            dprintf(2, "assert failed [%s:%d]: %s\n", __FILE_NAME__, __LINE__, \
                    #EXPR);                                                    \
            exit(-1);                                                          \
        }                                                                      \
    } while (0)

#define logCodec(fmt, ...)                  \
    do {                                    \
        if (LOG_CODEC)                      \
            dprintf(2, fmt, ##__VA_ARGS__); \
    } while (0)

#define logCodecE(fmt, ...)             \
    do {                                \
        dprintf(2, fmt, ##__VA_ARGS__); \
    } while (0)

#define logRender(fmt, ...)                 \
    do {                                    \
        if (LOG_RENDER)                     \
            dprintf(2, fmt, ##__VA_ARGS__); \
    } while (0)

#define logRenderE(fmt, ...)            \
    do {                                \
        dprintf(2, fmt, ##__VA_ARGS__); \
    } while (0)

#define logAudio(fmt, ...)                  \
    do {                                    \
        if (LOG_AUDIO)                      \
            dprintf(2, fmt, ##__VA_ARGS__); \
    } while (0)

#define logAudioE(fmt, ...)             \
    do {                                \
        dprintf(2, fmt, ##__VA_ARGS__); \
    } while (0)

#define TimeIt(name, proc)                                  \
    do {                                                    \
        int64_t before = get_time_millisec();               \
        proc int64_t after = get_time_millisec();           \
        logRender("%s cost %ldms\n", name, after - before); \
    } while (0)

__attribute__((noreturn)) void averror(int code, const char *msg);
__attribute__((noreturn)) void error(const char *msg);

int64_t get_time_millisec();

#endif /* ifndef _UTILS_H_ */
