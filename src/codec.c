#include "codec.h"

#include <pthread.h>
#include <unistd.h>

#include "audio.h"
#include "config.h"
#include "utils.h"
#include "video.h"

static int packet_can_queue(Queue *q) {
    if (q->length >= PKT_QUEUE_SIZE) {
        logCodec("packet queue is full\n");
    }
    return q->length < PKT_QUEUE_SIZE;
}

static int frame_can_queue(Queue *q) {
    if (q->length >= FRAME_QUEUE_SIZE) {
        logCodec("frame queue is full\n");
    }
    return q->length < FRAME_QUEUE_SIZE;
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
    queue_enqueue_wait(&play_ctx->pkt_queue, pkt, packet_can_queue);
    logCodec("enqueued packet: type=%s, queue_size=%d\n",
             av_get_media_type_string(play_ctx->cc->codec_type),
             play_ctx->pkt_queue.length);
}

void *demux_thread(PlayContext *ctx) {
    int ret;
    AVPacket *pkt;

    // 编解码的api参考 https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html
    int pkt_eof = 0;
    for (; !pkt_eof;) {
        // avcodec 分配一个packet
        // packet中包含一个或多个有效帧
        if ((pkt = av_packet_alloc()) == NULL) {
            averror(AVERROR_UNKNOWN, "alloc packet");
        }

        // avformat 读取一个packet，其中至少有一个完整的frame
        // @see
        // http://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#details
        ret = av_read_frame(ctx->fc, pkt);
        if (ret) {
            if (ret == AVERROR_EOF) {
                pkt_eof = 1;
            } else {
                averror(ret, "read packet");
            }
        }
        if (!pkt_eof) {
            enqueue_packet(ctx, pkt);
        } else {
            enqueue_packet(ctx, NULL);
            av_packet_free(&pkt);
        }
    }
    return NULL;
}

static void decode_packet(StreamContext *ctx, const AVPacket *pkt) {
    int ret;
    AVFrame *frame;
    AVCodecContext *cc = ctx->cc;
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
            queue_enqueue_wait(&ctx->frame_queue, frame, frame_can_queue);
            logCodec("enqueued new frame: pts=%ld, type=%s, queue_size=%d\n",
                     frame->pts, av_get_media_type_string(cc->codec_type),
                     ctx->frame_queue.length);
        }

        if (done) {
            // 当前包解析完毕
            if (draining) {
                queue_enqueue_wait(&ctx->frame_queue, NULL, frame_can_queue);
            }
            break;
        }
    }
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
        pkt = queue_dequeue_wait(q, queue_has_data);
        if (!pkt) {
            // 收到空packet之后，进入draining模式，让解码器输出缓存的帧
            logCodec("[%s-decode] got null packet\n", media_type_str);
        } else {
            logCodec("[%s-decode] got new packet: pts=%ld\n", media_type_str,
                     pkt->pts);
        }

        decode_packet(sc, pkt);
        if (!pkt) {
            break;
        } else {
            av_packet_free(&pkt);
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
