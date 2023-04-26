#include "codec.h"

#include <pthread.h>
#include <unistd.h>

#include "audio.h"
#include "config.h"
#include "event_helper.h"
#include "utils.h"
#include "video.h"

static void dispatch_all_event(PlayContext *pc, Event *event);
static void dispatch_demux_event(PlayContext *pc, Event *event);
static void dispatch_decode_event(StreamContext *sc, Event *event);
static void dispatch_decode_event_all(PlayContext *pc, Event *event);
static void dispatch_play_event(StreamContext *sc, Event *event);
static void dispatch_play_event_all(PlayContext *pc, Event *event);
static void on_seek_end(PlayContext *pc);

static int packet_can_queue(Queue *q) {
    if (q->length >= PKT_QUEUE_SIZE) {
        /* logCodec("packet queue is full\n"); */
    }
    return q->length < PKT_QUEUE_SIZE;
}

static int frame_can_queue(Queue *q) {
    if (q->length >= FRAME_QUEUE_SIZE) {
        /* logCodec("frame queue is full\n"); */
    }
    return q->length < FRAME_QUEUE_SIZE;
}

static void free_frame(AVFrame *frame) {
    av_frame_free(&frame);
}

static void free_packet(AVPacket *pkt) {
    av_packet_free(&pkt);
}

static void do_seek(AVFormatContext *fc, StreamContext *sc,
                    int64_t to_microseconds) {
    if (sc) {
        av_seek_frame(fc, sc->stream->index,
                      microseconds_to_pts(sc, to_microseconds), 0);
        sc->play_time = to_microseconds;
        avcodec_flush_buffers(sc->cc);
        queue_clear(&sc->pkt_queue, (DataCleaner) free_packet);
    }
}

static void process_demux_event(PlayContext *pc) {
    /* logCodec("[event-demux] process demux event\n"); */
    for (int i = 0; i < MAX_EVENTS_PER_LOOP; i++) {
        Event *event = queue_dequeue(&pc->demux_event_queue);
        if (!event) {
            break;
        }
        logRender("[event-demux] get type %d\n", event->type);
        switch (event->type) {
            case EVENT_SEEK_START: {
                SeekEvent *seek_start = (SeekEvent *) event;
                int64_t to_microseconds = seek_start->to_microseconds;

                // 先广播到解码和播放线程
                dispatch_decode_event_all(pc, event);
                dispatch_play_event_all(pc, event);

                do_seek(pc->fc, pc->audio_sc, to_microseconds);
                do_seek(pc->fc, pc->video_sc, to_microseconds);

                Event *seek_end =
                    event_alloc(EVENT_SEEK_END, sizeof(SeekEvent));
                ((SeekEvent *) seek_end)->to_microseconds = to_microseconds;
                dispatch_decode_event_all(pc, seek_end);
                event_unref(seek_end);
                on_seek_end(pc);
            } break;
            default:
                break;
        }
        event_unref(event);
    }
}

static StreamContext *get_stream_context_for_packet(PlayContext *ctx,
                                                    const AVPacket *pkt) {
    if (ctx->video_sc && pkt->stream_index == ctx->video_sc->stream->index) {
        return ctx->video_sc;
    }
    if (ctx->audio_sc && pkt->stream_index == ctx->audio_sc->stream->index) {
        return ctx->audio_sc;
    }
    return NULL;
}

static void draining(PlayContext *ctx) {
    // draining mode
    logCodec("enter draining mode\n");
    if (ctx->video_sc) {
        queue_enqueue_wait(&ctx->video_sc->pkt_queue, NULL, packet_can_queue);
    }
    if (ctx->audio_sc) {
        queue_enqueue_wait(&ctx->audio_sc->pkt_queue, NULL, packet_can_queue);
    }
}

static void enqueue_packet(PlayContext *ctx, AVPacket *pkt) {
    if (!pkt) {
        draining(ctx);
        return;
    }
    enum AVMediaType pkt_type =
        ctx->fc->streams[pkt->stream_index]->codecpar->codec_type;
    StreamContext *play_ctx = get_stream_context_for_packet(ctx, pkt);
    if (!play_ctx) {
        logCodecE("no PlayContext available for pkt: stream=%d, type=%s",
                  pkt->stream_index, av_get_media_type_string(pkt_type));
        return;
    }
    while (!queue_enqueue_timedwait(&play_ctx->pkt_queue, pkt, packet_can_queue,
                                    QUEUE_WAIT_MICROSECONDS)) {
        process_demux_event(ctx);
    }
    process_demux_event(ctx);
    logCodec("enqueued packet: type=%s, queue_size=%d\n",
             av_get_media_type_string(play_ctx->cc->codec_type),
             play_ctx->pkt_queue.length);
}

void *demux_thread(PlayContext *pc) {
    int ret;
    AVPacket *pkt;

    // 编解码的api参考 https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html
    int pkt_eof = 0;
    int i = 0;
    for (; !pkt_eof; i++) {
        // avcodec 分配一个packet
        // packet中包含一个或多个有效帧
        if ((pkt = av_packet_alloc()) == NULL) {
            averror(AVERROR_UNKNOWN, "alloc packet");
        }

        // avformat 读取一个packet，其中至少有一个完整的frame
        // @see
        // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#details
        ret = av_read_frame(pc->fc, pkt);
        if (ret) {
            if (ret == AVERROR_EOF) {
                pkt_eof = 1;
            } else {
                averror(ret, "read packet");
            }
        }
        if (!pkt_eof) {
            logCodec("[demux] enqueue packets %d\n", i);
            enqueue_packet(pc, pkt);
        } else {
            enqueue_packet(pc, NULL);
            av_packet_free(&pkt);
        }
    }
    return NULL;
}

static void process_decode_event(PlayContext *pc, StreamContext *sc) {
    /* logCodec("[event-decode] process decode event\n"); */
    for (int i = 0; i < MAX_EVENTS_PER_LOOP; i++) {
        Event *event = queue_dequeue(&sc->decode_event_queue);
        if (!event) {
            break;
        }
        logCodec("[event-decode] get type %d\n", event->type);
        switch (event->type) {
            case EVENT_SEEK_START: {
                logCodec("[event-decode] waiting for SEEK_END\n");
                queue_clear(&sc->frame_queue, (DataCleaner) free_frame);
                dump_queue_info(pc);
                Event *seek_end =
                    wait_for_event(&sc->decode_event_queue, EVENT_SEEK_END);
                dispatch_play_event(sc, seek_end);
                event_unref(seek_end);
            } break;
            default:
                break;
        }
        event_unref(event);
    }
}

static void decode_packet(PlayContext *pc, StreamContext *sc,
                          const AVPacket *pkt) {
    int ret;
    AVFrame *frame;
    AVCodecContext *cc = sc->cc;
    int draining = pkt == NULL;

    if ((ret = avcodec_send_packet(cc, pkt)) != 0) {
        averror(ret, "send packet");
    }

    for (int n_frame = 0;; n_frame++) {
        if ((frame = av_frame_alloc()) == NULL) {
            averror(AVERROR_UNKNOWN, "alloc frame");
        }

        ret = avcodec_receive_frame(cc, frame);
        int done = 0;
        if (ret == AVERROR(EAGAIN)) {
            if (draining) {
                error("not in draining mode but EAGAIN returned");
            }
            done = 1;
        } else if (ret == AVERROR_EOF && draining) {
            done = 1;
        } else if (ret != 0) {
            averror(ret, "receive frame");
        }

        if (frame->format >= 0) {
            // TODO 数据包和事件一起处理，保持事件模型的简单
            while (!queue_enqueue_timedwait(&sc->frame_queue, frame,
                                            frame_can_queue,
                                            QUEUE_WAIT_MICROSECONDS)) {
                // 这里有个问题，如果在这个process函数里面发生了seek，seek会将包队列清空，
                // 但是由于这是在enqueue的等待循环中，上面的while又会立即传入一个seek之前的帧，
                // 导致SEEKING状态和包不匹配，进而导致音画同步的时间判断有问题
                process_decode_event(pc, sc);
            }
            logCodec("enqueued new frame: pts=%ld, type=%s, queue_size=%d\n",
                     frame->pts, av_get_media_type_string(cc->codec_type),
                     sc->frame_queue.length);
        }

        if (done) {
            // 当前包解析完毕
            if (draining) {
                queue_enqueue_wait(&sc->frame_queue, NULL, frame_can_queue);
            }
            break;
        }
    }
    process_decode_event(pc, sc);
}

static void decode_thread(PlayContext *pc, enum AVMediaType to_decode) {
    AVPacket *pkt;
    Queue *q;
    const char *media_type_str;
    StreamContext *sc;

    if (to_decode == AVMEDIA_TYPE_VIDEO) {
        sc = pc->video_sc;
    } else if (to_decode == AVMEDIA_TYPE_AUDIO) {
        sc = pc->audio_sc;
    } else {
        error("unsupported media type to decode");
    }
    media_type_str = av_get_media_type_string(to_decode);
    q = &sc->pkt_queue;

    for (;;) {
        while (!queue_dequeue_timedwait(
            q, queue_has_data, QUEUE_WAIT_MICROSECONDS, (void **) &pkt)) {
            process_decode_event(pc, sc);
        }
        if (!pkt) {
            // 收到空packet之后，进入draining模式，让解码器输出缓存的帧
            logCodec("[%s-decode] got null packet\n", media_type_str);
        } else {
            logCodec("[%s-decode] got new packet: pts=%ld\n", media_type_str,
                     pkt->pts);
        }

        decode_packet(pc, sc, pkt);
        if (!pkt) {
            break;
        } else {
            free_packet(pkt);
        }
    }
    logCodec("[%s-decode] finished\n", media_type_str);
}

void *decode_video_thread(PlayContext *pc) {
    decode_thread(pc, AVMEDIA_TYPE_VIDEO);
    return NULL;
}

void *decode_audio_thread(PlayContext *pc) {
    decode_thread(pc, AVMEDIA_TYPE_AUDIO);
    return NULL;
}

static void put_event(Queue *q, Event *event) {
    event_ref(event);
    queue_enqueue(q, event);
}

static void dispatch_all_event(PlayContext *pc, Event *event) {
    logCodec("[event] dispatch_all_event: type=%d\n, video=%d, audio=%d",
             event->type, !!pc->video_sc, !!pc->audio_sc);
    put_event(&pc->demux_event_queue, event);
    if (pc->audio_sc) {
        dispatch_decode_event(pc->audio_sc, event);
        dispatch_play_event(pc->audio_sc, event);
    }
    if (pc->video_sc) {
        dispatch_decode_event(pc->video_sc, event);
        dispatch_play_event(pc->video_sc, event);
    }
}

static void dispatch_demux_event(PlayContext *pc, Event *event) {
    logCodec("[event] dispatch_demux_event: type=%d\n", event->type);
    put_event(&pc->demux_event_queue, event);
}

static void dispatch_decode_event(StreamContext *sc, Event *event) {
    logCodec("[event] dispatch_decode_event: type=%d\n", event->type);
    put_event(&sc->decode_event_queue, event);
}

static void dispatch_decode_event_all(PlayContext *pc, Event *event) {
    logCodec("[event] dispatch_decode_event_all: type=%d\n, video=%d, audio=%d",
             event->type, !!pc->video_sc, !!pc->audio_sc);
    if (pc->audio_sc) {
        dispatch_decode_event(pc->audio_sc, event);
    }
    if (pc->video_sc) {
        dispatch_decode_event(pc->video_sc, event);
    }
}

static void dispatch_play_event(StreamContext *sc, Event *event) {
    logCodec("[event] dispatch_play_event: type=%d\n", event->type);
    put_event(&sc->play_event_queue, event);
}

static void dispatch_play_event_all(PlayContext *pc, Event *event) {
    logCodec("[event] dispatch_play_event_all: type=%d\n, video=%d, audio=%d",
             event->type, !!pc->video_sc, !!pc->audio_sc);
    if (pc->audio_sc) {
        dispatch_play_event(pc->audio_sc, event);
    }
    if (pc->video_sc) {
        dispatch_play_event(pc->video_sc, event);
    }
}

int play_pause(PlayContext *pc) {
    if (pc->state == STATE_PLAYING) {
        pc->state = STATE_PAUSE;
        Event *ev = event_alloc_base(EVENT_PAUSE);
        dispatch_all_event(pc, ev);
        event_unref(ev);
        return 1;
    }
    return 0;
}

int play_resume(PlayContext *pc) {
    if (pc->state == STATE_PAUSE) {
        pc->state = STATE_PLAYING;
        Event *ev = event_alloc_base(EVENT_RESUME);
        dispatch_all_event(pc, ev);
        event_unref(ev);
        return 1;
    }
    return 0;
}

int play_toggle(PlayContext *pc) {
    if (pc->state == STATE_PLAYING) {
        return play_pause(pc);
    } else if (pc->state == STATE_PAUSE) {
        return play_resume(pc);
    }
    return 0;
}

static void on_seek_end(PlayContext *pc) {
    logCodec("[seek] on_seek_end\n");
    if (pc->state == STATE_PLAY_SEEKING) {
        pc->state = STATE_PLAYING;
    } else if (pc->state == STATE_PAUSE_SEEKING) {
        pc->state = STATE_PAUSE;
    } else {
        error("not seeking");
    }
    dump_queue_info(pc);
}

int play_seek(PlayContext *pc, int64_t to_microseconds) {
    logCodec("[demux] seek triggered\n");
    if (pc->state == STATE_PLAYING) {
        pc->state = STATE_PLAY_SEEKING;
    } else if (pc->state == STATE_PAUSE) {
        pc->state = STATE_PAUSE_SEEKING;
    } else {
        return 0;
    }
    Event *ev = event_alloc(EVENT_SEEK_START, sizeof(SeekEvent));
    ((SeekEvent *) ev)->to_microseconds = to_microseconds;
    dispatch_demux_event(pc, ev);
    event_unref(ev);
    return 1;
}
