#ifndef SF64_PSP_AUDIO_ME_H
#define SF64_PSP_AUDIO_ME_H

#include "PR/abi.h"
#include "PR/ultratypes.h"

typedef enum {
    PSP_AUDIO_VME_UNINITIALIZED,
    PSP_AUDIO_VME_DISABLED,
    PSP_AUDIO_VME_INITIALIZING,
    PSP_AUDIO_VME_READY,
    PSP_AUDIO_VME_FAULT,
} PspAudioVmeState;

typedef struct {
    PspAudioVmeState state;
    u32 checkpoint;
    u32 runs;
    u32 samples;
    u32 mismatches;
    s32 firstIndex;
    s32 input;
    s32 factor;
    s32 expected;
    s32 actual;
} PspAudioVmeSmokeResult;

typedef struct {
    u32 calls;
    u32 samples;
    u32 mismatches;
    s32 firstIndex;
    s32 input;
    s32 oldOutput;
    s32 gain;
    s32 expected;
    s32 actual;
} PspAudioVmeMixResult;

int PspAudioMe_Boot(void);
int PspAudioMe_Init(void);
void PspAudioMe_Submit(const Acmd* commands, s32 commandCount);
void PspAudioMe_Wait(void);
int PspAudioMe_IsActive(void);
int PspAudioMe_GetLastError(void);
void PspAudioMe_GetVmeSmokeResult(PspAudioVmeSmokeResult* result);
int PspAudioMe_ValidateVmeMix(u16 count, s16 gain, const s16* input,
                              const s16* output);
void PspAudioMe_GetVmeMixResult(PspAudioVmeMixResult* result);

#endif
