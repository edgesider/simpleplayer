#include "event.h"
#include "utils.h"

#include <stdlib.h>

Event *event_alloc(enum EventType type) {
    return event_alloc_ref(type, 1);
}

Event *event_alloc_ref(enum EventType type, int init_ref) {
    assert(init_ref > 0);
    Event *event = malloc(sizeof(Event));
    *event = (Event){
        .type = type,
    };
    atomic_init(&event->refcount, init_ref);
    return event;

}

void event_ref(Event *event) {
    atomic_fetch_add(&event->refcount, 1);
}

void event_unref(Event *event) {
    if (atomic_fetch_sub(&event->refcount, 1) == 1) {
        event_free(event);
    }
}

void event_free(Event *event) {
    assert(atomic_load(&event->refcount) == 0);
    free(event);
}
