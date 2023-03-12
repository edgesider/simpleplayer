#ifndef _EVENT_H_
#define _EVENT_H_

#include <stdatomic.h>

enum PlayState {
    STATE_PLAYING,
    STATE_PAUSE,
};

enum EventType {
    EVENT_PAUSE,
    EVENT_RESUME,
    EVENT_STOP,
};

typedef struct {
    enum EventType type;
    atomic_int refcount;
} Event;

Event *event_alloc(enum EventType type);
Event *event_alloc_ref(enum EventType type, int init_ref);
void event_ref(Event *event);
void event_unref(Event *event);
void event_free(Event *event);

#endif /* ifndef _EVENT_H_ */
