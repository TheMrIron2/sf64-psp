#ifndef PSP_PLATFORM_H
#define PSP_PLATFORM_H

#include "sf64thread.h"

void PspPlatform_Init(void);
void PspPlatform_PollInput(OSContPad* pads);
void PspPlatform_PostViEvent(void);
void PspPlatform_RunGfxTask(SPTask* task);
void PspPlatform_RunAudioTask(SPTask* task);
void PspPlatform_DebugFrame(void);

void PspPlatform_SetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg);
void PspPlatform_SetViEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount);
void PspPlatform_RequestExit(void);

#endif
