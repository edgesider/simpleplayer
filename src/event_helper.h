#ifndef _PLAY_HELPER_H_
#define _PLAY_HELPER_H_

#include "codec.h"

typedef void (*EventAction)(StreamContext *sc);

Event *wait_for_event(Queue *event_queue, enum EventType type);
void process_play_events(StreamContext *sc, EventAction onPause,
                         EventAction onResume, EventAction onSeek);

#endif /* ifndef _PLAY_HELPER_H_ */
