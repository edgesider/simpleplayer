#include "render.h"

// clang-format off
// glad需要在glfw之前
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include "event.h"
#include "queue.h"
#include "utils.h"

static pthread_t tid;
static PlayContext *pc;
/**
 * 用于视频帧消费线程与渲染线程之间的交互。
 *
 * 与上面几个队列相比，渲染线程作为消费者的消费逻辑不太一样，
 * 它不会保证处理队列里面的每一个元素，而是每次只取队列的最
 * 后一个元素，同时跳过并释放其他元素。
 *
 * 这是由于渲染队列需要尽量使得每帧都能显示出最新的画面，
 * 而不能因为排队而延迟。具体来说，渲染队列由VSync驱动，
 * 视频播放线程由视频帧率驱动，因此通常比视频播放线程更新
 * 频率高，这时候该队列基本上长度都小于等于1；如果遇到高
 * 帧率视频，或是屏幕刷新率低，导致视频播放线程更新速率大
 * 于等于VSync速率，该队列就会堆积多个帧，而渲染线程只需
 * 要取最新一帧即可，确保及时渲染。
 */
static Queue to_render;
static int stop_requested = 0;

static GLFWwindow *window;
static uint texture = -1, program = -1;
static uint vao, vbo;
static float shaderBuffer[(3 + 2) * 4] = {
    -1, 1,  0, 0, 0,  // left-top
    -1, -1, 0, 0, 1,  // left-bottom
    1,  1,  0, 1, 0,  // right-top
    1,  -1, 0, 1, 1,  // right-bottom
};                    // (vec3 + vec2) * 4

static void check_gl_error() {
    int error = glGetError();
    if (error != GL_NO_ERROR) {
        logRenderE("gl error: %d\n", error);
        exit(-1);
    }
}

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

static void on_key_event(GLFWwindow *win, int key, int scancode, int action,
                         int mods);

static void init_render() {
    queue_init(&to_render);
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    window = glfwCreateWindow(1000, 1000, "SimplePlayer", NULL, NULL);
    if (window == NULL) {
        logRenderE("failed to create window\n");
        glfwTerminate();
        exit(-1);
    }
    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, on_key_event);

    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        logRenderE("failed to initialize opengl\n");
        exit(-1);
    }

    glViewport(0, 0, 1000, 1000);

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

static void on_key_event(GLFWwindow *win, int key, int scancode, int action,
                         int mods) {
    if (key == GLFW_KEY_Q && mods == 0) {
        glfwTerminate();
        // TODO 整体的状态管理（不应由视频线程退出整个进程）
        exit(-1);
    } else if (key == GLFW_KEY_RIGHT && action == GLFW_PRESS) {
        logRender("[event] forward\n");
        play_seek(pc, pc->audio_sc->play_time + 5 * 1000 * 1000);
    } else if (key == GLFW_KEY_LEFT && action == GLFW_PRESS) {
        logRender("[event] backword\n");
        play_seek(pc, pc->audio_sc->play_time - 5 * 1000 * 1000);
    } else if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        logRender("[event] toggle play state\n");
        play_toggle(pc);
    } else if (key == GLFW_KEY_I && action == GLFW_PRESS) {
        dump_queue_info(pc);
    }
}

/**
 * 渲染线程，同时也负责输入事件的捕获
 */
static void *render_thread() {
    logRender("[render] tid=%lu\n", pthread_self());

    // TODO 等待渲染线程就绪之后，视频播放线程再开始工作
    init_render();

    AVFrame *curr_frame = NULL;

    while (!glfwWindowShouldClose(window) && !stop_requested) {
        glfwPollEvents();
        /* logRender("[render] rendering frame...\n"); */

        if (glfwGetWindowAttrib(window, GLFW_VISIBLE) == GLFW_FALSE) {
            glfwShowWindow(window);
        }
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        AVFrame *new_frame = get_newest_frame();
        if (new_frame) {
            av_frame_free(&curr_frame);
            curr_frame = new_frame;
        }
        if (curr_frame) {
            // TODO 目前每帧都是全量更新的，是否可以优化成
            // 按需更新（关键在于如何处理SwapBuffers）
            glfwSetWindowSize(window, curr_frame->width, curr_frame->height);
            glViewport(0, 0, curr_frame->width, curr_frame->height);
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

void start_render(PlayContext *ctx) {
    pc = ctx;
    pthread_create(&tid, NULL, render_thread, NULL);
}

void commit_frame(AVFrame *frame) {
    queue_enqueue(&to_render, frame);
}

void stop_render() {
    stop_requested = 1;
    pthread_join(tid, NULL);
}
