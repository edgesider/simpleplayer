// clang-format off
#include <glad/glad.h>
// clang-format on

#include "play_helper.h"
#include "video.h"

#include <GLFW/glfw3.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <pthread.h>

#include "config.h"
#include "event.h"
#include "render.h"
#include "utils.h"

// TODO 用OpenGL实现 / OpenGL渲染YUV
static AVFrame *convert_frame_to_rgb24(const AVFrame *frame) {
    int ret;
    if (frame->format == AV_PIX_FMT_RGB24) {
        // 复制新的AVFrame，同时共享Buffer
        AVFrame *newFrame = av_frame_alloc();
        av_frame_ref(newFrame, frame);
        return newFrame;
    }
    logRender("frame is in %s format, not rgb24, converting...\n",
              av_get_pix_fmt_name(frame->format));
    struct SwsContext *sc = sws_getContext(
        frame->width, frame->height, frame->format, frame->width, frame->height,
        AV_PIX_FMT_RGB24, SWS_SPLINE, NULL, NULL, NULL);
    if (sc == NULL) {
        logCodecE("convert failed\n");
        exit(-1);
    }

    AVFrame *outFrame = av_frame_alloc();
    av_frame_copy_props(outFrame, frame);
    outFrame->width = frame->width;
    outFrame->height = frame->height;
    outFrame->format = AV_PIX_FMT_RGB24;
    if ((ret = av_frame_get_buffer(outFrame, 1)) != 0) {
        averror(ret, "av_frame_get_buffer");
    }

    sws_scale(sc, (const uint8_t *const *) frame->data, frame->linesize, 0,
              frame->height, outFrame->data, outFrame->linesize);

    sws_freeContext(sc);
    return outFrame;
}

static void update(StreamContext *ctx, const AVFrame *frame) {
    commit_frame(convert_frame_to_rgb24(frame));

    int64_t frameTime =
        av_rescale_q(1000 * 1000, (AVRational){1, 1}, ctx->cc->framerate);
    av_usleep(frameTime);
}

void *video_play_thread(PlayContext *pc) {
    AVFrame *frame;
    Queue *q;
    StreamContext *sc = pc->video_sc;
    StreamContext *audio_sc = pc->audio_sc;

    logRender("[video-play] tid=%lu\n", pthread_self());
    q = &sc->frame_queue;

    for (;;) {
        frame = queue_dequeue_wait(q, queue_has_data);
        if (!frame) {
            logRender("[video-play] EOS\n");
            break;
        }
        sc->play_time = pts_to_microseconds(sc, frame->pts);
        logRender("[video-play] time updated: curr_time=%f\n",
                  sc->play_time / 1000.0 / 1000);

        if (audio_sc) {
            int64_t diff =
                pts_to_microseconds(sc, frame->pts) - pc->audio_sc->play_time;
            /* logRender("[video-play] diff=%ld\n", diff); */
            if (diff <= -MAX_DIFF) {
                logRender("[video-play] syncing, skipping frame, diff=%ld\n",
                          diff);
            } else if (diff >= MAX_DIFF) {
                logRender(
                    "[video-play] syncing, waiting for frame render, "
                    "diff=%ld\n",
                    diff);
                av_usleep(diff);
                update(sc, frame);
            } else {
                update(sc, frame);
            }
        } else {
            update(sc, frame);
        }
        av_frame_free(&frame);

        process_play_events(sc, NULL, NULL);
    }
    logRender("[video-play] finished\n");

    return NULL;
}
