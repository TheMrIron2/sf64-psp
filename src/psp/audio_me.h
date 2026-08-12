#ifndef SF64_PSP_AUDIO_ME_H
#define SF64_PSP_AUDIO_ME_H

#include "PR/abi.h"
#include "PR/ultratypes.h"
#include "src/psp/gfx/gfx_me_replay.h"

int PspAudioMe_Boot(void);
int PspAudioMe_Init(void);
void PspAudioMe_Submit(const Acmd* commands, s32 commandCount);
void PspAudioMe_Wait(void);
void PspMe_WaitGfxReplayPool(const void* task);
int PspMe_RunGfxTransform(const void* task, const Gfx* dl, u32 taskIndex,
                          const PspGfxMeTransformTrace** trace, u32* traceCount);
void PspMe_SubmitGfxReplay(const void* task, const Gfx* dl, u32 taskIndex,
                           const PspGfxMeReplayStats* expected,
                           const PspGfxMeTransformTrace* expectedTrace);
int PspAudioMe_IsActive(void);
int PspAudioMe_GetLastError(void);
void PspMe_GetGfxReplayCounts(u32* withinFine, u32* withinCoarse,
                              u32* overCoarse, u32* structuralMismatches,
                              u32* maxErrorQ16, u32* skippedBusy);

#endif
