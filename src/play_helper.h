#ifndef _PLAY_HELPER_H_
#define _PLAY_HELPER_H_

#include "codec.h"

typedef void (*PauseResumeAction)(StreamContext *sc);

void process_play_events(StreamContext *sc, PauseResumeAction onPause,
                         PauseResumeAction onResume);

#endif /* ifndef _PLAY_HELPER_H_ */
