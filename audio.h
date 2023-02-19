#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <libavutil/frame.h>

void initAudioPlay();
void processAudioFrame(const AVFrame *frame);

#endif /* ifndef _AUDIO_H_ */
