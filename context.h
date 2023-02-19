#ifndef _CONTEXT_H_
#define _CONTEXT_H_

// av_format_open_input会同时作为输入/输出读取，所以需要初始化为NULL
#include "libs/ffmpeg5.1.2/include/libavcodec/avcodec.h"
#include "libs/ffmpeg5.1.2/include/libavformat/avformat.h"

extern AVCodecContext *vCC, *aCC;

#endif /* ifndef _CONTEXT_H_ */
