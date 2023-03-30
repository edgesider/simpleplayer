#include "event.h"

#include <stdlib.h>

#include "utils.h"

Event *event_alloc_base(enum EventType type) {
    return event_alloc(type, sizeof(Event));
}

Event *event_alloc(enum EventType type, size_t event_size) {
    assert(event_size >= sizeof(Event));

    Event *event = malloc(event_size);
    event->type = type;
    event->before_free = event->before_free_data = NULL;
    atomic_init(&event->refcount, 1);
    return event;
}

void event_ref(Event *event) {
    atomic_fetch_add(&event->refcount, 1);
}

void event_unref(Event *event) {
    if (atomic_fetch_sub(&event->refcount, 1) == 1) {
        if (event->before_free) {
            event->before_free(event->before_free_data);
        }
        event_free(event);
    }
}

void event_free(Event *event) {
    assert(atomic_load(&event->refcount) == 0);
    free(event);
}

void event_set_before_free(Event *event, BeforeEventFree fun, void *data) {
    event->before_free = fun;
    event->before_free_data = data;
}
