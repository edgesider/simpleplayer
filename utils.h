#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>

#define LOG_CODEC 1
#define LOG_RENDER 0
#define LOG_AUDIO 0

#define assert(EXPR) \
    do { \
        if (!(EXPR)) { \
            dprintf(2, "assert failed [%s:%d]: %s\n", __FILE_NAME__, __LINE__, #EXPR); \
            exit(-1); \
        } \
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


__attribute__((noreturn)) void averror(int code, const char *msg);
__attribute__((noreturn)) void error(const char *msg);

typedef void (*FrameProcesser)(const AVFrame *frame);

#endif /* ifndef _UTILS_H_ */
