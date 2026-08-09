#ifndef SF64_PSP_AUDIO_ME_H
#define SF64_PSP_AUDIO_ME_H

#include "PR/abi.h"
#include "PR/ultratypes.h"

int PspAudioMe_Boot(void);
int PspAudioMe_Init(void);
void PspAudioMe_Submit(const Acmd* commands, s32 commandCount);
void PspAudioMe_Wait(void);
int PspAudioMe_IsActive(void);
int PspAudioMe_GetLastError(void);

#endif
