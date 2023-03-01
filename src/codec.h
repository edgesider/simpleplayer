#ifndef _CODEC_H_
#define _CODEC_H_

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>

#include "queue.h"

typedef struct {
    enum AVMediaType media_type;
    Queue pkt_queue, frame_queue;
    AVCodecContext *cc;
    AVStream *stream;
    // 当前播放位置
    int64_t play_time;
} StreamContext;

typedef struct {
    AVFormatContext *fc;
    StreamContext *video_sc;
    StreamContext *audio_sc;
} PlayContext;

void *demux_thread(PlayContext *ctx);
void *decode_audio_thread(PlayContext *pc);
void *decode_video_thread(PlayContext *pc);

static int64_t pts_to_microseconds(const StreamContext *sc, int64_t pts) {
    AVRational time_base = sc->stream->time_base;
    return pts * time_base.num * 1000 * 1000 / time_base.den;
}

#endif
