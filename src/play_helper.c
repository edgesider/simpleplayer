/**
 * 放一些音视频播放可以公用的函数
 */

#include "play_helper.h"

#include "config.h"

static Event *wait_for_event(StreamContext *sc, enum EventType type) {
    for (;;) {
        Event *event =
            queue_dequeue_wait(&sc->play_event_queue, queue_has_data);
        logRender("not resume\n");
        if (event->type == EVENT_RESUME) {
            return event;
        }
        event_unref(event);
    }
}

void process_play_events(StreamContext *sc, PauseResumeAction onPause,
                         PauseResumeAction onResume) {
    for (int i = 0; i < MAX_EVENTS_PER_FRAME; i++) {
        Event *event = queue_dequeue(&sc->play_event_queue);
        if (!event) {
            break;
        }
        logRender("[event-video] get type %d\n", event->type);
        switch (event->type) {
            case EVENT_PAUSE:
                event_unref(event);
                if (onPause) {
                    onPause(sc);
                }
                event_unref(wait_for_event(sc, EVENT_RESUME));
                if (onResume) {
                    onResume(sc);
                }
                return;
            case EVENT_RESUME:
                // pass
                break;
            case EVENT_STOP:
                break;
        }
        event_unref(event);
    }
}
