#ifndef _CONFIG_H_
#define _CONFIG_H_

// TODO 队列大小使用时间来限制
// 包队列的大小
#define PKT_QUEUE_SIZE 20
// 帧队列的大小
#define FRAME_QUEUE_SIZE 40

// 每次进入事件处理函数最多可以处理的事件数量
#define MAX_EVENTS_PER_LOOP 10
// 等待队列就绪的超时
#define QUEUE_WAIT_MICROSECONDS 16 * 1000

// 触发音画同步的阈值
#define SYNC_DIFF_THRESHOLD (50 * 1000)  // in microseconds
// 音画同步最多等待的帧数
#define SYNC_MAX_WAIT_FRAMES 1

#endif /* ifndef _CONFIG_H_ */
