#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <libavutil/frame.h>

#include "codec.h"

void *audio_play_thread(PlayContext *params);

#endif /* ifndef _AUDIO_H_ */
