// clang-format off
#include <glad/glad.h>
// clang-format on

// 图像渲染相关
#include <GLFW/glfw3.h>
#include <libavutil/frame.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

#include "utils.h"
#include "video.h"

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

static void render_frame(const PlayContext *ctx, const AVFrame *frame) {
    if (glfwWindowShouldClose(window)) {
        glfwTerminate();
        exit(-1);
    }
    glfwPollEvents();
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        glfwTerminate();
        exit(-1);
    }
    logRender("rendering frame...\n");

    if (glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_FALSE) {
        glfwShowWindow(window);
    }
    glfwSetWindowSize(window, frame->width, frame->height);
    glViewport(0, 0, frame->width, frame->height);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // 这里可能要处理一下linesize和width不一致的情况
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frame->width, frame->height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, frame->data[0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    check_gl_error();
    glfwSwapBuffers(window);

    int64_t frameTime =
        av_rescale_q(1000 * 1000, (AVRational){1, 1}, ctx->cc->framerate);
    av_usleep(frameTime);
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

    sws_scale(sc, (const uint8_t *const *)frame->data, frame->linesize, 0,
              frame->height, outFrame->data, outFrame->linesize);

    sws_freeContext(sc);
    return outFrame;
}

void init_render() {
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

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
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
                          (void *)(sizeof(float) * 3));

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

void process_video_frame(const PlayContext *ctx, const AVFrame *frame) {
    AVFrame *rgbFrame = convert_frame_to_rgb24(frame);
    render_frame(ctx, rgbFrame);
    av_frame_free(&rgbFrame);
}
