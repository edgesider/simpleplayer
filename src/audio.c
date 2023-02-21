#include "audio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <stdlib.h>
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
                        (AVChannelLayout *)&frame->ch_layout, frame->format,
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

    swr_convert(ctx, outFrame->data, samples, (const uint8_t **)frame->data,
                samples);
    swr_free(&ctx);
    return outFrame;
}

static void alloc_buffer_and_queue(const AVFrame *frame) {
    ALuint buf;
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_STEREO16, frame->data[0], frame->linesize[0],
                 frame->sample_rate);
    check_al_error("alBufferData");
    alSourceQueueBuffers(a_src, 1, &buf);
    check_al_error("alSourceQueueBuffers");
}

static void free_buffers() {
    ALint processed;
    alGetSourcei(a_src, AL_BUFFERS_PROCESSED, &processed);
    if (processed > 0) {
        ALuint *buffers = malloc(sizeof(ALint) * processed);
        alSourceUnqueueBuffers(a_src, processed, buffers);
        alDeleteBuffers(processed, buffers);
    }
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

static void play_audio_frame(const PlayContext *ctx, const AVFrame *frame) {
    static int pos_in;
    ALuint buf;

    if (frame->format != AV_SAMPLE_FMT_S16) {
        logAudioE("frame format is %s, not %s\n",
                  av_get_sample_fmt_name(frame->format),
                  av_get_sample_fmt_name(AV_SAMPLE_FMT_S16));
        exit(-1);
    }

    // 1. 清理已经播放的数据，给队列腾出空间；
    // 2. 如果音频队列未满，则将数据入队，并跳转至Step5；
    // 3. 如果音频队列已满，则等待一帧的时间；
    // 4. 重新检查队列是否已满，如果仍满，跳转到Step1；
    // 5. 更新当前时间；
    // 6. 结束。

#define START_MIN_QUEUED 1  // TODO 队列限制使用时间为单位
#define MAX_QUEUED 20
#define ONE_FRAME_TIME 20  // TODO 一帧的时间

    ALint state, queued, processed;

    alGetSourcei(a_src, AL_SOURCE_STATE, &state);
    alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei(a_src, AL_BUFFERS_PROCESSED, &processed);
    logAudio("play_audio_frame: queued=%d, processed=%d, state=%s\n", queued,
             processed, get_source_state_name(state));

    for (;;) { // 等待队列有空间
        free_buffers();
        alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
        alGetSourcei(a_src, AL_SOURCE_STATE, &state);
        if (queued < MAX_QUEUED) {
            break;
        }
        logAudio("audio buffer is full, waiting...\n");
        if (state != AL_PLAYING) {
            alSourcePlay(a_src);
            check_al_error("alSourcePlay");
        }
        av_usleep(1000 * ONE_FRAME_TIME);
    }

    alloc_buffer_and_queue(frame);
    alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei(a_src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        if (queued >= START_MIN_QUEUED) {
            alSourcePlay(a_src);
            check_al_error("alSourcePlay");
        }
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

void process_audio_frame(const PlayContext *ctx, const AVFrame *frame) {
    AVFrame *s16Frame = convert_frame_to_stereo_s16(frame);
    play_audio_frame(ctx, s16Frame);
    av_frame_free(&s16Frame);
}
