#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <libavutil/frame.h>

#include "codec.h"

void init_audio_play();
void process_audio_frame(const PlayContext *ctx, const AVFrame *frame);

#endif /* ifndef _AUDIO_H_ */
