#ifndef _CODEC_H_
#define _CODEC_H_

#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>

#include "event.h"
#include "queue.h"

enum PlayState {
    STATE_PLAYING,
    STATE_PAUSE,
    STATE_PLAY_SEEKING,
    STATE_PAUSE_SEEKING,
};

typedef struct {
    enum AVMediaType media_type;
    Queue pkt_queue, frame_queue;
    AVCodecContext *cc;
    AVStream *stream;
    /** 当前播放位置，单位：微秒 */
    int64_t play_time;
    /**
     * 播放线程事件队列，音视频播放线程消费
     *
     * TODO 是否需要把帧队列也合进来
     * TODO 消费线程可以指定需要接收哪些事件
     */
    Queue play_event_queue;
    /**
     * 解码线程事件队列，解码线程消费
     */
    Queue decode_event_queue;
} StreamContext;

typedef struct {
    AVFormatContext *fc;
    StreamContext *video_sc;
    StreamContext *audio_sc;
    enum PlayState state;
    /**
     * 解封装线程事件队列，解封装线程消费
     */
    Queue demux_event_queue;
} PlayContext;

void *demux_thread(PlayContext *ctx);
void *decode_audio_thread(PlayContext *pc);
void *decode_video_thread(PlayContext *pc);

int play_pause(PlayContext *pc);
int play_resume(PlayContext *pc);
int play_toggle(PlayContext *pc);
int play_seek(PlayContext *pc, int64_t to_microseconds);

static int64_t pts_to_microseconds(const StreamContext *sc, int64_t pts) {
    AVRational time_base =
        sc->stream->time_base;  // AVStream.time_base的单位是秒
    return pts * time_base.num * 1000 * 1000 / time_base.den;
}

static int64_t microseconds_to_pts(const StreamContext *sc,
                                   int64_t microseconds) {
    AVRational time_base =
        sc->stream->time_base;  // AVStream.time_base的单位是秒
    return microseconds / (time_base.num * 1000 * 1000 / time_base.den);
}

static void dump_queue_info(const PlayContext *pc) {
    logCodec("[queue-info] v_pkt=%d, v_frame=%d, a_pkt=%d, a_frame=%d\n",
             pc->video_sc ? pc->video_sc->pkt_queue.length : -1,
             pc->video_sc ? pc->video_sc->frame_queue.length : -1,
             pc->audio_sc ? pc->audio_sc->pkt_queue.length : -1,
             pc->audio_sc ? pc->audio_sc->frame_queue.length : -1);
}

#endif
