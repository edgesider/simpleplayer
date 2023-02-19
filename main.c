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
#include "context.h"
#include "list.h"
#include "video.h"

// av_format_open_input会同时作为输入/输出读取，所以需要初始化为NULL
AVFormatContext *fc = NULL;
AVStream *vStream = NULL, *aStream = NULL;
const AVCodec *vCodec = NULL, *aCodec = NULL;
AVCodecContext *vCC = NULL, *aCC = NULL;

static AVStream *findStreamByType(const AVFormatContext *fc,
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

static void getCodec(const AVStream *stream, const AVCodec **outCodec,
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

static void processFrame(const AVFrame *frame, enum AVMediaType type) {
    switch (type) {
        case AVMEDIA_TYPE_VIDEO: processVideoFrame(frame); break;
        case AVMEDIA_TYPE_AUDIO: processAudioFrame(frame); break;
        default:
            logCodecE("unsupported frame type: %s\n",
                      av_get_media_type_string(type));
    }
}

static AVCodecContext *getCodecContextForPacket(const AVPacket *pkt) {
    if (pkt->stream_index == vStream->index) {
        return vCC;
    }
    if (pkt->stream_index == aStream->index) {
        return aCC;
    }
    return NULL;
}

static void putPacketToCodecContext(const AVPacket *pkt, AVCodecContext *cc) {
    int ret;
    AVFrame *frame;

    if ((frame = av_frame_alloc()) == NULL) {
        averror(AVERROR_UNKNOWN, "alloc frame");
    }

    // avcodec send_packet
    if ((ret = avcodec_send_packet(cc, pkt)) != 0) {
        averror(ret, "send packet");
    }

    int draining = pkt == NULL;

    // avcodec receive_frame
    for (int n_frame = 0;; n_frame++) {
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
        // @see 关于time_base https://www.jianshu.com/p/bf323cee3b8e
        logCodec("parsed new frame[%d]: type=%s, pts=%ld\n", n_frame,
                 av_get_media_type_string(cc->codec_type),
                 cc->codec_type == AVMEDIA_TYPE_VIDEO ? frame->pts : 0  // TODO
        );

        if (frame->format >= 0) {
            processFrame(frame, cc->codec_type);
        }
        av_frame_unref(frame);

        if (done) {
            // 当前包解析完毕
            break;
        }
    }
    av_frame_free(&frame);
}

void processPacket(const AVPacket *pkt) {
    if (!pkt) {
        // draining mode
        logCodec("enter draining mode\n");
        if (aCC) {
            putPacketToCodecContext(NULL, aCC);
        }
        if (vCC) {
            putPacketToCodecContext(NULL, vCC);
        }
        return;
    }
    enum AVMediaType pkt_type =
        fc->streams[pkt->stream_index]->codecpar->codec_type;
    AVCodecContext *cc = getCodecContextForPacket(pkt);

    if (!cc) {
        logCodecE("no codec available for pkt: stream=%d, type=%d",
                  pkt->stream_index, pkt_type);
        return;
    }

    putPacketToCodecContext(pkt, cc);
}

void initDecode(const char *file) {
    int ret;

    // avformat 打开文件输入
    if ((ret = avformat_open_input(&fc, file, NULL, NULL)) != 0) {
        averror(ret, "open_input");
    }

    // avformat
    // 有的封装格式的流信息在Packet里面，这个函数会读取这些Packet得到流信息
    if ((ret = avformat_find_stream_info(fc, NULL)) < 0) {
        averror(ret, "find_stream_info");
    }

    vStream = findStreamByType(fc, AVMEDIA_TYPE_VIDEO);
    aStream = findStreamByType(fc, AVMEDIA_TYPE_AUDIO);
    if (!vStream && !aStream) {
        logCodecE("no available stream found\n");
        exit(-1);
    }
    if (vStream) {
        getCodec(vStream, &vCodec, &vCC);
    }
    if (aStream) {
        getCodec(aStream, &aCodec, &aCC);
    }
}

#ifdef TEST
void test() {
    /* testList(); */
    test_queue();
}
#endif

void printVersions() {
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

    initDecode(argv[1]);

    /* av_log_set_level(AV_LOG_TRACE); */

    PlayContext *v_ctx = NULL, *a_ctx = NULL;
    if (vCC) {
        v_ctx = malloc(sizeof(PlayContext));
        *v_ctx = (PlayContext){
            .media_type = AVMEDIA_TYPE_VIDEO,
            .stream = vStream,
            .cc = vCC,
        };
        queue_init(&v_ctx->pkt_queue);
        queue_init(&v_ctx->frame_queue);
    }
    if (aCC) {
        a_ctx = malloc(sizeof(PlayContext));
        *a_ctx = (PlayContext){
            .media_type = AVMEDIA_TYPE_AUDIO,
            .stream = aStream,
            .cc = aCC,
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
