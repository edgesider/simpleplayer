/**
 * 放一些音视频播放可以公用的函数
 */

#include "event_helper.h"

#include "config.h"

Event *wait_for_event(Queue *event_queue, enum EventType type) {
    for (;;) {
        Event *event = queue_dequeue_wait(event_queue, queue_has_data);
        if (event->type == type) {
            return event;
        }
        event_unref(event);
    }
}

void process_play_events(StreamContext *sc, EventAction onPause,
                         EventAction onResume, EventAction onSeek) {
    for (int i = 0; i < MAX_EVENTS_PER_LOOP; i++) {
        Event *event = queue_dequeue(&sc->play_event_queue);
        if (!event) {
            break;
        }
        logCodec("[event-play] get type %d\n", event->type);
        switch (event->type) {
            case EVENT_PAUSE:
                if (onPause) {
                    onPause(sc);
                }
                // TODO 漏事件问题
                event_unref(
                    wait_for_event(&sc->play_event_queue, EVENT_RESUME));
                if (onResume) {
                    onResume(sc);
                }
            case EVENT_RESUME:
                // pass
                break;
            case EVENT_STOP:
                break;
            case EVENT_SEEK_START: {
                if (onSeek) {
                    onSeek(sc);
                }
                sc->play_time = ((SeekEvent *) event)->to_microseconds;
                event_unref(
                    wait_for_event(&sc->play_event_queue, EVENT_SEEK_END));
            } break;
            default:
                break;
        }
        event_unref(event);
    }
}
