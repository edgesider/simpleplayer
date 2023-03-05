#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <libavutil/frame.h>

#include "codec.h"

void *video_play_thread(PlayContext *pc);
void *render_thread();

#endif /* ifndef _VIDEO_H_ */
