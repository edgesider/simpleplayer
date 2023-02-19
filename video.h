#ifndef _VIDEO_H_
#define _VIDEO_H_

#include <libavutil/frame.h>

void initRender();
void processVideoFrame(const AVFrame *frame);

#endif /* ifndef _VIDEO_H_ */
