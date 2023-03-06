#include "audio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "utils.h"

// 音频播放相关
#define NB_AL_BUFFER 128
static ALCdevice *a_dev;
static ALCcontext *a_ctx;
static ALuint a_buf[NB_AL_BUFFER];
static ALuint a_src;

static void check_al_error(const char *msg) {
    ALuint error;
    if ((error = alGetError()) != AL_NO_ERROR) {
        logAudioE("%s: %d\n", msg, error);
        exit(-1);
    }
}

// 转为双声道、Signed 16-bits int
static AVFrame *convert_frame_to_stereo_s16(const AVFrame *frame) {
    int ret;
    SwrContext *ctx = NULL;
    AVChannelLayout cl = AV_CHANNEL_LAYOUT_STEREO;

    if (frame->format == AV_SAMPLE_FMT_S16 &&
        av_channel_layout_compare(&cl, &frame->ch_layout) == 0) {
        // 已经是正确的格式，复制新的AVFrame，同时共享Buffer
        AVFrame *newFrame = av_frame_alloc();
        av_frame_ref(newFrame, frame);
        return newFrame;
    }

    // TODO 共享Context
    swr_alloc_set_opts2(&ctx, &cl, AV_SAMPLE_FMT_S16, frame->sample_rate,
                        (AVChannelLayout *) &frame->ch_layout, frame->format,
                        frame->sample_rate, 0, NULL);
    if (swr_init(ctx) != 0) {
        logAudio("swr_init failed");
        exit(-1);
    }

    const int samples = frame->nb_samples;
    AVFrame *outFrame = av_frame_alloc();
    av_frame_copy_props(outFrame, frame);
    outFrame->format = AV_SAMPLE_FMT_S16;
    outFrame->nb_samples = samples;
    av_channel_layout_copy(&outFrame->ch_layout, &frame->ch_layout);
    if ((ret = av_frame_get_buffer(outFrame, 1)) != 0) {
        averror(ret, "av_frame_get_buffer");
    }

    swr_convert(ctx, outFrame->data, samples, (const uint8_t **) frame->data,
                samples);
    swr_free(&ctx);
    return outFrame;
}

static char *get_source_state_name(ALuint state) {
    switch (state) {
        case AL_INITIAL: return "initial";
        case AL_PLAYING: return "playing";
        case AL_PAUSED: return "paused";
        case AL_STOPPED: return "stopped";
    }
    return "unknown";
}

static Queue
    pts_queue;  // TODO 该队列可以同时用来维护AL队列的其他信息，如帧时长等

static void alloc_buffer_and_queue(StreamContext *sc, const AVFrame *frame) {
    ALuint buf;
    // TODO 复用Buffer
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_STEREO16, frame->data[0], frame->linesize[0],
                 frame->sample_rate);
    check_al_error("alBufferData");
    alSourceQueueBuffers(a_src, 1, &buf);
    check_al_error("alSourceQueueBuffers");
    queue_enqueue(&pts_queue, (void *) pts_to_microseconds(
                                  sc, frame->pts));  // TODO 32bit support
}

static int free_buffers(StreamContext *sc) {
    ALint processed;
    alGetSourcei(a_src, AL_BUFFERS_PROCESSED, &processed);
    if (processed > 0) {
        ALuint *buffers = malloc(sizeof(ALint) * processed);
        alSourceUnqueueBuffers(a_src, processed, buffers);
        alDeleteBuffers(processed, buffers);

        int64_t last_pts;
        for (int i = 0; i < processed; i++) {
            last_pts = (int64_t) queue_dequeue(&pts_queue);
        }
        sc->play_time = last_pts;
        logAudio("[audio-play] time updated: curr_time=%lf\n",
                 sc->play_time / (double) 1000.0 / 1000);
    }
    return processed;
}

// TODO 以时间为单位，而不是帧数量
#define MAX_QUEUED_FRAMES 50
#define IDLE_WAIT_FRAMES 2

/**
 * 不断更新播放时间直到AL播放完成
 */
void wait_remain_buffers(StreamContext *sc) {
    logAudio("[audio-play] EOS\n");
    ALint queued;
    for (;;) {
        free_buffers(sc);
        alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
        if (queued <= 0) {
            break;
        }
        // 等待n帧 IDLE_WAIT_FRAMES
        av_usleep(1000 * 20);
    }
}

static void play_audio_frame(StreamContext *sc, const AVFrame *frame) {
    static int pos_in;
    ALuint buf;

    if (frame->format != AV_SAMPLE_FMT_S16) {
        logAudioE("frame format is %s, not %s\n",
                  av_get_sample_fmt_name(frame->format),
                  av_get_sample_fmt_name(AV_SAMPLE_FMT_S16));
        exit(-1);
    }

    // 1. 清理已经播放的数据，给队列腾出空间；
    // 2. 如果音频队列未满，则将数据入队，并跳转至Step4；
    // 3. 如果音频队列已满，则等待一帧的时间，并跳转至Step2重新检查；
    // 4. 更新当前时间；
    // 5. 结束。

    /* 统计速率 */
    /* static int64_t last_pts = 0, last_msec = 0; */
    /* alGetSourcei(a_src, AL_SOURCE_STATE, &state); */
    /* alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued); */
    /* alGetSourcei(a_src, AL_BUFFERS_PROCESSED, &processed); */
    /* int64_t curr_msec = get_time_millisec(); */
    /* logAudio( */
    /* "frame: queued=%d, processed=%d, state=%s; pts=%ld, " */
    /* "pts_diff=%ld, pkt_diff=%ld\n", */
    /* queued, processed, get_source_state_name(state), frame->pts, */
    /* frame->pts - last_pts, curr_msec - last_msec); */
    /* last_pts = frame->pts; */
    /* last_msec = curr_msec; */

    ALint state, queued, processed;

    for (int n = 0;; n++) {  // 等待队列有空间
        free_buffers(sc);
        alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
        if (queued < MAX_QUEUED_FRAMES) {
            break;
        }
        alGetSourcei(a_src, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(a_src);
            check_al_error("alSourcePlay");
        }
        // 等待n帧 IDLE_WAIT_FRAMES
        av_usleep(
            pts_to_microseconds(sc, frame->nb_samples * IDLE_WAIT_FRAMES));
    }

    alloc_buffer_and_queue(sc, frame);
    alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei(a_src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        alSourcePlay(a_src);
        check_al_error("alSourcePlay");
    }
}

void init_audio_play() {
    a_dev = alcOpenDevice(NULL);
    if (!a_dev) {
        logAudioE("failed to open audio device\n");
        goto end;
    }
    logAudio("open device %s\n", alcGetString(a_dev, ALC_DEVICE_SPECIFIER));
    a_ctx = alcCreateContext(a_dev, NULL);
    if (!a_ctx) {
        logAudioE("alCreateContext failed");
        goto end;
    }
    alcMakeContextCurrent(a_ctx);

    alGetError();
    alGenBuffers(NB_AL_BUFFER, a_buf);
    check_al_error("alGenBuffers");

    alGenSources(1, &a_src);
    check_al_error("alGenSources");

    alSourcei(a_src, AL_PITCH, 1);    // 音调乘数
    alSourcef(a_src, AL_GAIN, 0.2);   // 增益
    alSourcei(a_src, AL_LOOPING, 0);  // 循环

    alListener3f(AL_POSITION, 0, 0, 0);
    return;

end:
    a_dev = NULL;
}

static void convert_audio_frame(StreamContext *ctx, const AVFrame *frame) {
    AVFrame *s16Frame = convert_frame_to_stereo_s16(frame);
    play_audio_frame(ctx, s16Frame);
    av_frame_free(&s16Frame);
}

void *audio_play_thread(PlayContext *pc) {
    AVFrame *frame;
    Queue *q;
    StreamContext *sc = pc->audio_sc;
    AVRational time_base = sc->stream->time_base;
    // 一帧的时长，单位微秒
    int64_t frame_time = pts_to_microseconds(sc, 1);

    logRender("[audio-play] tid=%lu\n", pthread_self());
    init_audio_play();
    queue_init(&pts_queue);
    q = &sc->frame_queue;

    for (;;) {
        frame = queue_dequeue_wait(q, queue_has_data);
        if (!frame) {
            // 时间更新依赖上面的循环，如果播放到最后没数据了，
            // 需要处理下AL播放队列中剩余的内容，并更新时间
            wait_remain_buffers(sc);
            break;
        }
        convert_audio_frame(sc, frame);
        av_frame_free(&frame);
    }

    logRender("[audio-play] finished\n");
    return NULL;
}
