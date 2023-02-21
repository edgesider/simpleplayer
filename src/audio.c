#include "audio.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <libswresample/swresample.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

// 音频播放相关
#define NB_AL_BUFFER 64
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

static void play_audio_frame(const PlayContext *ctx, const AVFrame *frame) {
    static int posIn;
    ALuint buf;

    if (frame->format != AV_SAMPLE_FMT_S16) {
        logAudioE("frame format is %s, not %s\n",
                  av_get_sample_fmt_name(frame->format),
                  av_get_sample_fmt_name(AV_SAMPLE_FMT_S16));
        exit(-1);
    }

    ALint queued, processed;
    static int pending_space = NB_AL_BUFFER;
    static int pending_filled = 0;
    static ALuint pending[NB_AL_BUFFER];

    alGetSourcei(a_src, AL_BUFFERS_QUEUED, &queued);
    if (queued < NB_AL_BUFFER && pending_filled < pending_space) {
        buf = a_buf[posIn % NB_AL_BUFFER];
        logAudio("pending[%d]: %d\n", queued, buf);
        alBufferData(buf, AL_FORMAT_STEREO16, frame->data[0],
                     frame->linesize[0], frame->sample_rate);
        check_al_error("BufferData");
        pending[pending_filled++] = buf;
        posIn++;
    }

    if (pending_filled == pending_space) {
        logAudio("queuing %d frame\n", pending_space);
        alSourceQueueBuffers(a_src, pending_space, pending);
        check_al_error("Queue");
        pending_space = NB_AL_BUFFER;
        pending_filled = 0;
        usleep(1000 * 150);
    }

    ALint state;
    alGetSourcei(a_src, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        logAudio("not playing\n");
        alSourcePlay(a_src);
        check_al_error("Play");
    }

    alGetSourcei(a_src, AL_BUFFERS_PROCESSED, &processed);
    if (processed > 0) {
        ALuint unqueued[NB_AL_BUFFER];
        alSourceUnqueueBuffers(a_src, processed, unqueued);
        check_al_error("Unqueue");
        pending_space = processed;
        pending_filled = 0;
        logAudio("unqueue[%d]: ", processed);
        for (int i = 0; i < processed; i++) {
            logAudio(i == 0 ? "%d" : " %d", unqueued[i]);
        }
        logAudio(", ");
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
