#ifndef SF64_PSP_AUDIO_ME_H
#define SF64_PSP_AUDIO_ME_H

#include "PR/abi.h"
#include "PR/ultratypes.h"
#include "src/psp/gfx/gfx_me_replay.h"

int PspAudioMe_Boot(void);
int PspAudioMe_Init(void);
void PspAudioMe_Submit(const Acmd* commands, s32 commandCount);
void PspAudioMe_Wait(void);
void PspMe_SubmitGfxReplay(const Gfx* dl, u32 taskIndex,
                           const PspGfxMeReplayStats* expected);
int PspAudioMe_IsActive(void);
int PspAudioMe_GetLastError(void);

#endif
