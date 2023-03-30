#ifndef _EVENT_H_
#define _EVENT_H_

#include <stdatomic.h>
#include <stddef.h>
#include <sys/types.h>

enum EventType {
    EVENT_RETURN,

    EVENT_PAUSE,
    EVENT_RESUME,
    EVENT_STOP,

    EVENT_SEEK_START,
    EVENT_SEEK_END,
};

typedef void (*BeforeEventFree)(void *data);

#define EVENT_OBJ_HEAD           \
    enum EventType type;         \
    atomic_int refcount;         \
    BeforeEventFree before_free; \
    void *before_free_data;

typedef struct {
    EVENT_OBJ_HEAD
} Event;

typedef struct {
    EVENT_OBJ_HEAD

    int64_t to_microseconds;
} SeekEvent;

Event *event_alloc_base(enum EventType ype);
Event *event_alloc(enum EventType type, size_t event_size);
void event_ref(Event *event);
void event_unref(Event *event);
void event_free(Event *event);
void event_set_before_free(Event *event, BeforeEventFree fun, void *data);

#endif /* ifndef _EVENT_H_ */
