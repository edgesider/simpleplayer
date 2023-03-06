#ifndef _RENDER_H_
#define _RENDER_H_

#include <libavutil/frame.h>
#include <pthread.h>

void start_render();
void commit_frame(AVFrame *frame);
void stop_render();

#endif /* ifndef _RENDER_H_ */
