// clang-format off
#include <glad/glad.h>
// clang-format on

#include <AL/al.h>
#include <AL/alc.h>
#include <GLFW/glfw3.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>

#include "audio.h"
#include "codec.h"
#include "list.h"
#include "video.h"

// av_format_open_input会同时作为输入/输出读取，所以需要初始化为NULL
static AVFormatContext *fc = NULL;
static AVStream *v_stream = NULL, *a_stream = NULL;
static const AVCodec *v_codec = NULL, *a_codec = NULL;
static AVCodecContext *v_cc = NULL, *a_cc = NULL;

static AVStream *find_stream_by_type(const AVFormatContext *fc,
                                     enum AVMediaType type) {
    AVStream *stream;
    for (int i = 0; i < fc->nb_streams; i++) {
        stream = fc->streams[i];
        if (stream->codecpar->codec_type == type) {
            return stream;
        }
    }
    return NULL;
}

static void get_codec(const AVStream *stream, const AVCodec **outCodec,
                      AVCodecContext **outCodecCtx) {
    int ret;
    const AVCodec *codec;
    AVCodecContext *cc;

    *outCodec = NULL;
    *outCodecCtx = NULL;

    // avcodec 从FormatContext里面获取第一个流的编码器ID
    if ((codec = avcodec_find_decoder(stream->codecpar->codec_id)) == NULL) {
        error("find_decoder");
    }
    // avcodec 根据编码器ID分配一个CodecContext
    if ((cc = avcodec_alloc_context3(codec)) == NULL) {
        averror(AVERROR_UNKNOWN, "alloc codec context");
    }
    // avcodec 将流中的视频信息赋给解码器
    if ((ret = avcodec_parameters_to_context(cc, stream->codecpar)) != 0) {
        averror(ret, "avcodec_parameters_to_context");
    }
    // avcodec 打开解码器
    if ((ret = avcodec_open2(cc, codec, NULL)) != 0) {
        averror(ret, "open codec");
    }
    *outCodec = codec;
    *outCodecCtx = cc;
}

static void init_decode(const char *file) {
    int ret;

    // 打开文件输入
    if ((ret = avformat_open_input(&fc, file, NULL, NULL)) != 0) {
        averror(ret, "open_input");
    }

    // 有的封装格式的流信息在Packet里面，这个函数会读取这些Packet得到流信息
    if ((ret = avformat_find_stream_info(fc, NULL)) < 0) {
        averror(ret, "find_stream_info");
    }

    v_stream = find_stream_by_type(fc, AVMEDIA_TYPE_VIDEO);
    a_stream = find_stream_by_type(fc, AVMEDIA_TYPE_AUDIO);
    if (!v_stream && !a_stream) {
        error("no available stream found");
    }
    if (v_stream) {
        get_codec(v_stream, &v_codec, &v_cc);
    }
    if (a_stream) {
        get_codec(a_stream, &a_codec, &a_cc);
    }
}

#ifdef TEST
void test() {
    test_list();
    test_queue();
}
#endif

void print_versions() {
    char *alVersion = "unknown";
#ifdef AL_VERSION_1_1
    alVersion = "1.1";
#else
    alVersion = "1.0";
#endif

    dprintf(2,
            "FFmpeg version %s\n"
            "OpenGL version %d.%d\n"
            "OpenAL version: %s\n",
            av_version_info(), GLVersion.major, GLVersion.minor, alVersion);
}

int main(int argc, char *argv[]) {
#ifdef TEST
    printf("testing...\n");
    test();
    printf("all test passed\n");
    return 0;
#endif

    if (argc != 2) {
        dprintf(2, "usage: %s FILE\n", argv[0]);
        return -1;
    }
    init_decode(argv[1]);

    PlayContext *v_ctx = NULL, *a_ctx = NULL;
    if (v_cc) {
        v_ctx = malloc(sizeof(PlayContext));
        *v_ctx = (PlayContext){
            .media_type = AVMEDIA_TYPE_VIDEO,
            .stream = v_stream,
            .cc = v_cc,
        };
        queue_init(&v_ctx->pkt_queue);
        queue_init(&v_ctx->frame_queue);
    }
    if (a_cc) {
        a_ctx = malloc(sizeof(PlayContext));
        *a_ctx = (PlayContext){
            .media_type = AVMEDIA_TYPE_AUDIO,
            .stream = a_stream,
            .cc = a_cc,
        };
        queue_init(&a_ctx->pkt_queue);
        queue_init(&a_ctx->frame_queue);
    }
    DemuxContext demux_ctx = {
        .fc = fc,
        .v_ctx = v_ctx,
        .a_ctx = a_ctx,
    };

    pthread_t t_a, t_a_play, t_v, t_v_play, t_demux;
    if (v_ctx) {
        pthread_create(&t_v, NULL, (void *)decode_thread, v_ctx);
        pthread_create(&t_v_play, NULL, (void *)video_play_thread, v_ctx);
    }
    if (a_ctx) {
        pthread_create(&t_a, NULL, (void *)decode_thread, a_ctx);
        pthread_create(&t_a_play, NULL, (void *)audio_play_thread, a_ctx);
    }
    pthread_create(&t_demux, NULL, (void *)demux_thread, &demux_ctx);
    if (v_ctx) {
        pthread_join(t_v, NULL);
        pthread_join(t_v_play, NULL);
    }
    if (a_ctx) {
        pthread_join(t_a, NULL);
        pthread_join(t_a_play, NULL);
    }
    pthread_join(t_demux, NULL);
    return 0;
}
