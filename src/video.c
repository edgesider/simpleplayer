// clang-format off
#include <glad/glad.h>
// clang-format on

#include "video.h"

#include <GLFW/glfw3.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <pthread.h>

#include "config.h"
#include "utils.h"

static GLFWwindow *window;
static uint texture = -1, program = -1;
static uint vao, vbo;
static float shaderBuffer[(3 + 2) * 4] = {
    -1, 1,  0, 0, 0,  // left-top
    -1, -1, 0, 0, 1,  // left-bottom
    1,  1,  0, 1, 0,  // right-top
    1,  -1, 0, 1, 1,  // right-bottom
};                    // (vec3 + vec2) * 4

static uint compile_shader(const char *code, uint type) {
    int succ = 0;
    char info[512];
    uint shader;

    shader = glCreateShader(type);
    if (shader == 0) {
        logRenderE("failed to create shader\n");
        exit(-1);
    }
    glShaderSource(shader, 1, &code, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &succ);
    if (!succ) {
        glGetShaderInfoLog(shader, sizeof(info), NULL, info);
        logRenderE("failed to compile shader: %s\n", info);
        exit(-1);
    }
    return shader;
}

static void check_gl_error() {
    int error = glGetError();
    if (error != GL_NO_ERROR) {
        logRenderE("gl error: %d\n", error);
        exit(-1);
    }
}

/**
 * 用于视频帧消费线程与渲染线程之间的交互。
 *
 * 视频播放线程会控制好帧停留时长，当需要渲染新的一帧时，就将其送入该队列；
 * 接着渲染线程会从该队列中取出最新的一帧，进行更新。两个线程是消费者-生产者
 * 的关系，但是消费者（渲染线程）始终只取最后一个入队的帧，其余的都丢弃。
 *
 * 渲染队列由VSync驱动，视频播放线程由视频帧率驱动，因此通常比视频播放线程
 * 更新频率高，这时候该队列基本上长度都小于等于1；如果遇到高帧率视频，或是
 * 屏幕刷新率低，导致视频播放线程更新速率大于等于VSync速率，该队列就会堆积
 * 多个帧，而渲染线程只需要取最新一帧即可，确保及时渲染。
 */
static Queue to_render;

static void init_render() {
    queue_init(&to_render);
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    window = glfwCreateWindow(2000, 2000, "ffmpeg-play", NULL, NULL);
    if (window == NULL) {
        logRenderE("failed to create window\n");
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        logRenderE("failed to initialize opengl\n");
        exit(-1);
    }

    glViewport(0, 0, 2000, 2000);

    int succ;
    uint verShader, fragShader;
    char info[512], *code;
    program = glCreateProgram();
    code =
        "#version 330 core\n"
        "layout (location = 0) in vec3 pos;\n"
        "layout (location = 1) in vec2 texPos;\n"
        "out vec2 vTexPos;\n"
        "void main() {\n"
        "    gl_Position = vec4(pos, 1.0);\n"
        "    vTexPos = texPos;\n"
        "}";
    logRender("compiling vertex shader:\n%s\n", code);
    verShader = compile_shader(code, GL_VERTEX_SHADER);
    code =
        "#version 330 core\n"
        "in vec2 vTexPos;\n"
        "uniform sampler2D tex;\n"
        "void main() {\n"
        "    gl_FragColor = texture(tex, vTexPos);\n"
        "}";
    logRender("compiling fragment shader:\n%s\n", code);
    fragShader = compile_shader(code, GL_FRAGMENT_SHADER);
    glAttachShader(program, verShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &succ);
    if (!succ) {
        glGetProgramInfoLog(program, sizeof(info), NULL, info);
        logRenderE("failed to link shader: %s\n", info);
        exit(-1);
    }
    glDeleteShader(verShader);
    glDeleteShader(fragShader);
    glUseProgram(program);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(shaderBuffer), shaderBuffer,
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 5, 0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 5,
                          (void *) (sizeof(float) * 3));

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 这里不能用枚举，只需要序号，设置为0就代表着GL_TEXTURE0
    glUniform1i(glGetUniformLocation(program, "tex"), 0);
}

static AVFrame *get_newest_frame() {
    AVFrame *frame, *newest;
    newest = NULL;
    while ((frame = queue_dequeue(&to_render)) != NULL) {
        if (newest != NULL) {
            av_frame_free(&newest);
        }
        newest = frame;
    }
    return newest;
}

/**
 * 渲染线程，同时也负责输入事件的捕获
 */
void *render_thread() {
    logRender("[render] tid=%lu\n", pthread_self());

    // TODO 等待渲染线程就绪之后，视频播放线程再开始工作
    init_render();
    AVFrame *curr_frame = NULL;

    // TODO 整体的状态管理（不应由视频线程退出整个进程）
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            glfwTerminate();
            exit(-1);
        }
        logRender("[render] rendering frame...\n");

        if (glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_FALSE) {
            glfwShowWindow(window);
        }

        AVFrame *new_frame = get_newest_frame();
        if (new_frame) {
            av_frame_free(&curr_frame);
            curr_frame = new_frame;
        }
        if (curr_frame) {
            // TODO 目前每帧都是全量更新的，是否可以优化成按需更新
            glfwSetWindowSize(window, curr_frame->width, curr_frame->height);
            glViewport(0, 0, curr_frame->width, curr_frame->height);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            // TODO 这里可能要处理一下linesize和width不一致的情况
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, curr_frame->width,
                         curr_frame->height, 0, GL_RGB, GL_UNSIGNED_BYTE,
                         curr_frame->data[0]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            check_gl_error();
        }
        glfwSwapBuffers(window);
    }
    if (curr_frame) {
        av_frame_free(&curr_frame);
    }

    glfwTerminate();
    return NULL;
}

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

static void process_video_frame(StreamContext *ctx, const AVFrame *frame) {
    queue_enqueue(&to_render, convert_frame_to_rgb24(frame));

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
                process_video_frame(sc, frame);
            } else {
                process_video_frame(sc, frame);
            }
        } else {
            process_video_frame(sc, frame);
        }
        av_frame_free(&frame);
    }
    logRender("[video-play] finished\n");

    return NULL;
}
