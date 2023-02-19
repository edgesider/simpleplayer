#ifndef _CODEC_H_
#define _CODEC_H_

#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>

#include "queue.h"

typedef struct {
    enum AVMediaType media_type;
    Queue pkt_queue, frame_queue;
    AVCodecContext *cc;
    AVStream *stream;
} PlayContext;

typedef struct {
    AVFormatContext *fc;
    PlayContext *v_ctx;
    PlayContext *a_ctx;
} DemuxContext;

void *demux_thread(DemuxContext *ctx);
void *decode_thread(PlayContext *params);
void *video_play_thread(PlayContext *params);
void *audio_play_thread(PlayContext *params);

#endif
