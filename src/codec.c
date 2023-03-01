// 解封装、解码线程

// 解封装
//   解码线程需要新数据的时候，解封装线程开始解封装；
//   解码线程会缓存一系列的包，这个缓冲区里面的包会被渲染线程消费；
//   当缓冲区中的有效包小于某个数量之后（饥饿状态，饥饿阈值），解封装线程开始解封装；
//   如果音频解码需要新包，但是视频解码不需要，在解新包的过程中解出来了视频包，这些视频包怎么处理？
//      直接送入视频解码线程的缓冲区（缓冲区需要用链表实现，不限长度，与饥饿阈值不直接相关）

#include "codec.h"

#include <pthread.h>
#include <unistd.h>

#include "audio.h"
#include "utils.h"
#include "video.h"

#define PKT_QUEUE_SIZE 10
#define FRAME_QUEUE_SIZE 40

static int packet_can_queue(Queue *q) {
    if (q->length > PKT_QUEUE_SIZE) {
        logCodec("packet queue is full\n");
        /* logAudio("packet queue is full\n"); */
    }
    return q->length <= PKT_QUEUE_SIZE;
}

static int frame_can_queue(Queue *q) {
    if (q->length > FRAME_QUEUE_SIZE) {
        logCodec("frame queue is full\n");
        /* logAudio("frame queue is full\n"); */
    }
    return q->length <= FRAME_QUEUE_SIZE;
}

static PlayContext *get_play_context_for_packet(DemuxContext *ctx,
                                                const AVPacket *pkt) {
    if (ctx->v_ctx && pkt->stream_index == ctx->v_ctx->stream->index) {
        return ctx->v_ctx;
    }
    if (ctx->a_ctx && pkt->stream_index == ctx->a_ctx->stream->index) {
        return ctx->a_ctx;
    }
    return NULL;
}

static void draining(DemuxContext *ctx) {
    // draining mode
    logCodec("enter draining mode\n");
    if (ctx->v_ctx) {
        queue_enqueue_wait(&ctx->v_ctx->pkt_queue, NULL, packet_can_queue);
    }
    if (ctx->a_ctx) {
        queue_enqueue_wait(&ctx->a_ctx->pkt_queue, NULL, packet_can_queue);
    }
}

static void enqueue_packet(DemuxContext *ctx, AVPacket *pkt) {
    if (!pkt) {
        draining(ctx);
        return;
    }
    enum AVMediaType pkt_type =
        ctx->fc->streams[pkt->stream_index]->codecpar->codec_type;
    PlayContext *play_ctx = get_play_context_for_packet(ctx, pkt);
    if (!play_ctx) {
        logCodecE("no PlayContext available for pkt: stream=%d, type=%s",
                  pkt->stream_index, av_get_media_type_string(pkt_type));
        return;
    }
    if (play_ctx->media_type == AVMEDIA_TYPE_VIDEO) {
        queue_enqueue(&play_ctx->pkt_queue, pkt);
        logCodec("video packet queue size: %d\n", play_ctx->pkt_queue.length);
    } else {
        queue_enqueue_wait(&play_ctx->pkt_queue, pkt, packet_can_queue);
        logCodec("audio packet queue size: %d\n", play_ctx->pkt_queue.length);
    }
    logCodec("enqueued packet, type=%s\n",
             av_get_media_type_string(play_ctx->cc->codec_type));
}

void *demux_thread(DemuxContext *ctx) {
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

static void decode_packet(PlayContext *ctx, const AVPacket *pkt) {
    int ret;
    AVFrame *frame;
    AVCodecContext *cc = ctx->cc;

    // avcodec send_packet
    if ((ret = avcodec_send_packet(cc, pkt)) != 0) {
        averror(ret, "send packet");
    }

    int draining = pkt == NULL;

    // avcodec receive_frame
    for (int n_frame = 0;; n_frame++) {
        if ((frame = av_frame_alloc()) == NULL) {
            averror(AVERROR_UNKNOWN, "alloc frame");
        }

        ret = avcodec_receive_frame(cc, frame);
        int done = 0;
        if (ret == AVERROR(EAGAIN)) {
            if (draining) {
                error("not in draining mode but packet is null");
            }
            done = 1;
        } else if (ret == AVERROR_EOF && draining) {
            done = 1;
        } else if (ret != 0) {
            averror(ret, "receive frame");
        }

        if (frame->format >= 0) {
            /* processFrame(frame, cc->codec_type); */
            // @see 关于time_base https://www.jianshu.com/p/bf323cee3b8e
            logCodec("parsed new frame[%d]: type=%s, pts=%ld\n", n_frame,
                     av_get_media_type_string(cc->codec_type), frame->pts);
            if (cc->codec_type == AVMEDIA_TYPE_VIDEO) {
                logCodec("video frame queue size: %d\n", ctx->frame_queue.length);
                queue_enqueue(&ctx->frame_queue, frame);
                /* queue_enqueue_wait(&ctx->frame_queue, frame, frame_can_queue); */
            } else {
                logCodec("audio frame queue size: %d\n", ctx->frame_queue.length);
                queue_enqueue_wait(&ctx->frame_queue, frame, frame_can_queue);
            }
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

void *decode_thread(PlayContext *ctx) {
    AVPacket *pkt;
    Queue *q;
    const char *media_type_str;

    media_type_str = av_get_media_type_string(ctx->media_type);
    q = &ctx->pkt_queue;

    for (;;) {
        pkt = queue_dequeue_wait(q, queue_has_data);
        if (!pkt) {
            // 收到空packet之后，进入draining模式，让解码器输出缓存的帧
            logCodec("[%s-decode] got null packet\n", media_type_str);
        } else {
            logCodec("[%s-decode] got new packet: pts=%ld\n", media_type_str,
                     pkt->pts);
        }

        decode_packet(ctx, pkt);
        if (!pkt) {
            break;
        } else {
            av_packet_free(&pkt);
        }
    }
    logCodec("[%s-decode] finished\n", media_type_str);
    return NULL;
}

int64_t pts_to_microseconds(const PlayContext *ctx, int64_t pts) {
    AVRational time_base = ctx->stream->time_base;
    return pts * time_base.num * 1000 * 1000 / time_base.den;
}
