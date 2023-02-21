#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <libavutil/frame.h>

#include "codec.h"

void init_render();
void process_video_frame(const PlayContext *ctx, const AVFrame *frame);

#endif /* ifndef _VIDEO_H_ */
