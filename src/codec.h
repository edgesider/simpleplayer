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
} PlayContext;

typedef struct {
    AVFormatContext *fc;
    PlayContext *v_ctx;
    PlayContext *a_ctx;
} DemuxContext;

void *demux_thread(DemuxContext *ctx);
void *decode_thread(PlayContext *params);

int64_t pts_to_microseconds(const PlayContext *ctx, int64_t pts);

#endif
