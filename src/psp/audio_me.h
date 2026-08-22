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

#define PSP_AUDIO_VME_ENV_BENCH_CASES 5
#define PSP_AUDIO_VME_ENV_MAX_SAMPLES 2048
#define PSP_AUDIO_VME_TRANSPORT_BENCH_CASES 5

typedef struct {
    u32 samples;
    u32 calls;
    u32 multiplyMismatches;
    s32 multiplyFirstMismatch;
    s32 multiplyExpected;
    s32 multiplyActual;
    u32 mismatches;
    s32 firstMismatch;
    s32 expected;
    s32 actual;
    s32 bestOffset;
    u32 offsetMismatches;
    u64 scalarTicks;
    u64 vmeTicks;
    u64 validateTicks;
} PspAudioVmeEnvBenchResult;

typedef struct {
    u32 samples;
    u32 calls;
    u32 mismatches;
    u32 mainAddress;
    u32 localAddress;
    s32 firstDomain;
    s32 firstIndex;
    s32 expected;
    s32 actual;
    u64 mainToTicks;
    u64 localToTicks;
    u64 mainFromTicks;
    u64 localFromTicks;
} PspAudioVmeTransportBenchResult;

typedef struct {
    u32 calls;
    u32 samples;
    u32 mismatches;
    s32 firstOutput;
    s32 firstIndex;
    s32 expected;
    s32 actual;
    u64 prepareTicks;
    u64 transferInTicks;
    u64 setupTicks;
    u64 runTicks;
    u64 transferOutTicks;
    u64 materializeTicks;
} PspAudioVmeEnvBoundaryBenchResult;

typedef struct {
    u32 calls;
    u32 voices;
    u32 samples;
    u32 seedMismatches;
    u32 residentMismatches;
    s32 firstStage;
    s32 firstIndex;
    s32 expected;
    s32 actual;
    s32 bestOffset;
    u32 offsetMismatches;
    u32 clampMismatches;
    s32 clampBestOffset;
    u32 clampOffsetMismatches;
    s32 clampMaskBestOffset[4];
    u32 clampMaskOffsetMismatches[4];
    u32 clampPassingMask;
    u32 dryCalls;
    u32 dryVoices;
    u32 drySamples;
    u32 drySeedMismatches[2];
    u32 dryResidentMismatches[2];
    s32 dryFirstLane;
    s32 dryFirstStage;
    s32 dryFirstIndex;
    s32 dryExpected;
    s32 dryActual;
    u32 wetCalls;
    u32 wetVoices;
    u32 wetSamples;
    u32 wetProductMismatches[2];
    u32 wetSeedMismatches[2];
    u32 wetResidentMismatches[2];
    s32 wetFirstLane;
    s32 wetFirstStage;
    s32 wetFirstIndex;
    s32 wetExpected;
    s32 wetActual;
    u64 wetTicks;
} PspAudioVmeEnvPipelineResult;

typedef struct {
    u32 calls;
    u32 voices;
    u32 samples;
    u32 mismatches;
    s32 firstStage;
    s32 firstLane;
    s32 firstIndex;
    s32 expected;
    s32 actual;
    u64 prepareTicks;
    u64 accumulatorInTicks;
    u64 inputTicks;
    u64 rampTicks;
    u64 runTicks;
    u64 outputTicks;
    u64 materializeTicks;
    u64 validateTicks;
} PspAudioVmeEnvRampResult;

typedef struct {
    u32 jobs;
    u32 envCommands;
    u32 compatibleRuns;
    u32 compatibleVoices;
    u32 compatibleSamples;
    u32 maxVoices;
    u32 flaggedCommands;
    u32 destinationBreaks;
    u32 saveBoundaries;
    u32 addBoundaries;
    u32 mixBoundaries;
    u32 clearBoundaries;
    u32 moveBoundaries;
    u32 resampleBoundaries;
    u32 otherBoundaries;
    u32 isolationRuns;
    u32 isolationMismatches;
    u32 segments;
    u32 segmentCommands;
    u32 maxSegmentCommands;
    u32 captureRuns;
    u32 captureVoices;
    u32 captureBytes;
    u32 captureMismatches;
    u32 shadowRuns;
    u32 shadowVoices;
    u32 shadowMismatches;
    u32 shadowFirstLane;
    u32 shadowFirstIndex;
    s32 shadowExpected;
    s32 shadowActual;
    u64 captureTicks;
    u64 scalarTicks;
    u64 shadowWipeTicks;
    u64 shadowStageTicks;
    u64 shadowRunTicks;
    u64 shadowMaterializeTicks;
    u64 shadowValidateTicks;
    u64 shadowTotalTicks;
    u32 commitRuns;
    u32 commitDeclined;
    u32 authoritativeRuns;
    u32 authoritativeDeclined;
    u32 fallbackRuns;
} PspAudioVmeEnvRunResult;

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
void PspAudioMe_GetVmeEnvBenchResult(
    u32 index, PspAudioVmeEnvBenchResult* result);
void PspAudioMe_GetVmeTransportBenchResult(
    u32 index, PspAudioVmeTransportBenchResult* result);
void PspAudioMe_GetVmeEnvBoundaryBenchResult(
    PspAudioVmeEnvBoundaryBenchResult* result);
void PspAudioMe_GetVmeEnvPipelineResult(
    PspAudioVmeEnvPipelineResult* result);
void PspAudioMe_GetVmeEnvRampResult(PspAudioVmeEnvRampResult* result);
void PspAudioMe_GetVmeEnvRunResult(PspAudioVmeEnvRunResult* result);
int PspAudioMe_BeginEnvCapture(const Acmd* commands, s32 commandCount);
void PspAudioMe_CaptureEnvVoice(const s16* input, u32 samples,
                                const u16* volumes, const u16* rates,
                                u16 wetVolume, u16 wetRate,
                                s16* dryLeft, s16* dryRight,
                                s16* wetLeft, s16* wetRight);
void PspAudioMe_EndEnvCapture(void);
void PspAudioMe_RecordEnvCaptureScalar(u32 ticks);
u32 PspAudioMe_BenchReadCount(void);
void PspAudioMe_RecordScalarMix(u32 samples, u32 ticks);
void PspAudioMe_GetVmeBenchRow(u32 index, PspAudioVmeBenchRow* result);

#endif
