# SimplePlayer

一个简单的播放器。

## 依赖库

- 解封装、解码：FFmpeg
- 视频渲染：OpenGL
- 音频渲染：OpenAL

## TODO

- [x] 音频播放
- [x] 音频卡顿问题
    - `alSourcePlay` 调用时机不合适
    - > `alSourcePlay`: When called on a source which is already playing, the source will restart at the beginning.
- [x] 拆文件
- [x] 拆线程、拆队列
    - 队列消费逻辑
    - 线程划分：生产线程、消费线程
    - 数据背压
    - 线程同步
- [ ] 完善音频播放
    - [x] 重写队列逻辑
    - [ ] 音频状态跳动
        - 包速率莫名变低，导致队列消耗完，AL将状态转到了STOPPED，出现音频卡顿
        - 视频帧的消耗速率比实际的pts要慢，而音频的播放由音频库维护，因此速度不会慢，
        - 这导致了音频包队列和音频帧队列依次逐渐被堆满，阻塞音频播放；
        - 本质上还是因为视频播放机制不完善，以及缺乏音视频同步机制。
    - [ ] 音频时间更新
- [ ] 音画同步
    - 音频始终正常播放，同时维护播放位置；
    - 视频解码出来之后，判断与音频的时间差：
        - 如果时间差小于阈值，正常渲染；
        - 如果时间差大于阈值，在渲染的同时，seek视频流
            - ? 是否要seek。seek可能会导致画面跳动
    - VSync驱动（glfwPollEvents），每次判断视频画面是否需要更新（观察者模式）
    - ? 是否需要两个FormatContext分别解析音视频流？
- [ ] 进度调整
    - av_seek_frame
    - 队列清空
- [ ] 优化日志系统
    - 支持Tag
    - 支持优先级
- [ ] 错误处理
- [ ] Kotlin/Native
- [ ] 视频右侧花条
- [ ] sysroot
