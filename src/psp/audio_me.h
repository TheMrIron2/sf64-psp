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

typedef struct {
    u32 runs;
    u32 outputs;
    u32 mismatches;
    s32 firstCase;
    s32 firstIndex;
    s32 expected;
    s32 actual;
} PspAudioVmeFilterResult;

typedef struct {
    u32 runs;
    u32 products;
    u32 mismatches;
    s32 firstLane;
    s32 firstIndex;
    s32 input;
    s32 coefficient;
    s32 expected;
    s32 actual;
    u32 commands;
    u32 outputs;
    u32 pairMismatches;
    u32 outputMismatches;
    u32 stateMismatches;
    u32 skipped;
    s32 firstOutputIndex;
    s32 outputExpected;
    s32 outputActual;
    s32 firstPairIndex;
    s32 pair01Expected;
    s32 pair01Actual;
    s32 pair23Expected;
    s32 pair23Actual;
    s32 firstStateIndex;
    s32 stateExpected;
    s32 stateActual;
} PspAudioVmeResampleResult;

#define PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES 1024

#define PSP_AUDIO_VME_BENCH_ROWS 8

typedef struct {
    u32 samples;
    u32 calls;
    u32 scalarCalls;
    u64 scalarTicks;
    u64 stageTicks;
    u64 updateTicks;
    u64 runTicks;
    u64 readbackTicks;
    u64 postTicks;
} PspAudioVmeBenchRow;

typedef struct {
    u32 calls;
    u32 samples;
    u32 mismatches;
    s32 firstMismatch;
    s32 expected;
    s32 actual;
    u64 scalarTicks;
    u64 prepareTicks;
    u64 stageTicks;
    u64 updateTicks;
    u64 runTicks;
    u64 pairTicks;
    u64 readbackTicks;
    u64 validateTicks;
    u64 wipeTicks;
} PspAudioVmeResampleBenchResult;

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
void PspAudioMe_GetVmeFilterResult(PspAudioVmeFilterResult* result);
int PspAudioMe_ValidateVmeResample(u32 count, const s16* inputs,
                                   const s16* coefficients,
                                   const s16* output,
                                   const s16* expectedState,
                                   const s16* actualState,
                                   u32 prepareTicks, u32 scalarTicks);
void PspAudioMe_GetVmeResampleResult(PspAudioVmeResampleResult* result);
void PspAudioMe_GetVmeResampleBenchResult(
    PspAudioVmeResampleBenchResult* result);
void PspAudioMe_GetVmeResampleBatchBenchResult(
    PspAudioVmeResampleBenchResult* result);
void PspAudioMe_GetVmeResampleDmacBenchResult(
    PspAudioVmeResampleBenchResult* result);
u32 PspAudioMe_BenchReadCount(void);
void PspAudioMe_RecordScalarMix(u32 samples, u32 ticks);
void PspAudioMe_GetVmeBenchRow(u32 index, PspAudioVmeBenchRow* result);

#endif
