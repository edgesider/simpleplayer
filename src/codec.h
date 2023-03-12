#ifndef _CODEC_H_
#define _CODEC_H_

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>

#include "event.h"
#include "queue.h"

typedef struct {
    enum AVMediaType media_type;
    Queue pkt_queue, frame_queue;
    AVCodecContext *cc;
    AVStream *stream;
    /** 当前播放位置，单位：微秒 */
    int64_t play_time;
    /**
     * 播放事件队列，音视频播放线程消费
     *
     * TODO 是否需要把帧队列也合进来
     * TODO 消费线程可以指定需要接收哪些事件
     */
    Queue play_event_queue;
} StreamContext;

typedef struct {
    AVFormatContext *fc;
    StreamContext *video_sc;
    StreamContext *audio_sc;
    enum PlayState state;
} PlayContext;

void *demux_thread(PlayContext *ctx);
void *decode_audio_thread(PlayContext *pc);
void *decode_video_thread(PlayContext *pc);

int play_pause(PlayContext *pc);
int play_resume(PlayContext *pc);
int play_toggle(PlayContext *pc);

static int64_t pts_to_microseconds(const StreamContext *sc, int64_t pts) {
    AVRational time_base =
        sc->stream->time_base;  // AVStream.time_base的单位是秒
    return pts * time_base.num * 1000 * 1000 / time_base.den;
}

#endif
