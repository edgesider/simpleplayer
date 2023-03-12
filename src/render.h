#ifndef _RENDER_H_
#define _RENDER_H_

#include <libavutil/frame.h>
#include <pthread.h>

#include "codec.h"

void start_render(PlayContext *ctx);
void commit_frame(AVFrame *frame);
void stop_render();

#endif /* ifndef _RENDER_H_ */
