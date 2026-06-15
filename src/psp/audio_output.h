#ifndef SF64_PSP_AUDIO_OUTPUT_H
#define SF64_PSP_AUDIO_OUTPUT_H

int PspAudioOutput_Init(void);
int PspAudioOutput_Submit(const void* samples, unsigned int size);
unsigned int PspAudioOutput_GetQueuedBytes(void);

#endif
