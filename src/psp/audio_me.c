#include <pspkernel.h>
#include <pspintrman.h>
#include <pspthreadman.h>

#include "src/psp/audio_me.h"
#include "src/psp/audio_mixer.h"
#include "src/psp/audio_profile.h"

#ifndef PSP_AUDIO
#define PSP_AUDIO 0
#endif
#ifndef PSP_AUDIO_VME
#define PSP_AUDIO_VME 0
#endif
#ifndef PSP_AUDIO_VME_VALIDATE
#define PSP_AUDIO_VME_VALIDATE 0
#endif
#ifndef PSP_AUDIO_VME_BENCH
#define PSP_AUDIO_VME_BENCH 0
#endif

#if PSP_AUDIO
#include <me-core-mapper/me-core.h>
#if PSP_AUDIO_VME
#include <me-core-mapper/vme-lib.h>
#endif

#define PSP_AUDIO_ME_TIMEOUT_US 250000
#define PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US 10000
#define PSP_AUDIO_ME_POLL_US 100
#define PSP_AUDIO_ME_READY 0x100
#define PSP_AUDIO_ME_UNCACHED 0x40000000
#define PSP_AUDIO_ME_CACHE_LINE_SIZE 64
#define PSP_AUDIO_ME_MAX_INPUT_RANGES 16
#define PSP_AUDIO_ME_MAX_WRITE_RANGES 512
#define PSP_AUDIO_VME_SMOKE_SAMPLES 16
#define PSP_AUDIO_VME_SMOKE_PROLOGUE 16
#define PSP_AUDIO_VME_SMOKE_FACTOR -7
#define PSP_AUDIO_VME_SMOKE_REPEATS 4
#define PSP_AUDIO_VME_MIX_MAX_SAMPLES 1024
#define PSP_AUDIO_VME_BUFFER_WORDS 2048
#define PSP_AUDIO_VME_FILTER_TAPS 8
#define PSP_AUDIO_VME_FILTER_OUTPUTS 4
#define PSP_AUDIO_VME_FILTER_COEFF_OFFSET 32
#define PSP_AUDIO_VME_RESAMPLE_LANES 4
#define PSP_AUDIO_VME_RESAMPLE_WORDS 1024
#define PSP_AUDIO_VME_RESAMPLE_COEFF_OFFSET 1024
#define PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES 2048
#define PSP_AUDIO_VME_RESAMPLE_BATCH_REPEATS 4
#define PSP_AUDIO_VME_RESAMPLE_BATCH_STRIDE \
    (PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES + 32)
#define PSP_AUDIO_VME_ENV_BENCH_REPEATS 4
#define PSP_AUDIO_VME_ENV_WET_OFFSET 256
#define PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET 512
#define PSP_AUDIO_VME_TRANSPORT_BENCH_REPEATS 4
#define PSP_AUDIO_VME_BASE_WORD_OFFSET 0
#define PSP_AUDIO_VME_TOP_WORD_OFFSET 0x8000
#define PSP_AUDIO_VME_LANE_WORDS 0x800
#define PSP_AUDIO_ROUND_UP_32(v) (((v) + 31) & ~31)
#define PSP_AUDIO_ROUND_DOWN_16(v) ((v) & ~15)

typedef enum {
    PSP_AUDIO_ME_BOOTING,
    PSP_AUDIO_ME_IDLE,
    PSP_AUDIO_ME_RUN,
    PSP_AUDIO_ME_VME_SMOKE,
    PSP_AUDIO_ME_STOP,
    PSP_AUDIO_ME_HALTED,
    PSP_AUDIO_ME_FAULT,
} PspAudioMeState;

enum {
    PSP_AUDIO_ME_SHARED_STATE,
    PSP_AUDIO_ME_SHARED_COMMANDS,
    PSP_AUDIO_ME_SHARED_COMMAND_COUNT,
    PSP_AUDIO_ME_SHARED_RESULT,
    PSP_AUDIO_ME_SHARED_PROGRESS,
    PSP_AUDIO_ME_SHARED_COMPLETION_ENABLED,
#if PSP_AUDIO_VME
    PSP_AUDIO_ME_SHARED_VME_STATE,
    PSP_AUDIO_ME_SHARED_VME_CHECKPOINT,
    PSP_AUDIO_ME_SHARED_VME_RUNS,
    PSP_AUDIO_ME_SHARED_VME_SAMPLES,
    PSP_AUDIO_ME_SHARED_VME_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_FIRST_INDEX,
    PSP_AUDIO_ME_SHARED_VME_INPUT,
    PSP_AUDIO_ME_SHARED_VME_FACTOR,
    PSP_AUDIO_ME_SHARED_VME_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_ACTUAL,
#if PSP_AUDIO_VME_VALIDATE
    PSP_AUDIO_ME_SHARED_VME_MIX_CALLS,
    PSP_AUDIO_ME_SHARED_VME_MIX_SAMPLES,
    PSP_AUDIO_ME_SHARED_VME_MIX_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_MIX_FIRST_INDEX,
    PSP_AUDIO_ME_SHARED_VME_MIX_INPUT,
    PSP_AUDIO_ME_SHARED_VME_MIX_OLD_OUTPUT,
    PSP_AUDIO_ME_SHARED_VME_MIX_GAIN,
    PSP_AUDIO_ME_SHARED_VME_MIX_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_MIX_ACTUAL,
    PSP_AUDIO_ME_SHARED_VME_FILTER_RUNS,
    PSP_AUDIO_ME_SHARED_VME_FILTER_OUTPUTS,
    PSP_AUDIO_ME_SHARED_VME_FILTER_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_FILTER_FIRST_CASE,
    PSP_AUDIO_ME_SHARED_VME_FILTER_FIRST_INDEX,
    PSP_AUDIO_ME_SHARED_VME_FILTER_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_FILTER_ACTUAL,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_RUNS,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PRODUCTS,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_LANE,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_INDEX,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_INPUT,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_COEFFICIENT,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_ACTUAL,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_COMMANDS,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUTS,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUT_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_STATE_MISMATCHES,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_SKIPPED,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_OUTPUT_INDEX,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUT_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUT_ACTUAL,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_PAIR_INDEX,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR01_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR01_ACTUAL,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR23_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR23_ACTUAL,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_STATE_INDEX,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_STATE_EXPECTED,
    PSP_AUDIO_ME_SHARED_VME_RESAMPLE_STATE_ACTUAL,
#endif
#endif
    PSP_AUDIO_ME_SHARED_COUNT,
};

static volatile u32 sSharedStorage[PSP_AUDIO_ME_SHARED_COUNT]
    __attribute__((aligned(64), section(".uncached")));

#define PSP_AUDIO_ME_SHARED \
    ((volatile u32*) (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) sSharedStorage))
#define sMeState PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_STATE]
#define sMeCommands PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_COMMANDS]
#define sMeCommandCount PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_COMMAND_COUNT]
#define sMeResult PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_RESULT]
#define sMeProgress PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_PROGRESS]
#define sMeCompletionEnabled PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_COMPLETION_ENABLED]
#if PSP_AUDIO_VME
#define sVmeState PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_STATE]
#define sVmeCheckpoint PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_CHECKPOINT]
#define sVmeRuns PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RUNS]
#define sVmeSamples PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_SAMPLES]
#define sVmeMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MISMATCHES]
#define sVmeFirstIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FIRST_INDEX]
#define sVmeInput PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_INPUT]
#define sVmeFactor PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FACTOR]
#define sVmeExpected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_EXPECTED]
#define sVmeActual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_ACTUAL]
#if PSP_AUDIO_VME_VALIDATE
#define sVmeMixCalls PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_CALLS]
#define sVmeMixSamples PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_SAMPLES]
#define sVmeMixMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_MISMATCHES]
#define sVmeMixFirstIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_FIRST_INDEX]
#define sVmeMixInput PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_INPUT]
#define sVmeMixOldOutput PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_OLD_OUTPUT]
#define sVmeMixGain PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_GAIN]
#define sVmeMixExpected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_EXPECTED]
#define sVmeMixActual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_MIX_ACTUAL]
#define sVmeFilterRuns PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_RUNS]
#define sVmeFilterOutputs PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_OUTPUTS]
#define sVmeFilterMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_MISMATCHES]
#define sVmeFilterFirstCase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_FIRST_CASE]
#define sVmeFilterFirstIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_FIRST_INDEX]
#define sVmeFilterExpected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_EXPECTED]
#define sVmeFilterActual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_FILTER_ACTUAL]
#define sVmeResampleRuns PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_RUNS]
#define sVmeResampleProducts PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PRODUCTS]
#define sVmeResampleMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_MISMATCHES]
#define sVmeResampleFirstLane PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_LANE]
#define sVmeResampleFirstIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_INDEX]
#define sVmeResampleInput PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_INPUT]
#define sVmeResampleCoefficient PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_COEFFICIENT]
#define sVmeResampleExpected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_EXPECTED]
#define sVmeResampleActual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_ACTUAL]
#define sVmeResampleCommands PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_COMMANDS]
#define sVmeResampleOutputs PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUTS]
#define sVmeResamplePairMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR_MISMATCHES]
#define sVmeResampleOutputMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUT_MISMATCHES]
#define sVmeResampleStateMismatches PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_STATE_MISMATCHES]
#define sVmeResampleSkipped PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_SKIPPED]
#define sVmeResampleFirstOutputIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_OUTPUT_INDEX]
#define sVmeResampleOutputExpected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUT_EXPECTED]
#define sVmeResampleOutputActual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_OUTPUT_ACTUAL]
#define sVmeResampleFirstPairIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_PAIR_INDEX]
#define sVmeResamplePair01Expected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR01_EXPECTED]
#define sVmeResamplePair01Actual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR01_ACTUAL]
#define sVmeResamplePair23Expected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR23_EXPECTED]
#define sVmeResamplePair23Actual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_PAIR23_ACTUAL]
#define sVmeResampleFirstStateIndex PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_FIRST_STATE_INDEX]
#define sVmeResampleStateExpected PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_STATE_EXPECTED]
#define sVmeResampleStateActual PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_VME_RESAMPLE_STATE_ACTUAL]
static s32 sVmeResamplePair01[PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES]
    __attribute__((aligned(64)));
static s32 sVmeResamplePair23[PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES]
    __attribute__((aligned(64)));
static s16 sVmeResampleOutput[PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES]
    __attribute__((aligned(64)));
#if PSP_AUDIO_VME_BENCH
static s16 sVmeResampleBatchInputs
    [PSP_AUDIO_VME_RESAMPLE_LANES][PSP_AUDIO_VME_RESAMPLE_BATCH_STRIDE]
    __attribute__((aligned(64)));
static s16 sVmeResampleBatchCoefficients
    [PSP_AUDIO_VME_RESAMPLE_LANES][PSP_AUDIO_VME_RESAMPLE_BATCH_STRIDE]
    __attribute__((aligned(64)));
static s16 sVmeResampleBatchExpected[PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES]
    __attribute__((aligned(64)));
static s16 sVmeResampleBatchActual[PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES]
    __attribute__((aligned(64)));
typedef struct {
    s16 input;
    u16 volLeft;
    u16 volRight;
    u16 volWet;
    s16 accumulator[4];
    s16 expected[4];
} PspAudioVmeEnvSample;

static PspAudioVmeEnvSample
    sVmeEnvSamples[PSP_AUDIO_VME_ENV_MAX_SAMPLES]
    __attribute__((aligned(64)));
static s16 sVmeTransportMain[PSP_AUDIO_VME_ENV_MAX_SAMPLES]
    __attribute__((aligned(64)));
static s16 sVmeEnvBoundaryOutput[4][192]
    __attribute__((aligned(64)));
#endif
#endif
#endif

#if PSP_AUDIO_VME_BENCH
static volatile PspAudioVmeBenchRow
    sVmeBenchStorage[PSP_AUDIO_VME_BENCH_ROWS]
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeResampleBenchResult sVmeResampleBenchStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeResampleBenchResult sVmeResampleBatchBenchStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeResampleBenchResult sVmeResampleDmacBenchStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeEnvBenchResult
    sVmeEnvBenchStorage[PSP_AUDIO_VME_ENV_BENCH_CASES]
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeTransportBenchResult
    sVmeTransportBenchStorage[PSP_AUDIO_VME_TRANSPORT_BENCH_CASES]
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeEnvBoundaryBenchResult
    sVmeEnvBoundaryBenchStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile PspAudioVmeEnvPipelineResult
    sVmeEnvPipelineStorage
    __attribute__((aligned(64), section(".uncached")));
#define PSP_AUDIO_VME_BENCH_STATS \
    ((volatile PspAudioVmeBenchRow*) \
        (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) sVmeBenchStorage))
#define PSP_AUDIO_VME_RESAMPLE_BENCH_STATS \
    ((volatile PspAudioVmeResampleBenchResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) &sVmeResampleBenchStorage))
#define PSP_AUDIO_VME_RESAMPLE_BATCH_BENCH_STATS \
    ((volatile PspAudioVmeResampleBenchResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) &sVmeResampleBatchBenchStorage))
#define PSP_AUDIO_VME_RESAMPLE_DMAC_BENCH_STATS \
    ((volatile PspAudioVmeResampleBenchResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) &sVmeResampleDmacBenchStorage))
#define PSP_AUDIO_VME_ENV_BENCH_STATS \
    ((volatile PspAudioVmeEnvBenchResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) sVmeEnvBenchStorage))
#define PSP_AUDIO_VME_TRANSPORT_BENCH_STATS \
    ((volatile PspAudioVmeTransportBenchResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) sVmeTransportBenchStorage))
#define PSP_AUDIO_VME_ENV_BOUNDARY_BENCH_STATS \
    ((volatile PspAudioVmeEnvBoundaryBenchResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) &sVmeEnvBoundaryBenchStorage))
#define PSP_AUDIO_VME_ENV_PIPELINE_STATS \
    ((volatile PspAudioVmeEnvPipelineResult*) \
        (PSP_AUDIO_ME_UNCACHED | \
         (u32) (uintptr_t) &sVmeEnvPipelineStorage))
#endif

#if PSP_AUDIO_VME && PSP_AUDIO_VME_VALIDATE && PSP_AUDIO_VME_BENCH
typedef struct {
    u32 count;
} PspAudioVmeEnvContextParam;

typedef struct {
    u32 count;
    s32 gain;
    u32 secondaryMask;
} PspAudioVmeEnvPipelineContextParam;

typedef struct {
    u32 count;
    s32 gainLeft;
    s32 gainRight;
} PspAudioVmeEnvDryContextParam;

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_pipeline_context, value, {
    PspAudioVmeEnvPipelineContextParam* param = value;
    u32 count = param->count - 1;
    u32 multiply = VME_FU_OPCODE_MUL_CONST_RSHIFT_0;
    u32 add = VME_FU_OPCODE_ADD_IBUF_RSHIFT;
    u32 clamp = VME_FU_OPCODE_CLAMP_BACK;

    vme_set(ENABLE, FU_1, param->secondaryMask << 28);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, VME_DEF_MAPPER);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0), multiply, 16);
    vme_pe0(fu_reg(PRIMARY, B), (u32) param->gain);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE, vme_cyc(8));
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe2(vme_fu(PRIMARY), vme_mux(BASE_2, BASE_0), add, 0);
    vme_pe2(vme_fu(SECONDARY), vme_mux(NONE, STAGING_2), clamp);
    vme_pe2(fu_reg(SECONDARY, A), 32767);
    vme_pe2(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe2(agu_base(MODE), VME_DEF_MODE, vme_cyc(8));
    vme_pe2(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe2(agu_write(MODE), VME_DEF_MODE, vme_cyc(17));
    vme_pe2(agu_write(COUNT), VME_DEF_STEP, count);
});

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_dry_context, value, {
    PspAudioVmeEnvDryContextParam* param = value;
    u32 count = param->count - 1;
    u32 multiply = VME_FU_OPCODE_MUL_CONST_RSHIFT_0;
    u32 add = VME_FU_OPCODE_ADD_IBUF_RSHIFT;
    u32 clamp = VME_FU_OPCODE_CLAMP_BACK;

    vme_set(ENABLE, FU_1, 0x0f << 28);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, VME_DEF_MAPPER);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0), multiply, 16);
    vme_pe0(fu_reg(PRIMARY, B), (u32) param->gainLeft);
    vme_pe0(vme_fu(SECONDARY), vme_mux(NONE, STAGING_0), clamp);
    vme_pe0(fu_reg(SECONDARY, A), 32767);
    vme_pe0(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE, vme_cyc(11));
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);

    vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_0), multiply, 16);
    vme_pe1(fu_reg(PRIMARY, B), (u32) param->gainRight);
    vme_pe1(vme_fu(SECONDARY), vme_mux(NONE, STAGING_1), clamp);
    vme_pe1(fu_reg(SECONDARY, A), 32767);
    vme_pe1(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe1(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9);
    vme_pe1(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe1(agu_base(MODE), VME_DEF_MODE, vme_cyc(11));
    vme_pe1(agu_base(COUNT), VME_DEF_STEP, count);

    vme_pe2(vme_fu(PRIMARY), vme_mux(BASE_2, BASE_0), add, 0);
    vme_pe2(vme_fu(SECONDARY), vme_mux(NONE, STAGING_2), clamp);
    vme_pe2(fu_reg(SECONDARY, A), 32767);
    vme_pe2(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe2(agu_base(MODE), VME_DEF_MODE, vme_cyc(11));
    vme_pe2(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe2(agu_write(MODE), VME_DEF_MODE, vme_cyc(20));
    vme_pe2(agu_write(COUNT), VME_DEF_STEP, count);

    vme_pe3(vme_fu(PRIMARY), vme_mux(BASE_3, BASE_1), add, 0);
    vme_pe3(vme_fu(SECONDARY), vme_mux(NONE, STAGING_3), clamp);
    vme_pe3(fu_reg(SECONDARY, A), 32767);
    vme_pe3(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe3(agu_base(MODE), VME_DEF_MODE, vme_cyc(11));
    vme_pe3(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe3(agu_write(MODE), VME_DEF_MODE, vme_cyc(20));
    vme_pe3(agu_write(COUNT), VME_DEF_STEP, count);
});

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_wet_multiply_context, value, {
    PspAudioVmeEnvPipelineContextParam* param = value;
    u32 count = param->count - 1;
    u32 multiply = VME_FU_OPCODE_MUL_CONST_RSHIFT_0;

    vme_set(ENABLE, FU_1, 0);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, VME_DEF_MAPPER);
    vme_pe0(vme_fu(PRIMARY), vme_mux(NONE, BASE_0), multiply, 16);
    vme_pe0(fu_reg(PRIMARY, B), (u32) param->gain);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6,
            PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe1(vme_fu(PRIMARY), vme_mux(NONE, BASE_1), multiply, 16);
    vme_pe1(fu_reg(PRIMARY, B), (u32) param->gain);
    vme_pe1(agu_base(MODE), VME_DEF_MODE);
    vme_pe1(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe1(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6,
            PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET);
    vme_pe1(agu_write(COUNT), VME_DEF_STEP, count);
});

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_wet_add_context, value, {
    PspAudioVmeEnvContextParam* param = value;
    u32 count = param->count - 1;
    u32 add = VME_FU_OPCODE_ADD_IBUF_RSHIFT;
    u32 clamp = VME_FU_OPCODE_CLAMP_BACK;

    vme_set(ENABLE, FU_1, 0x0f << 28);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, VME_DEF_MAPPER);
    vme_pe0(vme_fu(PRIMARY), vme_mux(NONE, BASE_0), add, 0);
    vme_pe0(vme_fu(SECONDARY), vme_mux(NONE, STAGING_0), clamp);
    vme_pe0(fu_reg(SECONDARY, A), 32767);
    vme_pe0(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe0(agu_base(MODE), VME_DEF_MODE, 0,
            PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe1(vme_fu(PRIMARY), vme_mux(NONE, BASE_1), add, 0);
    vme_pe1(vme_fu(SECONDARY), vme_mux(NONE, STAGING_1), clamp);
    vme_pe1(fu_reg(SECONDARY, A), 32767);
    vme_pe1(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe1(agu_base(MODE), VME_DEF_MODE, 0,
            PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET);
    vme_pe1(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe1(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9);
    vme_pe1(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe2(vme_fu(PRIMARY), vme_mux(BASE_2, BASE_0), add, 0);
    vme_pe2(vme_fu(SECONDARY), vme_mux(NONE, STAGING_2), clamp);
    vme_pe2(fu_reg(SECONDARY, A), 32767);
    vme_pe2(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe2(agu_base(MODE), VME_DEF_MODE, 0,
            PSP_AUDIO_VME_ENV_WET_OFFSET);
    vme_pe2(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe2(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9,
            PSP_AUDIO_VME_ENV_WET_OFFSET);
    vme_pe2(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe3(vme_fu(PRIMARY), vme_mux(BASE_3, BASE_1), add, 0);
    vme_pe3(vme_fu(SECONDARY), vme_mux(NONE, STAGING_3), clamp);
    vme_pe3(fu_reg(SECONDARY, A), 32767);
    vme_pe3(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe3(agu_base(MODE), VME_DEF_MODE, 0,
            PSP_AUDIO_VME_ENV_WET_OFFSET);
    vme_pe3(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe3(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9,
            PSP_AUDIO_VME_ENV_WET_OFFSET);
    vme_pe3(agu_write(COUNT), VME_DEF_STEP, count);
});

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_clamp_context, value, {
    PspAudioVmeEnvPipelineContextParam* param = value;
    u32 count = param->count - 1;
    u32 multiply = VME_FU_OPCODE_MUL_CONST_RSHIFT_0;
    u32 add = VME_FU_OPCODE_ADD_IBUF_RSHIFT;
    u32 clamp = VME_FU_OPCODE_CLAMP_BACK;

    vme_set(ENABLE, FU_1, param->secondaryMask << 28);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, VME_DEF_MAPPER);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0), multiply, 16);
    vme_pe0(fu_reg(PRIMARY, B), (u32) param->gain);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE, vme_cyc(8));
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe1(agu_base(MODE), VME_DEF_MODE, vme_cyc(8));
    vme_pe1(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe2(vme_fu(PRIMARY), vme_mux(BASE_1, BASE_0), add, 0);
    vme_pe2(vme_fu(SECONDARY), vme_mux(NONE, STAGING_2), clamp);
    vme_pe2(fu_reg(SECONDARY, A), 32767);
    vme_pe2(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe2(agu_write(MODE), VME_DEF_MODE, vme_cyc(17));
    vme_pe2(agu_write(COUNT), VME_DEF_STEP, count);
});

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_multiply_context, value, {
    PspAudioVmeEnvContextParam* param = value;
    u32 count = param->count - 1;
    u32 multiply = VME_FU_OPCODE_MUL_VEC_RSHIFT;

    vme_set(ENABLE, FU_1, 0);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, BASE_0), multiply, 16);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_1, BASE_1), multiply, 16);
    vme_pe2(vme_fu(PRIMARY), vme_mux(TOP_2, BASE_2), multiply, 16);
    vme_pe3(vme_fu(PRIMARY), vme_mux(TOP_3, BASE_3), multiply, 16);
});

VME_LIB_CONTEXT_BUILDER(psp_audio_me_vme_env_add_context, value, {
    PspAudioVmeEnvContextParam* param = value;
    u32 count = param->count - 1;
    u32 add = VME_FU_OPCODE_ADD_IBUF_RSHIFT;
    u32 clamp = VME_FU_OPCODE_CLAMP_BACK;

    vme_set(ENABLE, FU_1, 0xf << 28);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, BASE_0), add, 0);
    vme_pe0(vme_fu(SECONDARY), vme_mux(NONE, STAGING_0), clamp);
    vme_pe0(fu_reg(SECONDARY, A), 32767);
    vme_pe0(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);
    vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_1, BASE_1), add, 0);
    vme_pe1(vme_fu(SECONDARY), vme_mux(NONE, STAGING_1), clamp);
    vme_pe1(fu_reg(SECONDARY, A), 32767);
    vme_pe1(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe2(vme_fu(PRIMARY), vme_mux(TOP_2, BASE_2), add, 0);
    vme_pe2(vme_fu(SECONDARY), vme_mux(NONE, STAGING_2), clamp);
    vme_pe2(fu_reg(SECONDARY, A), 32767);
    vme_pe2(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe3(vme_fu(PRIMARY), vme_mux(TOP_3, BASE_3), add, 0);
    vme_pe3(vme_fu(SECONDARY), vme_mux(NONE, STAGING_3), clamp);
    vme_pe3(fu_reg(SECONDARY, A), 32767);
    vme_pe3(fu_reg(SECONDARY, B), (u32) (s32) -32768);
});
#endif

typedef struct {
    void* address;
    u32 size;
} PspAudioMeCacheRange;

static const Acmd* sPendingCommands;
static s32 sPendingCommandCount;
static u32 sPendingStart;
static s32 sBootResult;
static s32 sBootStarted;
static s32 sInitialized;
static s32 sPending;
static s32 sLastError;
#if PSP_AUDIO_VME
static s32 sVmeTimedOut;
#endif
static SceUID sCompletionSema = -1;
static s32 sCompletionReady;
static PspAudioMeCacheRange sInputRanges[PSP_AUDIO_ME_MAX_INPUT_RANGES];
static u32 sInputRangeCount;
static s32 sInputRangeOverflow;
static PspAudioMeCacheRange sWriteRanges[PSP_AUDIO_ME_MAX_WRITE_RANGES];
static u32 sWriteRangeCount;
static s32 sWriteRangeOverflow;

static void psp_audio_me_completion_interrupt(int subIntr, void* arg) {
    (void) subIntr;
    (void) arg;
    if (sCompletionSema > 0) {
        sceKernelSignalSema(sCompletionSema, 1);
    }
}

static void psp_audio_me_drain_completion(void) {
    if (!sCompletionReady) {
        return;
    }
    while (sceKernelPollSema(sCompletionSema, 1) == 0) {
    }
}

static void psp_audio_me_enable_completion(void) {
    int result;

    if (sCompletionReady) {
        return;
    }
    sCompletionSema = sceKernelCreateSema("sf64_audio_me_done", 0, 0, 1, NULL);
    if (sCompletionSema <= 0) {
        sCompletionSema = -1;
        return;
    }
    result = sceKernelRegisterSubIntrHandler(PSP_MECODEC_INT, 0, psp_audio_me_completion_interrupt, NULL);
    if (result < 0) {
        sceKernelDeleteSema(sCompletionSema);
        sCompletionSema = -1;
        return;
    }
    result = sceKernelEnableSubIntr(PSP_MECODEC_INT, 0);
    if (result < 0) {
        sceKernelReleaseSubIntrHandler(PSP_MECODEC_INT, 0);
        sceKernelDeleteSema(sCompletionSema);
        sCompletionSema = -1;
        return;
    }
    sMeCompletionEnabled = 1;
    meLibSync();
    sCompletionReady = 1;
}

static u32 psp_audio_me_command_dma_size(u32 w0) {
    return ((w0 >> 16) & 0xFF) << 4;
}

static void psp_audio_me_reset_ranges(void) {
    sInputRangeCount = 0;
    sInputRangeOverflow = false;
    sWriteRangeCount = 0;
    sWriteRangeOverflow = false;
}

static void psp_audio_me_record_range(PspAudioMeCacheRange* ranges, u32* count, s32* overflow,
                                      u32 capacity, void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;
    u32 i;

    if ((address == NULL) || (size == 0) || *overflow) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    for (i = 0; i < *count; i++) {
        uintptr_t rangeStart = (uintptr_t) ranges[i].address;
        uintptr_t rangeEnd = rangeStart + ranges[i].size;

        if ((start <= rangeEnd) && (end >= rangeStart)) {
            if (start < rangeStart) {
                rangeStart = start;
            }
            if (end < rangeEnd) {
                end = rangeEnd;
            }
            ranges[i].address = (void*) rangeStart;
            ranges[i].size = end - rangeStart;
            return;
        }
    }
    if (*count >= capacity) {
        *overflow = true;
        return;
    }
    ranges[*count].address = (void*) start;
    ranges[*count].size = end - start;
    (*count)++;
}

static void psp_audio_me_record_input(const void* address, u32 size) {
    psp_audio_me_record_range(sInputRanges, &sInputRangeCount, &sInputRangeOverflow,
                              PSP_AUDIO_ME_MAX_INPUT_RANGES, (void*) address, size);
}

static void psp_audio_me_record_write(void* address, u32 size) {
    psp_audio_me_record_range(sWriteRanges, &sWriteRangeCount, &sWriteRangeOverflow,
                              PSP_AUDIO_ME_MAX_WRITE_RANGES, address, size);
}

static void psp_audio_me_collect_ranges(const Acmd* commands, s32 commandCount) {
    s32 i;

    psp_audio_me_reset_ranges();
    psp_audio_me_record_input(commands, commandCount * sizeof(*commands));
    for (i = 0; i < commandCount; i++) {
        u32 w0 = commands[i].words.w0;
        u32 w1 = commands[i].words.w1;

        switch (w0 >> 24) {
            case A_ADPCM:
            case A_RESAMPLE:
            case A_S8DEC:
                psp_audio_me_record_input((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                psp_audio_me_record_write((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            case A_FILTER:
                if (((w0 >> 16) & 0xFF) > A_INIT) {
                    psp_audio_me_record_input((void*) (uintptr_t) w1, 8 * sizeof(s16));
                } else {
                    psp_audio_me_record_input((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                    psp_audio_me_record_write((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                }
                break;
            case A_LOADADPCM:
                psp_audio_me_record_input((void*) (uintptr_t) w1, w0 & 0xFFFFFF);
                break;
            case A_LOADBUFF:
                psp_audio_me_record_input((void*) (uintptr_t) w1, psp_audio_me_command_dma_size(w0));
                break;
            case A_SAVEBUFF:
                psp_audio_me_record_input((void*) (uintptr_t) w1, psp_audio_me_command_dma_size(w0));
                psp_audio_me_record_write((void*) (uintptr_t) w1, psp_audio_me_command_dma_size(w0));
                break;
            case A_SETLOOP:
                psp_audio_me_record_input((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            default:
                break;
        }
    }
}

static void psp_audio_me_writeback_inputs(void) {
    u32 i;

    if (sInputRangeOverflow) {
        sceKernelDcacheWritebackAll();
        return;
    }
    for (i = 0; i < sInputRangeCount; i++) {
        sceKernelDcacheWritebackRange(sInputRanges[i].address, sInputRanges[i].size);
    }
}

static void psp_audio_me_invalidate_writes(void) {
    u32 i;

    if (sWriteRangeOverflow) {
        sceKernelDcacheWritebackInvalidateAll();
    } else {
        for (i = 0; i < sWriteRangeCount; i++) {
            sceKernelDcacheInvalidateRange(sWriteRanges[i].address, sWriteRanges[i].size);
        }
    }
    psp_audio_me_reset_ranges();
}

static void psp_audio_me_writeback_range_cpu(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    sceKernelDcacheWritebackRange((void*) start, end - start);
}

static void psp_audio_me_invalidate_range_cpu(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    sceKernelDcacheInvalidateRange((void*) start, end - start);
}

static void psp_audio_me_invalidate_range_me(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    meLibDcacheInvalidateRange((u32) start, end - start);
}

#if !defined(PSP_LOG_ENABLED) || !PSP_LOG_ENABLED
static void psp_audio_me_writeback_range_me(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    meLibDcacheWritebackRange((u32) start, end - start);
}
#endif

#if !defined(PSP_LOG_ENABLED) || !PSP_LOG_ENABLED
static void psp_audio_me_invalidate_inputs_me(const Acmd* commands, s32 commandCount) {
    s32 i;

    psp_audio_me_invalidate_range_me(commands, commandCount * sizeof(*commands));
    for (i = 0; i < commandCount; i++) {
        u32 w0 = commands[i].words.w0;
        u32 w1 = commands[i].words.w1;

        switch (w0 >> 24) {
            case A_ADPCM:
            case A_RESAMPLE:
            case A_S8DEC:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            case A_FILTER:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1,
                                                  ((w0 >> 16) & 0xFF) > A_INIT ?
                                                      8 * sizeof(s16) : sizeof(ADPCM_STATE));
                break;
            case A_LOADADPCM:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1, w0 & 0xFFFFFF);
                break;
            case A_LOADBUFF:
            case A_SAVEBUFF:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1,
                                                  psp_audio_me_command_dma_size(w0));
                break;
            case A_SETLOOP:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            default:
                break;
        }
    }
}

static void psp_audio_me_writeback_outputs_me(const Acmd* commands, s32 commandCount) {
    s32 i;

    for (i = 0; i < commandCount; i++) {
        u32 w0 = commands[i].words.w0;
        u32 w1 = commands[i].words.w1;

        switch (w0 >> 24) {
            case A_ADPCM:
            case A_RESAMPLE:
            case A_S8DEC:
                psp_audio_me_writeback_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            case A_FILTER:
                if (((w0 >> 16) & 0xFF) <= A_INIT) {
                    psp_audio_me_writeback_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                }
                break;
            case A_SAVEBUFF:
                psp_audio_me_writeback_range_me((void*) (uintptr_t) w1,
                                                 psp_audio_me_command_dma_size(w0));
                break;
            default:
                break;
        }
    }
    psp_audio_me_writeback_range_me(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
}
#endif

#if PSP_AUDIO_VME
#if PSP_AUDIO_VME_VALIDATE
static int psp_audio_me_vme_mix_synthetic(void);
static int psp_audio_me_vme_filter_synthetic(void);
static int psp_audio_me_vme_resample_synthetic(void);
#if PSP_AUDIO_VME_BENCH
static int psp_audio_me_vme_resample_batch_bench(void);
static int psp_audio_me_vme_env_bench(void);
static int psp_audio_me_vme_transport_bench(void);
static int psp_audio_me_vme_env_boundary_bench(void);
static int psp_audio_me_vme_env_pipeline_proof(void);
static int psp_audio_me_vme_env_dry_proof(void);
static int psp_audio_me_vme_env_wet_proof(void);
static int psp_audio_me_vme_env_clamp_proof(void);
#endif
#endif

static int psp_audio_me_vme_smoke_once(void) {
    static const s16 sInputs[PSP_AUDIO_VME_SMOKE_SAMPLES] = {
        0, 1, -1, 0x7fff, -0x8000, 2, -2, 17,
        -17, 255, -255, 4096, -4096, 16384, -16384, 30000,
    };
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* products = (volatile s32*) VME_BASE_BUFFERS;
    const s32 count = PSP_AUDIO_VME_SMOKE_SAMPLES + PSP_AUDIO_VME_SMOKE_PROLOGUE;
    s32 i;

    for (i = 0; i < PSP_AUDIO_VME_SMOKE_SAMPLES; i++) {
        top[i] = sInputs[i];
    }
    for (; i < count; i++) {
        top[i] = 0;
    }

    sVmeCheckpoint = 6;
    meLibSync();
    sVmeCheckpoint = 7;
    vmeLibStart();
    vme_set(ENABLE, FU_1, 0);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0), VME_FU_OPCODE_MUL_CONST_RSHIFT_0, 0);
    vme_pe0(fu_reg(PRIMARY, B), (u32) (s32) PSP_AUDIO_VME_SMOKE_FACTOR);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(FORMAT_0), PSP_AUDIO_VME_SMOKE_PROLOGUE);
    vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
    vmeLibFinish();
    sVmeCheckpoint = 8;
    meLibSync();

    products += PSP_AUDIO_VME_SMOKE_PROLOGUE;
    sVmeRuns++;
    for (i = 0; i < PSP_AUDIO_VME_SMOKE_SAMPLES; i++) {
        s32 expected = (s32) sInputs[i] * PSP_AUDIO_VME_SMOKE_FACTOR;
        s32 actual = products[i];

        sVmeSamples++;
        if (actual != expected) {
            if (sVmeMismatches == 0) {
                sVmeFirstIndex = i;
                sVmeInput = (u32) (s32) sInputs[i];
                sVmeFactor = (u32) (s32) PSP_AUDIO_VME_SMOKE_FACTOR;
                sVmeExpected = (u32) expected;
                sVmeActual = (u32) actual;
            }
            sVmeMismatches++;
        }
    }
    sVmeCheckpoint = 9;
    meLibSync();
    return sVmeMismatches == 0 ? 0 : -1;
}

static int psp_audio_me_vme_smoke(void) {
    s32 run;

    sVmeState = PSP_AUDIO_VME_INITIALIZING;
    sVmeCheckpoint = 1;
    sVmeRuns = 0;
    sVmeSamples = 0;
    sVmeMismatches = 0;
    sVmeFirstIndex = (u32) -1;
    sVmeInput = 0;
    sVmeFactor = PSP_AUDIO_VME_SMOKE_FACTOR;
    sVmeExpected = 0;
    sVmeActual = 0;
    meLibSync();
    meLibExceptionHandlerInit(0);
    sVmeCheckpoint = 2;
    meLibSync();

    for (run = 0; run < PSP_AUDIO_VME_SMOKE_REPEATS; run++) {
        sVmeCheckpoint = 3;
        meLibSync();
        vmeLibEnable();
        sVmeCheckpoint = 4;
        meLibSync();
        vmeLibWipe();
        sVmeCheckpoint = 5;
        meLibSync();
        if (psp_audio_me_vme_smoke_once() < 0) {
            vmeLibDisable();
            sVmeState = PSP_AUDIO_VME_FAULT;
            meLibSync();
            return -1;
        }
        if (run + 1 < PSP_AUDIO_VME_SMOKE_REPEATS) {
            sVmeCheckpoint = 10;
            meLibSync();
            vmeLibDisable();
            sVmeCheckpoint = 11;
            meLibSync();
        }
    }

    sVmeCheckpoint = 12;
    sVmeState = PSP_AUDIO_VME_READY;
    meLibSync();
#if PSP_AUDIO_VME_VALIDATE
    if (psp_audio_me_vme_mix_synthetic() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_filter_synthetic() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_resample_synthetic() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
#if PSP_AUDIO_VME_BENCH
    if (psp_audio_me_vme_resample_batch_bench() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_transport_bench() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_env_boundary_bench() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_env_pipeline_proof() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_env_clamp_proof() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_env_dry_proof() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_env_wet_proof() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    if (psp_audio_me_vme_env_bench() < 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
#endif
#endif
    return 0;
}
#endif

#if PSP_AUDIO_VME_VALIDATE
static s16 sVmeMixOutput[PSP_AUDIO_VME_MIX_MAX_SAMPLES]
    __attribute__((aligned(64)));
#if PSP_AUDIO_VME_BENCH
static u32 sVmeBenchReadOverhead;
static volatile PspAudioVmeBenchRow* sVmeBenchLastRow;
#endif

static s16 psp_audio_me_clamp16(s32 value) {
    if (value < -0x8000) {
        return -0x8000;
    }
    if (value > 0x7fff) {
        return 0x7fff;
    }
    return (s16) value;
}

u32 PspAudioMe_BenchReadCount(void) {
#if PSP_AUDIO_VME_BENCH
    u32 count;

    __asm__ volatile("mfc0 %0, $9" : "=r"(count));
    return count;
#else
    return 0;
#endif
}

#if PSP_AUDIO_VME_BENCH
static u32 psp_audio_me_bench_elapsed(u32 start) {
    u32 elapsed = PspAudioMe_BenchReadCount() - start;

    return elapsed > sVmeBenchReadOverhead ?
           elapsed - sVmeBenchReadOverhead : 0;
}

static void psp_audio_me_bench_reset(void) {
    volatile u32* words = (volatile u32*) PSP_AUDIO_VME_BENCH_STATS;
    volatile u32* resampleWords =
        (volatile u32*) PSP_AUDIO_VME_RESAMPLE_BENCH_STATS;
    volatile u32* batchWords =
        (volatile u32*) PSP_AUDIO_VME_RESAMPLE_BATCH_BENCH_STATS;
    volatile u32* dmacWords =
        (volatile u32*) PSP_AUDIO_VME_RESAMPLE_DMAC_BENCH_STATS;
    volatile u32* envWords =
        (volatile u32*) PSP_AUDIO_VME_ENV_BENCH_STATS;
    volatile u32* transportWords =
        (volatile u32*) PSP_AUDIO_VME_TRANSPORT_BENCH_STATS;
    volatile u32* boundaryWords =
        (volatile u32*) PSP_AUDIO_VME_ENV_BOUNDARY_BENCH_STATS;
    volatile u32* pipelineWords =
        (volatile u32*) PSP_AUDIO_VME_ENV_PIPELINE_STATS;
    u32 count = (sizeof(sVmeBenchStorage) / sizeof(sVmeBenchStorage[0])) *
                (sizeof(sVmeBenchStorage[0]) / sizeof(u32));
    u32 envWordCount = sizeof(sVmeEnvBenchStorage);
    u32 transportWordCount = sizeof(sVmeTransportBenchStorage);
    u32 best = 0xffffffffU;
    u32 i;

    for (i = 0; i < count; i++) {
        words[i] = 0;
    }
    for (i = 0;
         i < sizeof(*PSP_AUDIO_VME_RESAMPLE_BENCH_STATS) / sizeof(u32);
         i++) {
        resampleWords[i] = 0;
        batchWords[i] = 0;
        dmacWords[i] = 0;
    }
    envWordCount /= sizeof(*envWords);
    for (i = 0; i < envWordCount; i++) {
        envWords[i] = 0;
    }
    transportWordCount /= sizeof(*transportWords);
    for (i = 0; i < transportWordCount; i++) {
        transportWords[i] = 0;
    }
    for (i = 0;
         i < sizeof(*PSP_AUDIO_VME_ENV_BOUNDARY_BENCH_STATS) / sizeof(u32);
         i++) {
        boundaryWords[i] = 0;
    }
    for (i = 0;
         i < sizeof(*PSP_AUDIO_VME_ENV_PIPELINE_STATS) / sizeof(u32);
         i++) {
        pipelineWords[i] = 0;
    }
    for (i = 0; i < 32; i++) {
        u32 start = PspAudioMe_BenchReadCount();
        u32 elapsed = PspAudioMe_BenchReadCount() - start;

        if (elapsed < best) {
            best = elapsed;
        }
    }
    sVmeBenchReadOverhead = best;
    sVmeBenchLastRow = NULL;
}

static volatile PspAudioVmeBenchRow* psp_audio_me_bench_row(u32 samples) {
    volatile PspAudioVmeBenchRow* empty = NULL;
    u32 i;

    for (i = 0; i < PSP_AUDIO_VME_BENCH_ROWS; i++) {
        volatile PspAudioVmeBenchRow* row = &PSP_AUDIO_VME_BENCH_STATS[i];

        if (row->samples == samples) {
            return row;
        }
        if ((empty == NULL) && (row->samples == 0)) {
            empty = row;
        }
    }
    if (empty != NULL) {
        empty->samples = samples;
    }
    return empty;
}
#endif

static void psp_audio_me_vme_multiply(s32 factor, s32 count,
                                      u32* updateTicks, u32* runTicks) {
#if PSP_AUDIO_VME_BENCH
    u32 start = PspAudioMe_BenchReadCount();
#else
    (void) updateTicks;
    (void) runTicks;
#endif

    vmeLibStart();
    vme_icn(FLOW, 0);
    vme_icn(ARCH, VME_DEF_MAPPER);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0),
            VME_FU_OPCODE_MUL_CONST_RSHIFT_0, 0);
    vme_pe0(fu_reg(PRIMARY, B), (u32) factor);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(FORMAT_0), PSP_AUDIO_VME_SMOKE_PROLOGUE);
    vme_pe0(agu_write(FORMAT_1), VME_END_TOKEN);
#if PSP_AUDIO_VME_BENCH
    *updateTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
    vmeLibFinish();
    meLibSync();
    *runTicks += psp_audio_me_bench_elapsed(start);
#else
    vmeLibFinish();
    meLibSync();
#endif
}

int PspAudioMe_ValidateVmeMix(u16 count, s16 gain, const s16* input,
                              const s16* output) {
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* products = (volatile s32*) VME_BASE_BUFFERS;
    volatile s32* highProducts = top + PSP_AUDIO_VME_BUFFER_WORDS;
    s16* vmeOutput = sVmeMixOutput;
    s32 nbytes = PSP_AUDIO_ROUND_UP_32(PSP_AUDIO_ROUND_DOWN_16((u32) count << 4));
    s32 samples = nbytes / (s32) sizeof(s16);
    s32 vmeCount = samples + PSP_AUDIO_VME_SMOKE_PROLOGUE;
    s32 lowFactor = (u16) gain & 0xff;
    s32 highFactor = ((s32) gain - lowFactor) / 256;
#if PSP_AUDIO_VME_BENCH
    volatile PspAudioVmeBenchRow* benchRow = psp_audio_me_bench_row(samples);
    u32 stageTicks;
    u32 updateTicks = 0;
    u32 runTicks = 0;
    u32 readbackTicks = 0;
    u32 postTicks;
    u32 start;
#else
    u32* updateTicks = NULL;
    u32* runTicks = NULL;
#endif
    s32 i;

    if ((sVmeState != PSP_AUDIO_VME_READY) || (input == NULL) ||
        (output == NULL) || (samples <= 0) ||
        (samples > PSP_AUDIO_VME_MIX_MAX_SAMPLES)) {
        return 0;
    }

#if PSP_AUDIO_VME_BENCH
    start = PspAudioMe_BenchReadCount();
#endif
    {
        for (i = 0; i < samples; i++) {
            top[i] = input[i];
        }
    }
    for (; i < vmeCount; i++) {
        top[i] = 0;
    }
    meLibSync();
#if PSP_AUDIO_VME_BENCH
    stageTicks = psp_audio_me_bench_elapsed(start);
#endif
    psp_audio_me_vme_multiply(highFactor, vmeCount,
#if PSP_AUDIO_VME_BENCH
                              &updateTicks, &runTicks);
#else
                              updateTicks, runTicks);
#endif

    products += PSP_AUDIO_VME_SMOKE_PROLOGUE;
#if PSP_AUDIO_VME_BENCH
    start = PspAudioMe_BenchReadCount();
#endif
    for (i = 0; i < samples; i++) {
        highProducts[i] = products[i];
    }
#if PSP_AUDIO_VME_BENCH
    readbackTicks += psp_audio_me_bench_elapsed(start);
#endif
    psp_audio_me_vme_multiply(lowFactor, vmeCount,
#if PSP_AUDIO_VME_BENCH
                              &updateTicks, &runTicks);
#else
                              updateTicks, runTicks);
#endif

    sVmeMixCalls++;
#if PSP_AUDIO_VME_BENCH
    start = PspAudioMe_BenchReadCount();
#endif
    for (i = 0; i < samples; i++) {
        s32 product = highProducts[i] * 256 + products[i];

        if (gain == -0x8000) {
            vmeOutput[i] = psp_audio_me_clamp16(
                (s32) output[i] + (product >> 15));
        } else {
            vmeOutput[i] = psp_audio_me_clamp16(
                (((s32) output[i] * 0x7fff + product) + 0x4000) >> 15);
        }
    }
#if PSP_AUDIO_VME_BENCH
    postTicks = psp_audio_me_bench_elapsed(start);
    if (benchRow != NULL) {
        benchRow->calls++;
        benchRow->stageTicks += stageTicks;
        benchRow->updateTicks += updateTicks;
        benchRow->runTicks += runTicks;
        benchRow->readbackTicks += readbackTicks;
        benchRow->postTicks += postTicks;
        sVmeBenchLastRow = benchRow;
    }
#endif
    for (i = 0; i < samples; i++) {
        s16 expected;
        s16 actual = vmeOutput[i];

        if (gain == -0x8000) {
            expected = psp_audio_me_clamp16((s32) output[i] - input[i]);
        } else {
            expected = psp_audio_me_clamp16(
                (((s32) output[i] * 0x7fff + (s32) input[i] * gain) + 0x4000) >> 15);
        }
        sVmeMixSamples++;
        if (actual != expected) {
            if (sVmeMixMismatches == 0) {
                sVmeMixFirstIndex = i;
                sVmeMixInput = (u32) (s32) input[i];
                sVmeMixOldOutput = (u32) (s32) output[i];
                sVmeMixGain = (u32) (s32) gain;
                sVmeMixExpected = (u32) (s32) expected;
                sVmeMixActual = (u32) (s32) actual;
            }
            sVmeMixMismatches++;
        }
    }
    if (sVmeMixMismatches != 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    return 1;
}

static int psp_audio_me_vme_mix_synthetic(void) {
    static const s16 sInput[16] = {
        0, 1, -1, 0x7fff, -0x8000, 2, -2, 17,
        -17, 255, -255, 4096, -4096, 16384, -16384, 30000,
    };
    static const s16 sOutput[16] = {
        0x7fff, -0x8000, 1, -1, 0, 30000, -30000, 17,
        -17, 255, -255, 4096, -4096, 16384, -16384, 0,
    };
    static const s16 sGains[4] = { 0, 0x7fff, -0x7fff, -0x8000 };
    s32 i;

    for (i = 0; i < 4; i++) {
        if (PspAudioMe_ValidateVmeMix(2, sGains[i], sInput, sOutput) != 1) {
            return -1;
        }
    }
#if PSP_AUDIO_VME_BENCH
    psp_audio_me_bench_reset();
#endif
    return 0;
}

static void psp_audio_me_vme_filter4(const s16* input, const s16* filter,
                                     s32* output) {
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    volatile s32* coeffs = base + PSP_AUDIO_VME_FILTER_COEFF_OFFSET;
    const u32 op = VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS;
    const u32 mux0 = vme_mux(TOP_0, BASE_0);
    const u32 mux1 = vme_mux(TOP_1, BASE_0);
    const u32 mux2 = vme_mux(TOP_2, BASE_0);
    const u32 mux3 = vme_mux(TOP_3, BASE_0);
    const u32 shift = 15;
    s32 lane;
    s32 tap;

    for (lane = 0; lane < PSP_AUDIO_VME_FILTER_OUTPUTS; lane++) {
        volatile s32* window = top + lane * PSP_AUDIO_VME_BUFFER_WORDS;

        for (tap = 0; tap < PSP_AUDIO_VME_FILTER_TAPS; tap++) {
            window[tap] = input[lane + tap];
        }
    }
    for (tap = 0; tap < PSP_AUDIO_VME_FILTER_TAPS; tap++) {
        coeffs[tap] = filter[PSP_AUDIO_VME_FILTER_TAPS - 1 - tap];
    }
    meLibSync();

    vmeLibStart();
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), op, mux0, shift);
    vme_pe0(fu_reg(PRIMARY, B), 0x4000);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, PSP_AUDIO_VME_FILTER_TAPS - 1);
    vme_pe0(agu_base(MODE), VME_DEF_MODE,
            PSP_AUDIO_VME_FILTER_COEFF_OFFSET);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, PSP_AUDIO_VME_FILTER_TAPS - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, PSP_AUDIO_VME_FILTER_TAPS - 1);
    vme_pe0(agu_write(FORMAT_0), 0);
    vme_pe0(agu_write(FORMAT_1), 0);
    vme_pe1(vme_fu(PRIMARY), op, mux1, shift);
    vme_pe1(fu_reg(PRIMARY, B), 0x4000);
    vme_pe2(vme_fu(PRIMARY), op, mux2, shift);
    vme_pe2(fu_reg(PRIMARY, B), 0x4000);
    vme_pe3(vme_fu(PRIMARY), op, mux3, shift);
    vme_pe3(fu_reg(PRIMARY, B), 0x4000);
    vmeLibFinish();
    meLibSync();

    for (lane = 0; lane < PSP_AUDIO_VME_FILTER_OUTPUTS; lane++) {
        output[lane] = base[lane * PSP_AUDIO_VME_BUFFER_WORDS +
                            PSP_AUDIO_VME_FILTER_TAPS - 1];
    }
}

static int psp_audio_me_validate_vme_filter4(const s16* input,
                                             const s16* filter,
                                             const s16* output) {
    s32 actual[PSP_AUDIO_VME_FILTER_OUTPUTS];
    s32 i;

    if ((sVmeState != PSP_AUDIO_VME_READY) || (input == NULL) ||
        (filter == NULL) || (output == NULL)) {
        return 0;
    }
    psp_audio_me_vme_filter4(input, filter, actual);
    sVmeFilterRuns++;
    for (i = 0; i < PSP_AUDIO_VME_FILTER_OUTPUTS; i++) {
        s16 clamped = psp_audio_me_clamp16(actual[i]);

        sVmeFilterOutputs++;
        if (clamped != output[i]) {
            if (sVmeFilterMismatches == 0) {
                sVmeFilterFirstCase = sVmeFilterRuns - 1;
                sVmeFilterFirstIndex = i;
                sVmeFilterExpected = (u32) (s32) output[i];
                sVmeFilterActual = (u32) actual[i];
            }
            sVmeFilterMismatches++;
        }
    }
    if (sVmeFilterMismatches != 0) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
    return 1;
}

static int psp_audio_me_vme_filter_synthetic(void) {
    static const s16 sInputs[4][11] = {
        { 0, 1, -1, 32767, -32768, 1234, -2345, 30000,
          -30000, 16384, -16384 },
        { 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
          32767, 32767, 32767 },
        { -32768, -32768, -32768, -32768, -32768, -32768, -32768,
          -32768, -32768, -32768, -32768 },
        { 32767, -32768, 30000, -30000, 16384, -16384, 8192, -8192,
          4096, -4096, 1 },
    };
    static const s16 sFilters[4][8] = {
        { 32767, -32768, 16384, -16384, 8192, -8192, 4096, -4096 },
        { 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767 },
        { 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767 },
        { -32768, 32767, -24576, 24576, -16384, 16384, -8192, 8192 },
    };
    s16 expected[PSP_AUDIO_VME_FILTER_OUTPUTS];
    s32 test;
    s32 i;

    for (test = 0; test < 4; test++) {
        for (i = 0; i < PSP_AUDIO_VME_FILTER_OUTPUTS; i++) {
            s64 sample = 0x4000;
            s32 tap;

            for (tap = 0; tap < PSP_AUDIO_VME_FILTER_TAPS; tap++) {
                sample += (s64) sInputs[test][i + tap] *
                          sFilters[test][PSP_AUDIO_VME_FILTER_TAPS - 1 - tap];
            }
            expected[i] = psp_audio_me_clamp16((s32) (sample >> 15));
        }
        if (psp_audio_me_validate_vme_filter4(sInputs[test], sFilters[test],
                                              expected) != 1) {
            return -1;
        }
    }
    return 0;
}

static s32 psp_audio_me_vme_resample_input(s32 lane, s32 index) {
    static const s16 sValues[8] = {
        0, 1, -1, 32767, -32768, 12345, -23456, 30000,
    };

    return sValues[(index + lane * 3) & 7];
}

static s32 psp_audio_me_vme_resample_coefficient(s32 lane, s32 index) {
    static const s16 sValues[8] = {
        32767, -32768, 16384, -16384, 8192, -8192, 1, -1,
    };

    return sValues[(index * 3 + lane) & 7];
}

static int psp_audio_me_vme_resample_synthetic(void) {
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    const u32 op = VME_FU_OPCODE_MUL_VEC_RSHIFT;
    const u32 round = 1 << 6;
    const u32 shift = 15;
    s32 lane;
    s32 index;

    for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane++) {
        volatile s32* inputs = top + lane * PSP_AUDIO_VME_BUFFER_WORDS;
        volatile s32* coefficients = base + lane * PSP_AUDIO_VME_BUFFER_WORDS +
                                     PSP_AUDIO_VME_RESAMPLE_COEFF_OFFSET;

        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_WORDS; index++) {
            inputs[index] = psp_audio_me_vme_resample_input(lane, index);
            coefficients[index] =
                psp_audio_me_vme_resample_coefficient(lane, index);
        }
    }
    meLibSync();

    vmeLibStart();
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, BASE_0), op, shift, round);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP,
            PSP_AUDIO_VME_RESAMPLE_WORDS - 1);
    vme_pe0(agu_base(MODE), VME_DEF_MODE,
            PSP_AUDIO_VME_RESAMPLE_COEFF_OFFSET);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP,
            PSP_AUDIO_VME_RESAMPLE_WORDS - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP,
            PSP_AUDIO_VME_RESAMPLE_WORDS - 1);
    vme_pe0(agu_write(FORMAT_0), 0);
    vme_pe0(agu_write(FORMAT_1), 0);
    vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_1, BASE_1), op, shift, round);
    vme_pe2(vme_fu(PRIMARY), vme_mux(TOP_2, BASE_2), op, shift, round);
    vme_pe3(vme_fu(PRIMARY), vme_mux(TOP_3, BASE_3), op, shift, round);
    vmeLibFinish();
    meLibSync();

    sVmeResampleRuns++;
    for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane++) {
        volatile s32* products = base + lane * PSP_AUDIO_VME_BUFFER_WORDS;

        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_WORDS; index++) {
            s32 input = psp_audio_me_vme_resample_input(lane, index);
            s32 coefficient =
                psp_audio_me_vme_resample_coefficient(lane, index);
            s32 expected = (input * coefficient + 0x4000) >> 15;
            s32 actual = products[index];

            sVmeResampleProducts++;
            if (actual != expected) {
                if (sVmeResampleMismatches == 0) {
                    sVmeResampleFirstLane = lane;
                    sVmeResampleFirstIndex = index;
                    sVmeResampleInput = (u32) input;
                    sVmeResampleCoefficient = (u32) coefficient;
                    sVmeResampleExpected = (u32) expected;
                    sVmeResampleActual = (u32) actual;
                }
                sVmeResampleMismatches++;
            }
        }
    }
    return sVmeResampleMismatches == 0 ? 0 : -1;
}

static void psp_audio_me_vme_resample_command(
    u32 count, const s16* inputs, const s16* coefficients, u32 inputStride,
    u32 coefficientOffset, s32 capturePairs, s32 useDmac,
    volatile PspAudioVmeResampleBenchResult* bench) {
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    const u32 multiply = VME_FU_OPCODE_MUL_VEC_RSHIFT;
    const u32 add = VME_FU_OPCODE_ADD_IBUF_RSHIFT;
    const u32 clamp = VME_FU_OPCODE_CLAMP_BACK;
    const u32 round = 1 << 6;
    u32 lane;
    u32 index;
#if PSP_AUDIO_VME_BENCH
    u32 start = PspAudioMe_BenchReadCount();
#else
    (void) bench;
    (void) useDmac;
#endif

#if PSP_AUDIO_VME_BENCH
    if (useDmac) {
        u32 transferSize =
            ((PSP_AUDIO_VME_RESAMPLE_LANES - 1) * inputStride + count) *
            sizeof(s16);

        meCoreDcacheWritebackRange((void*) inputs, transferSize);
        meCoreDcacheWritebackRange((void*) coefficients, transferSize);
        for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane += 2) {
            vmeLibMemTo16(
                (u32) (uintptr_t) &inputs[lane * inputStride],
                PSP_AUDIO_VME_TOP_WORD_OFFSET +
                    lane * PSP_AUDIO_VME_LANE_WORDS,
                count, VME_DMAC_TRANSFERT_NO_WAIT);
            vmeLibMemTo16(
                (u32) (uintptr_t) &inputs[(lane + 1) * inputStride],
                PSP_AUDIO_VME_TOP_WORD_OFFSET +
                    (lane + 1) * PSP_AUDIO_VME_LANE_WORDS,
                count, VME_DMAC_TRANSFERT_WAIT_FINISH);
        }
        for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane += 2) {
            vmeLibMemTo16(
                (u32) (uintptr_t) &coefficients[lane * inputStride],
                PSP_AUDIO_VME_BASE_WORD_OFFSET +
                    lane * PSP_AUDIO_VME_LANE_WORDS + coefficientOffset,
                count, VME_DMAC_TRANSFERT_NO_WAIT);
            vmeLibMemTo16(
                (u32) (uintptr_t) &coefficients[(lane + 1) * inputStride],
                PSP_AUDIO_VME_BASE_WORD_OFFSET +
                    (lane + 1) * PSP_AUDIO_VME_LANE_WORDS +
                    coefficientOffset,
                count, VME_DMAC_TRANSFERT_WAIT_FINISH);
        }
        meLibSync();
    } else
#endif
    {
        for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane++) {
            volatile s32* laneInputs =
                top + lane * PSP_AUDIO_VME_BUFFER_WORDS;
            volatile s32* laneCoefficients =
                base + lane * PSP_AUDIO_VME_BUFFER_WORDS +
                coefficientOffset;

            for (index = 0; index < count; index++) {
                laneInputs[index] = inputs[lane * inputStride + index];
                laneCoefficients[index] =
                    coefficients[lane * inputStride + index];
            }
        }
        meLibSync();
    }
#if PSP_AUDIO_VME_BENCH
    bench->stageTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif

    vmeLibStart();
    vme_set(ENABLE, FU_1, 0);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(TOP_0, BASE_0), multiply, 15,
            round);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_base(MODE), VME_DEF_MODE,
            coefficientOffset);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(FORMAT_0), 0);
    vme_pe0(agu_write(FORMAT_1), 0);
    vme_pe1(vme_fu(PRIMARY), vme_mux(TOP_1, BASE_1), multiply, 15,
            round);
    vme_pe2(vme_fu(PRIMARY), vme_mux(TOP_2, BASE_2), multiply, 15,
            round);
    vme_pe3(vme_fu(PRIMARY), vme_mux(TOP_3, BASE_3), multiply, 15,
            round);
#if PSP_AUDIO_VME_BENCH
    bench->updateTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif
    vmeLibFinish();
    meLibSync();
#if PSP_AUDIO_VME_BENCH
    bench->runTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif

    vmeLibStart();
    vme_set(ENABLE, FU_1, 0);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(BASE_0, BASE_1), add, 0);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(FORMAT_0), 0);
    vme_pe0(agu_write(FORMAT_1), 0);
    vme_pe1(vme_fu(PRIMARY), vme_mux(BASE_0, BASE_1), add, 0);
    vme_pe2(vme_fu(PRIMARY), vme_mux(BASE_2, BASE_3), add, 0);
    vme_pe3(vme_fu(PRIMARY), vme_mux(BASE_2, BASE_3), add, 0);
#if PSP_AUDIO_VME_BENCH
    bench->updateTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif
    vmeLibFinish();
    meLibSync();
#if PSP_AUDIO_VME_BENCH
    bench->runTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif

    if (capturePairs) {
        for (index = 0; index < count; index++) {
            sVmeResamplePair01[index] = base[index];
            sVmeResamplePair23[index] =
                base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index];
        }
    }
#if PSP_AUDIO_VME_BENCH
    bench->pairTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif

    vmeLibStart();
    vme_set(ENABLE, FU_1, 0xf << 28);
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, 0);
    vme_icn(AGU_WRITE, 0);
    vme_pe0(vme_fu(PRIMARY), vme_mux(BASE_0, BASE_2), add, 0);
    vme_pe0(vme_fu(SECONDARY), vme_mux(NONE, STAGING_0), clamp);
    vme_pe0(fu_reg(SECONDARY, A), 32767);
    vme_pe0(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe0(agu_base(MODE), VME_DEF_MODE);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_9);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count - 1);
    vme_pe0(agu_write(FORMAT_0), 0);
    vme_pe0(agu_write(FORMAT_1), 0);
    vme_pe1(vme_fu(PRIMARY), vme_mux(BASE_0, BASE_2), add, 0);
    vme_pe1(vme_fu(SECONDARY), vme_mux(NONE, STAGING_1), clamp);
    vme_pe1(fu_reg(SECONDARY, A), 32767);
    vme_pe1(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe2(vme_fu(PRIMARY), vme_mux(BASE_0, BASE_2), add, 0);
    vme_pe2(vme_fu(SECONDARY), vme_mux(NONE, STAGING_2), clamp);
    vme_pe2(fu_reg(SECONDARY, A), 32767);
    vme_pe2(fu_reg(SECONDARY, B), (u32) (s32) -32768);
    vme_pe3(vme_fu(PRIMARY), vme_mux(BASE_0, BASE_2), add, 0);
    vme_pe3(vme_fu(SECONDARY), vme_mux(NONE, STAGING_3), clamp);
    vme_pe3(fu_reg(SECONDARY, A), 32767);
    vme_pe3(fu_reg(SECONDARY, B), (u32) (s32) -32768);
#if PSP_AUDIO_VME_BENCH
    bench->updateTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif
    vmeLibFinish();
    meLibSync();
#if PSP_AUDIO_VME_BENCH
    bench->runTicks += psp_audio_me_bench_elapsed(start);
#endif
}

#if PSP_AUDIO_VME_BENCH
static int psp_audio_me_vme_resample_batch_bench(void) {
    volatile PspAudioVmeResampleBenchResult* bench =
        PSP_AUDIO_VME_RESAMPLE_BATCH_BENCH_STATS;
    volatile PspAudioVmeResampleBenchResult* dmacBench =
        PSP_AUDIO_VME_RESAMPLE_DMAC_BENCH_STATS;
    volatile s32* vmeOutput = (volatile s32*) VME_BASE_BUFFERS;
    u32 start;
    u32 index;
    u32 lane;
    u32 run;

    for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane++) {
        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;
             index++) {
            sVmeResampleBatchInputs[lane][index] =
                psp_audio_me_vme_resample_input(lane, index);
            sVmeResampleBatchCoefficients[lane][index] =
                psp_audio_me_vme_resample_coefficient(lane, index);
        }
    }

    for (run = 0; run < PSP_AUDIO_VME_RESAMPLE_BATCH_REPEATS; run++) {
        start = PspAudioMe_BenchReadCount();
        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;
             index++) {
            s32 sample = 0;

            for (lane = 0; lane < PSP_AUDIO_VME_RESAMPLE_LANES; lane++) {
                sample +=
                    ((s32) sVmeResampleBatchInputs[lane][index] *
                         sVmeResampleBatchCoefficients[lane][index] +
                     0x4000) >> 15;
            }
            sVmeResampleBatchExpected[index] =
                psp_audio_me_clamp16(sample);
        }
        {
            u32 scalarTicks = psp_audio_me_bench_elapsed(start);

            bench->scalarTicks += scalarTicks;
            dmacBench->scalarTicks += scalarTicks;
        }

        psp_audio_me_vme_resample_command(
            PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES,
            &sVmeResampleBatchInputs[0][0],
            &sVmeResampleBatchCoefficients[0][0],
            PSP_AUDIO_VME_RESAMPLE_BATCH_STRIDE, 0, 0, 0, bench);

        start = PspAudioMe_BenchReadCount();
        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;
             index++) {
            sVmeResampleBatchActual[index] = (s16) vmeOutput[index];
        }
        bench->readbackTicks += psp_audio_me_bench_elapsed(start);

        start = PspAudioMe_BenchReadCount();
        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;
             index++) {
            if (sVmeResampleBatchActual[index] !=
                sVmeResampleBatchExpected[index]) {
                if (bench->mismatches == 0) {
                    bench->firstMismatch = index;
                    bench->expected = sVmeResampleBatchExpected[index];
                    bench->actual = sVmeResampleBatchActual[index];
                }
                bench->mismatches++;
            }
        }
        bench->validateTicks += psp_audio_me_bench_elapsed(start);
        bench->calls++;
        bench->samples += PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;

        start = PspAudioMe_BenchReadCount();
        vmeLibWipe();
        meLibSync();
        bench->wipeTicks += psp_audio_me_bench_elapsed(start);
        if (bench->mismatches != 0) {
            return -1;
        }

        psp_audio_me_vme_resample_command(
            PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES,
            &sVmeResampleBatchInputs[0][0],
            &sVmeResampleBatchCoefficients[0][0],
            PSP_AUDIO_VME_RESAMPLE_BATCH_STRIDE, 0, 0, 1, dmacBench);

        start = PspAudioMe_BenchReadCount();
        meCoreDcacheWritebackInvalidateRange(
            sVmeResampleBatchActual,
            sizeof(sVmeResampleBatchActual));
        vmeLibMemFrom16(
            (u32) (uintptr_t) sVmeResampleBatchActual,
            PSP_AUDIO_VME_BASE_WORD_OFFSET,
            PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES,
            VME_DMAC_TRANSFERT_WAIT_FINISH);
        meCoreDcacheInvalidateRange(sVmeResampleBatchActual,
                                    sizeof(sVmeResampleBatchActual));
        meLibSync();
        dmacBench->readbackTicks += psp_audio_me_bench_elapsed(start);

        start = PspAudioMe_BenchReadCount();
        for (index = 0; index < PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;
             index++) {
            if (sVmeResampleBatchActual[index] !=
                sVmeResampleBatchExpected[index]) {
                if (dmacBench->mismatches == 0) {
                    dmacBench->firstMismatch = index;
                    dmacBench->expected = sVmeResampleBatchExpected[index];
                    dmacBench->actual = sVmeResampleBatchActual[index];
                }
                dmacBench->mismatches++;
            }
        }
        dmacBench->validateTicks += psp_audio_me_bench_elapsed(start);
        dmacBench->calls++;
        dmacBench->samples += PSP_AUDIO_VME_RESAMPLE_BATCH_SAMPLES;

        start = PspAudioMe_BenchReadCount();
        vmeLibWipe();
        meLibSync();
        dmacBench->wipeTicks += psp_audio_me_bench_elapsed(start);
        if (dmacBench->mismatches != 0) {
            return -1;
        }
    }
    return 0;
}

static void psp_audio_me_vme_transport_mismatch(
    volatile PspAudioVmeTransportBenchResult* result, s32 domain,
    s32 index, s32 expected, s32 actual) {
    if (result->mismatches == 0) {
        result->firstDomain = domain;
        result->firstIndex = index;
        result->expected = expected;
        result->actual = actual;
    }
    result->mismatches++;
}

__attribute__((noinline))
static int psp_audio_me_vme_transport_bench(void) {
    static const u32 sCases[PSP_AUDIO_VME_TRANSPORT_BENCH_CASES] = {
        192, 384, 768, 1536, 2048,
    };
    s16 localScratch[PSP_AUDIO_VME_ENV_MAX_SAMPLES]
        __attribute__((aligned(64)));
    volatile s32* vme = (volatile s32*) VME_BASE_BUFFERS;
    u32 caseIndex;

    for (caseIndex = 0;
         caseIndex < PSP_AUDIO_VME_TRANSPORT_BENCH_CASES;
         caseIndex++) {
        volatile PspAudioVmeTransportBenchResult* result =
            &PSP_AUDIO_VME_TRANSPORT_BENCH_STATS[caseIndex];
        u32 count = sCases[caseIndex];
        u32 size = count * sizeof(s16);
        u32 run;
        u32 index;

        result->samples = count;
        result->mainAddress = (u32) (uintptr_t) sVmeTransportMain;
        result->localAddress = (u32) (uintptr_t) localScratch;
        for (run = 0; run < PSP_AUDIO_VME_TRANSPORT_BENCH_REPEATS;
             run++) {
            u32 start;

            for (index = 0; index < count; index++) {
                s16 value = (s16) ((index * 1973 + run * 7919) & 0xffff);

                sVmeTransportMain[index] = value;
                localScratch[index] = value;
            }

            start = PspAudioMe_BenchReadCount();
            meCoreDcacheWritebackRange(sVmeTransportMain, size);
            vmeLibMemTo16(
                (u32) (uintptr_t) sVmeTransportMain,
                PSP_AUDIO_VME_BASE_WORD_OFFSET, count,
                VME_DMAC_TRANSFERT_WAIT_FINISH);
            meLibSync();
            result->mainToTicks += psp_audio_me_bench_elapsed(start);
            for (index = 0; index < count; index++) {
                s32 expected = sVmeTransportMain[index];

                if (vme[index] != expected) {
                    psp_audio_me_vme_transport_mismatch(
                        result, 0, index, expected, vme[index]);
                }
            }

            start = PspAudioMe_BenchReadCount();
            meCoreDcacheWritebackRange(localScratch, size);
            vmeLibMemTo16(
                (u32) (uintptr_t) localScratch,
                PSP_AUDIO_VME_BASE_WORD_OFFSET, count,
                VME_DMAC_TRANSFERT_WAIT_FINISH);
            meLibSync();
            result->localToTicks += psp_audio_me_bench_elapsed(start);
            for (index = 0; index < count; index++) {
                s32 expected = localScratch[index];

                if (vme[index] != expected) {
                    psp_audio_me_vme_transport_mismatch(
                        result, 1, index, expected, vme[index]);
                }
            }

            for (index = 0; index < count; index++) {
                vme[index] = (s16) ((index * 3181 + run * 2377) & 0xffff);
            }
            meLibSync();
            start = PspAudioMe_BenchReadCount();
            meCoreDcacheWritebackInvalidateRange(sVmeTransportMain, size);
            vmeLibMemFrom16(
                (u32) (uintptr_t) sVmeTransportMain,
                PSP_AUDIO_VME_BASE_WORD_OFFSET, count,
                VME_DMAC_TRANSFERT_WAIT_FINISH);
            meCoreDcacheInvalidateRange(sVmeTransportMain, size);
            meLibSync();
            result->mainFromTicks += psp_audio_me_bench_elapsed(start);
            for (index = 0; index < count; index++) {
                s32 expected =
                    (s16) ((index * 3181 + run * 2377) & 0xffff);

                if (sVmeTransportMain[index] != expected) {
                    psp_audio_me_vme_transport_mismatch(
                        result, 2, index, expected,
                        sVmeTransportMain[index]);
                }
            }

            for (index = 0; index < count; index++) {
                vme[index] = (s16) ((index * 1559 + run * 1877) & 0xffff);
            }
            meLibSync();
            start = PspAudioMe_BenchReadCount();
            meCoreDcacheWritebackInvalidateRange(localScratch, size);
            vmeLibMemFrom16(
                (u32) (uintptr_t) localScratch,
                PSP_AUDIO_VME_BASE_WORD_OFFSET, count,
                VME_DMAC_TRANSFERT_WAIT_FINISH);
            meCoreDcacheInvalidateRange(localScratch, size);
            meLibSync();
            result->localFromTicks += psp_audio_me_bench_elapsed(start);
            for (index = 0; index < count; index++) {
                s32 expected =
                    (s16) ((index * 1559 + run * 1877) & 0xffff);

                if (localScratch[index] != expected) {
                    psp_audio_me_vme_transport_mismatch(
                        result, 3, index, expected, localScratch[index]);
                }
            }
            result->calls++;
            if (result->mismatches != 0) {
                vmeLibWipe();
                meLibSync();
                return -1;
            }
        }
    }
    vmeLibWipe();
    meLibSync();
    return 0;
}

static void psp_audio_me_vme_boundary_mismatch(
    volatile PspAudioVmeEnvBoundaryBenchResult* result, s32 output,
    s32 index, s32 expected, s32 actual) {
    if (result->mismatches == 0) {
        result->firstOutput = output;
        result->firstIndex = index;
        result->expected = expected;
        result->actual = actual;
    }
    result->mismatches++;
}

static void psp_audio_me_vme_pipeline_mismatch(
    volatile PspAudioVmeEnvPipelineResult* result, s32 stage, s32 index,
    s32 expected, s32 actual) {
    if ((result->seedMismatches + result->residentMismatches) == 0) {
        result->firstStage = stage;
        result->firstIndex = index;
        result->expected = expected;
        result->actual = actual;
    }
    if (stage == 1) {
        result->seedMismatches++;
    } else {
        result->residentMismatches++;
    }
}

static s32 psp_audio_me_vme_pipeline_input0(u32 index, u32 run) {
    return (s16) ((index * 1973 + run * 7919) & 0xffff);
}

static s32 psp_audio_me_vme_pipeline_input1(u32 index, u32 run) {
    return (s16) ((index * 1559 + run * 2377 + 0x29b7) & 0xffff);
}

static s32 psp_audio_me_vme_pipeline_accumulator(u32 index, u32 run) {
    return (s16) ((index * 3181 + run * 1877 + 0x7211) & 0xffff);
}

static int psp_audio_me_vme_env_pipeline_proof(void) {
    const u32 samples = 192;
    const s32 gain0 = 0x7000;
    const s32 gain1 = 0x5000;
    volatile PspAudioVmeEnvPipelineResult* result =
        PSP_AUDIO_VME_ENV_PIPELINE_STATS;
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    PspAudioVmeEnvPipelineContextParam param;
    void* context;
    u32 run;
    u32 index;

    param.count = samples;
    param.gain = gain0;
    param.secondaryMask = 2;
    result->voices = 2;
    result->samples = samples;
    context = psp_audio_me_vme_env_pipeline_context(&param);
    for (run = 0; run < PSP_AUDIO_VME_ENV_BENCH_REPEATS; run++) {
        vmeLibWipe();
        for (index = 0; index < samples; index++) {
            top[index] = psp_audio_me_vme_pipeline_input0(index, run);
            base[index] = 0;
            base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                psp_audio_me_vme_pipeline_accumulator(index, run);
        }
        meLibSync();

        vmeLibStart();
        vmeLibLoadCustomContext(context);
        vmeLibFinish();
        meLibSync();
        result->bestOffset = 0;
        result->offsetMismatches = 0xffffffffU;
        {
            s32 offset;

            for (offset = -8; offset <= 8; offset++) {
                u32 offsetMismatches = offset < 0 ? -offset : offset;

                for (index = offset < 0 ? -offset : 0;
                     index < samples && (s32) index + offset < (s32) samples;
                     index++) {
                    s32 expectedIndex = (s32) index + offset;
                    s32 input = psp_audio_me_vme_pipeline_input0(
                        expectedIndex, run);
                    s32 accumulator =
                        psp_audio_me_vme_pipeline_accumulator(
                            expectedIndex, run);
                    s32 expected = psp_audio_me_clamp16(
                        accumulator + (s16) ((input * gain0) >> 16));
                    s32 actual =
                        base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index];

                    if (actual != expected) {
                        offsetMismatches++;
                    }
                }
                if (offsetMismatches < result->offsetMismatches) {
                    result->bestOffset = offset;
                    result->offsetMismatches = offsetMismatches;
                }
            }
        }
        for (index = 0; index < samples; index++) {
            s32 input = psp_audio_me_vme_pipeline_input0(index, run);
            s32 accumulator =
                psp_audio_me_vme_pipeline_accumulator(index, run);
            s32 contribution = (s16) ((input * gain0) >> 16);
            s32 expected = psp_audio_me_clamp16(
                accumulator + contribution);
            s32 actual =
                base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index];

            if (actual != expected) {
                psp_audio_me_vme_pipeline_mismatch(
                    result, 1, index, expected, actual);
            }
        }
        if (result->seedMismatches != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }

        for (index = 0; index < samples; index++) {
            top[index] = psp_audio_me_vme_pipeline_input1(index, run);
        }
        meLibSync();
        vmeLibStart();
        vme_pe0(fu_reg(PRIMARY, B), (u32) gain1);
        vmeLibFinish();
        meLibSync();
        for (index = 0; index < samples; index++) {
            s32 input0 = psp_audio_me_vme_pipeline_input0(index, run);
            s32 input1 = psp_audio_me_vme_pipeline_input1(index, run);
            s32 accumulator =
                psp_audio_me_vme_pipeline_accumulator(index, run);
            s32 first = psp_audio_me_clamp16(
                accumulator + (s16) ((input0 * gain0) >> 16));
            s32 expected = psp_audio_me_clamp16(
                first + (s16) ((input1 * gain1) >> 16));
            s32 actual =
                base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index];

            if (actual != expected) {
                psp_audio_me_vme_pipeline_mismatch(
                    result, 2, index, expected, actual);
            }
        }
        result->calls++;
        if (result->residentMismatches != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }
    }
    vmeLibWipe();
    meLibSync();
    return 0;
}

static int psp_audio_me_vme_env_clamp_proof(void) {
    const u32 samples = 192;
    const s32 gain = 0x7000;
    volatile PspAudioVmeEnvPipelineResult* result =
        PSP_AUDIO_VME_ENV_PIPELINE_STATS;
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    PspAudioVmeEnvPipelineContextParam param;
    u32 maskIndex;
    u32 index;

    param.count = samples;
    param.gain = gain;
    result->clampOffsetMismatches = 0xffffffffU;
    for (maskIndex = 0; maskIndex < 4; maskIndex++) {
        void* context;

        param.secondaryMask = 1U << maskIndex;
        context = psp_audio_me_vme_env_clamp_context(&param);
        vmeLibWipe();
        for (index = 0; index < samples; index++) {
            s32 input = (s16) ((index * 1973) & 0xffff);
            s32 accumulator =
                (s16) ((index * 3181 + 0x7211) & 0xffff);

            top[index] = input;
            base[index] = 0;
            base[PSP_AUDIO_VME_BUFFER_WORDS + index] = accumulator;
            base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] = 0;
        }
        meLibSync();
        vmeLibStart();
        vmeLibLoadCustomContext(context);
        vmeLibFinish();
        meLibSync();

        result->clampMaskBestOffset[maskIndex] = 0;
        result->clampMaskOffsetMismatches[maskIndex] = 0xffffffffU;
        {
            s32 offset;

            for (offset = -8; offset <= 8; offset++) {
                u32 mismatches = offset < 0 ? -offset : offset;

                for (index = offset < 0 ? -offset : 0;
                     index < samples && (s32) index + offset < (s32) samples;
                    index++) {
                    s32 expectedIndex = (s32) index + offset;
                    s32 input =
                        (s16) ((expectedIndex * 1973) & 0xffff);
                    s32 accumulator = (s16) ((expectedIndex * 3181 +
                        0x7211) & 0xffff);
                    s32 expected = psp_audio_me_clamp16(
                        accumulator + (s16) ((input * gain) >> 16));
                    s32 actual =
                        base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index];

                    if (actual != expected) {
                        mismatches++;
                    }
                }
                if (mismatches <
                    result->clampMaskOffsetMismatches[maskIndex]) {
                    result->clampMaskBestOffset[maskIndex] = offset;
                    result->clampMaskOffsetMismatches[maskIndex] =
                        mismatches;
                }
            }
        }
        if (result->clampMaskOffsetMismatches[maskIndex] <
            result->clampOffsetMismatches) {
            result->clampBestOffset =
                result->clampMaskBestOffset[maskIndex];
            result->clampOffsetMismatches =
                result->clampMaskOffsetMismatches[maskIndex];
        }
        if (result->clampMaskOffsetMismatches[maskIndex] ==
            (u32) (result->clampMaskBestOffset[maskIndex] < 0 ?
                -result->clampMaskBestOffset[maskIndex] :
                result->clampMaskBestOffset[maskIndex])) {
            result->clampPassingMask = param.secondaryMask;
        }
    }
    if (result->clampPassingMask == 0) {
        result->clampMismatches = result->clampOffsetMismatches;
        result->firstStage = 3;
        result->firstIndex = -1;
        result->expected = 0;
        result->actual = 0;
        vmeLibWipe();
        meLibSync();
        return -1;
    }
    result->clampMismatches = 0;
    vmeLibWipe();
    meLibSync();
    return 0;
}

static void psp_audio_me_vme_env_dry_mismatch(
    volatile PspAudioVmeEnvPipelineResult* result, u32 lane, u32 stage,
    u32 index, s32 expected, s32 actual) {
    if ((result->drySeedMismatches[0] + result->drySeedMismatches[1] +
         result->dryResidentMismatches[0] +
         result->dryResidentMismatches[1]) == 0) {
        result->dryFirstLane = lane;
        result->dryFirstStage = stage;
        result->dryFirstIndex = index;
        result->dryExpected = expected;
        result->dryActual = actual;
    }
    if (stage == 1) {
        result->drySeedMismatches[lane]++;
    } else {
        result->dryResidentMismatches[lane]++;
    }
}

static s32 psp_audio_me_vme_env_dry_accumulator(
    u32 lane, u32 index, u32 run) {
    return (s16) ((index * (3181 + lane * 419) + run * 1877 +
                   0x7211 + lane * 0x1943) & 0xffff);
}

static int psp_audio_me_vme_env_dry_proof(void) {
    const u32 samples = 192;
    const s32 gain0[2] = { 0x7000, 0xd000 };
    const s32 gain1[2] = { 0x5000, 0x3000 };
    volatile PspAudioVmeEnvPipelineResult* result =
        PSP_AUDIO_VME_ENV_PIPELINE_STATS;
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    PspAudioVmeEnvDryContextParam param;
    void* context;
    u32 run;
    u32 lane;
    u32 index;

    param.count = samples;
    param.gainLeft = gain0[0];
    param.gainRight = gain0[1];
    result->dryVoices = 2;
    result->drySamples = samples;
    context = psp_audio_me_vme_env_dry_context(&param);
    for (run = 0; run < PSP_AUDIO_VME_ENV_BENCH_REPEATS; run++) {
        vmeLibWipe();
        for (index = 0; index < samples; index++) {
            top[index] = psp_audio_me_vme_pipeline_input0(index, run);
            base[index] = 0;
            base[PSP_AUDIO_VME_BUFFER_WORDS + index] = 0;
            for (lane = 0; lane < 2; lane++) {
                base[(lane + 2) * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    psp_audio_me_vme_env_dry_accumulator(
                        lane, index, run);
            }
        }
        meLibSync();
        vmeLibStart();
        vmeLibLoadCustomContext(context);
        vmeLibFinish();
        meLibSync();
        for (lane = 0; lane < 2; lane++) {
            for (index = 0; index < samples; index++) {
                s32 input = psp_audio_me_vme_pipeline_input0(index, run);
                s32 accumulator = psp_audio_me_vme_env_dry_accumulator(
                    lane, index, run);
                s32 contribution = (s16) ((input * gain0[lane]) >> 16);
                s32 expected = psp_audio_me_clamp16(
                    accumulator + contribution);
                s32 actual = base[(lane + 2) *
                    PSP_AUDIO_VME_BUFFER_WORDS + index];

                if (actual != expected) {
                    psp_audio_me_vme_env_dry_mismatch(
                        result, lane, 1, index, expected, actual);
                }
            }
        }
        if ((result->drySeedMismatches[0] +
             result->drySeedMismatches[1]) != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }

        for (index = 0; index < samples; index++) {
            top[index] = psp_audio_me_vme_pipeline_input1(index, run);
        }
        meLibSync();
        vmeLibStart();
        vme_pe0(fu_reg(PRIMARY, B), (u32) gain1[0]);
        vme_pe1(fu_reg(PRIMARY, B), (u32) gain1[1]);
        vmeLibFinish();
        meLibSync();
        for (lane = 0; lane < 2; lane++) {
            for (index = 0; index < samples; index++) {
                s32 input0 = psp_audio_me_vme_pipeline_input0(index, run);
                s32 input1 = psp_audio_me_vme_pipeline_input1(index, run);
                s32 accumulator = psp_audio_me_vme_env_dry_accumulator(
                    lane, index, run);
                s32 first = psp_audio_me_clamp16(
                    accumulator +
                    (s16) ((input0 * gain0[lane]) >> 16));
                s32 expected = psp_audio_me_clamp16(
                    first + (s16) ((input1 * gain1[lane]) >> 16));
                s32 actual = base[(lane + 2) *
                    PSP_AUDIO_VME_BUFFER_WORDS + index];

                if (actual != expected) {
                    psp_audio_me_vme_env_dry_mismatch(
                        result, lane, 2, index, expected, actual);
                }
            }
        }
        result->dryCalls++;
        if ((result->dryResidentMismatches[0] +
             result->dryResidentMismatches[1]) != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }
    }
    vmeLibWipe();
    meLibSync();
    return 0;
}

static void psp_audio_me_vme_env_wet_mismatch(
    volatile PspAudioVmeEnvPipelineResult* result, u32 lane, u32 stage,
    u32 index, s32 expected, s32 actual) {
    if ((result->wetProductMismatches[0] +
         result->wetProductMismatches[1] +
         result->wetSeedMismatches[0] + result->wetSeedMismatches[1] +
         result->wetResidentMismatches[0] +
         result->wetResidentMismatches[1]) == 0) {
        result->wetFirstLane = lane;
        result->wetFirstStage = stage;
        result->wetFirstIndex = index;
        result->wetExpected = expected;
        result->wetActual = actual;
    }
    if ((stage == 1) || (stage == 3)) {
        result->wetProductMismatches[lane]++;
    } else if (stage == 2) {
        result->wetSeedMismatches[lane]++;
    } else {
        result->wetResidentMismatches[lane]++;
    }
}

static s32 psp_audio_me_vme_env_wet_accumulator(
    u32 lane, u32 index, u32 run) {
    return (s16) ((index * (2377 + lane * 313) + run * 1559 +
                   0x1943 + lane * 0x53b1) & 0xffff);
}

static int psp_audio_me_vme_env_wet_proof(void) {
    const u32 samples = 192;
    const s32 gain0[2] = { 0x7000, 0xd000 };
    const s32 gain1[2] = { 0x5000, 0x3000 };
    const s32 wetGain0 = 0xb000;
    const s32 wetGain1 = 0x6000;
    volatile PspAudioVmeEnvPipelineResult* result =
        PSP_AUDIO_VME_ENV_PIPELINE_STATS;
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    PspAudioVmeEnvDryContextParam dryParam;
    PspAudioVmeEnvPipelineContextParam wetMultiplyParam;
    PspAudioVmeEnvContextParam wetAddParam;
    void* dryContext;
    void* wetMultiplyContext;
    void* wetAddContext;
    u32 run;
    u32 lane;
    u32 index;
    u32 start;

    dryParam.count = samples;
    dryParam.gainLeft = gain0[0];
    dryParam.gainRight = gain0[1];
    wetMultiplyParam.count = samples;
    wetMultiplyParam.gain = wetGain0;
    wetMultiplyParam.secondaryMask = 0;
    wetAddParam.count = samples;
    result->wetVoices = 2;
    result->wetSamples = samples;
    dryContext = psp_audio_me_vme_env_dry_context(&dryParam);
    wetMultiplyContext =
        psp_audio_me_vme_env_wet_multiply_context(&wetMultiplyParam);
    wetAddContext = psp_audio_me_vme_env_wet_add_context(&wetAddParam);
    for (run = 0; run < PSP_AUDIO_VME_ENV_BENCH_REPEATS; run++) {
        vmeLibWipe();
        for (index = 0; index < samples; index++) {
            top[index] = psp_audio_me_vme_pipeline_input0(index, run);
            base[index] = 0;
            base[PSP_AUDIO_VME_BUFFER_WORDS + index] = 0;
            for (lane = 0; lane < 2; lane++) {
                base[(lane + 2) * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    psp_audio_me_vme_env_dry_accumulator(
                        lane, index, run);
                base[(lane + 2) * PSP_AUDIO_VME_BUFFER_WORDS +
                     PSP_AUDIO_VME_ENV_WET_OFFSET + index] =
                    psp_audio_me_vme_env_wet_accumulator(
                        lane, index, run);
            }
        }
        meLibSync();
        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(dryContext);
        vmeLibFinish();
        result->wetTicks += psp_audio_me_bench_elapsed(start);
        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(wetMultiplyContext);
        vmeLibFinish();
        result->wetTicks += psp_audio_me_bench_elapsed(start);
        meLibSync();
        for (lane = 0; lane < 2; lane++) {
            for (index = 0; index < samples; index++) {
                s32 input = psp_audio_me_vme_pipeline_input0(index, run);
                s32 dry = (s16) ((input * gain0[lane]) >> 16);
                s32 expected = (s16) ((dry * wetGain0) >> 16);
                s32 actual = base[lane * PSP_AUDIO_VME_BUFFER_WORDS +
                                  PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET +
                                  index];

                if (actual != expected) {
                    psp_audio_me_vme_env_wet_mismatch(
                        result, lane, 1, index, expected, actual);
                }
            }
        }
        if ((result->wetProductMismatches[0] +
             result->wetProductMismatches[1]) != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }
        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(wetAddContext);
        vmeLibFinish();
        result->wetTicks += psp_audio_me_bench_elapsed(start);
        meLibSync();
        for (lane = 0; lane < 2; lane++) {
            for (index = 0; index < samples; index++) {
                s32 input = psp_audio_me_vme_pipeline_input0(index, run);
                s32 dry = (s16) ((input * gain0[lane]) >> 16);
                s32 wet = (s16) ((dry * wetGain0) >> 16);
                s32 accumulator = psp_audio_me_vme_env_wet_accumulator(
                    lane, index, run);
                s32 expected = psp_audio_me_clamp16(accumulator + wet);
                s32 actual = base[(lane + 2) *
                    PSP_AUDIO_VME_BUFFER_WORDS +
                    PSP_AUDIO_VME_ENV_WET_OFFSET + index];

                if (actual != expected) {
                    psp_audio_me_vme_env_wet_mismatch(
                        result, lane, 2, index, expected, actual);
                }
            }
        }
        if ((result->wetSeedMismatches[0] +
             result->wetSeedMismatches[1]) != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }

        for (index = 0; index < samples; index++) {
            top[index] = psp_audio_me_vme_pipeline_input1(index, run);
        }
        meLibSync();
        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(dryContext);
        vme_pe0(fu_reg(PRIMARY, B), (u32) gain1[0]);
        vme_pe1(fu_reg(PRIMARY, B), (u32) gain1[1]);
        vmeLibFinish();
        result->wetTicks += psp_audio_me_bench_elapsed(start);
        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(wetMultiplyContext);
        vme_pe0(fu_reg(PRIMARY, B), (u32) wetGain1);
        vme_pe1(fu_reg(PRIMARY, B), (u32) wetGain1);
        vmeLibFinish();
        result->wetTicks += psp_audio_me_bench_elapsed(start);
        meLibSync();
        for (lane = 0; lane < 2; lane++) {
            for (index = 0; index < samples; index++) {
                s32 input = psp_audio_me_vme_pipeline_input1(index, run);
                s32 dry = (s16) ((input * gain1[lane]) >> 16);
                s32 expected = (s16) ((dry * wetGain1) >> 16);
                s32 actual = base[lane * PSP_AUDIO_VME_BUFFER_WORDS +
                                  PSP_AUDIO_VME_ENV_WET_PRODUCT_OFFSET +
                                  index];

                if (actual != expected) {
                    psp_audio_me_vme_env_wet_mismatch(
                        result, lane, 3, index, expected, actual);
                }
            }
        }
        if ((result->wetProductMismatches[0] +
             result->wetProductMismatches[1]) != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }
        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(wetAddContext);
        vmeLibFinish();
        result->wetTicks += psp_audio_me_bench_elapsed(start);
        meLibSync();
        for (lane = 0; lane < 2; lane++) {
            for (index = 0; index < samples; index++) {
                s32 input0 = psp_audio_me_vme_pipeline_input0(index, run);
                s32 input1 = psp_audio_me_vme_pipeline_input1(index, run);
                s32 dry0 = (s16) ((input0 * gain0[lane]) >> 16);
                s32 dry1 = (s16) ((input1 * gain1[lane]) >> 16);
                s32 wet0 = (s16) ((dry0 * wetGain0) >> 16);
                s32 wet1 = (s16) ((dry1 * wetGain1) >> 16);
                s32 accumulator = psp_audio_me_vme_env_wet_accumulator(
                    lane, index, run);
                s32 first = psp_audio_me_clamp16(accumulator + wet0);
                s32 expected = psp_audio_me_clamp16(first + wet1);
                s32 actual = base[(lane + 2) *
                    PSP_AUDIO_VME_BUFFER_WORDS +
                    PSP_AUDIO_VME_ENV_WET_OFFSET + index];

                if (actual != expected) {
                    psp_audio_me_vme_env_wet_mismatch(
                        result, lane, 4, index, expected, actual);
                }
            }
        }
        result->wetCalls++;
        if ((result->wetResidentMismatches[0] +
             result->wetResidentMismatches[1]) != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }
    }
    vmeLibWipe();
    meLibSync();
    return 0;
}

__attribute__((noinline))
static int psp_audio_me_vme_env_boundary_bench(void) {
    PspAudioVmeEnvContextParam param;
    s16 local[9][192] __attribute__((aligned(64)));
    volatile PspAudioVmeEnvBoundaryBenchResult* result =
        PSP_AUDIO_VME_ENV_BOUNDARY_BENCH_STATS;
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    void* multiplyContext;
    void* addContext;
    u32 run;
    u32 index;

    param.count = 192;
    multiplyContext = psp_audio_me_vme_env_multiply_context(&param);
    addContext = psp_audio_me_vme_env_add_context(&param);
    result->samples = 192;
    for (run = 0; run < PSP_AUDIO_VME_ENV_BENCH_REPEATS; run++) {
        u16 volLeft = 0x7000;
        u16 volRight = 0x5000;
        u16 volWet = 0x3000;
        u32 start = PspAudioMe_BenchReadCount();

        for (index = 0; index < 192; index++) {
            if ((index != 0) && ((index & 7) == 0)) {
                volLeft += 0x0111;
                volRight += (u16) -0x00b3;
                volWet += 0x0057;
            }
            local[0][index] = (s16) ((index * 1973) & 0xffff);
            local[1][index] = (s16) ((index * 3181 + 0x7211) & 0xffff);
            local[2][index] = (s16) ((index * 2377 + 0x1943) & 0xffff);
            local[3][index] = (s16) ((index * 1877 + 0x6d31) & 0xffff);
            local[4][index] = (s16) ((index * 1559 + 0x29b7) & 0xffff);
        }
        result->prepareTicks += psp_audio_me_bench_elapsed(start);

        start = PspAudioMe_BenchReadCount();
        meCoreDcacheWritebackRange(local, 5 * 192 * sizeof(s16));
        vmeLibMemTo16(
            (u32) (uintptr_t) local[0], PSP_AUDIO_VME_TOP_WORD_OFFSET,
            192, VME_DMAC_TRANSFERT_WAIT_FINISH);
        vmeLibMemTo16(
            (u32) (uintptr_t) local[1], PSP_AUDIO_VME_BASE_WORD_OFFSET,
            192, VME_DMAC_TRANSFERT_NO_WAIT);
        vmeLibMemTo16(
            (u32) (uintptr_t) local[2],
            PSP_AUDIO_VME_BASE_WORD_OFFSET + PSP_AUDIO_VME_LANE_WORDS,
            192, VME_DMAC_TRANSFERT_WAIT_FINISH);
        vmeLibMemTo16(
            (u32) (uintptr_t) local[3],
            PSP_AUDIO_VME_BASE_WORD_OFFSET + 2 * PSP_AUDIO_VME_LANE_WORDS,
            192, VME_DMAC_TRANSFERT_NO_WAIT);
        vmeLibMemTo16(
            (u32) (uintptr_t) local[4],
            PSP_AUDIO_VME_BASE_WORD_OFFSET + 3 * PSP_AUDIO_VME_LANE_WORDS,
            192, VME_DMAC_TRANSFERT_WAIT_FINISH);
        meLibSync();
        result->transferInTicks += psp_audio_me_bench_elapsed(start);
        for (index = 0; index < 192; index++) {
            u32 output;

            if (top[index] != local[0][index]) {
                psp_audio_me_vme_boundary_mismatch(
                    result, -1, index, local[0][index], top[index]);
            }
            for (output = 0; output < 4; output++) {
                s32 actual =
                    base[output * PSP_AUDIO_VME_BUFFER_WORDS + index];

                if (actual != local[output + 1][index]) {
                    psp_audio_me_vme_boundary_mismatch(
                        result, -2 - output, index,
                        local[output + 1][index], actual);
                }
            }
        }

        volLeft = 0x7000;
        volRight = 0x5000;
        volWet = 0x3000;
        for (index = 0; index < 192; index++) {
            s32 input;
            s16 left;
            s16 right;

            if ((index != 0) && ((index & 7) == 0)) {
                volLeft += 0x0111;
                volRight += (u16) -0x00b3;
                volWet += 0x0057;
            }
            input = local[0][index];
            left = (s16) ((input * (s32) volLeft) >> 16);
            right = (s16) ((input * (s32) volRight) >> 16);
            top[index] = input;
            top[PSP_AUDIO_VME_BUFFER_WORDS + index] = input;
            top[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] = left;
            top[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] = right;
            base[index] = volLeft;
            base[PSP_AUDIO_VME_BUFFER_WORDS + index] = volRight;
            base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] = volWet;
            base[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] = volWet;
        }
        meLibSync();

        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(multiplyContext);
        result->setupTicks += psp_audio_me_bench_elapsed(start);
        start = PspAudioMe_BenchReadCount();
        vmeLibFinish();
        meLibSync();
        result->runTicks += psp_audio_me_bench_elapsed(start);

        volLeft = 0x7000;
        volRight = 0x5000;
        volWet = 0x3000;
        for (index = 0; index < 192; index++) {
            s32 input;
            s16 left;
            s16 right;
            s16 wetLeft;
            s16 wetRight;

            if ((index != 0) && ((index & 7) == 0)) {
                volLeft += 0x0111;
                volRight += (u16) -0x00b3;
                volWet += 0x0057;
            }
            input = local[0][index];
            left = (s16) ((input * (s32) volLeft) >> 16);
            right = (s16) ((input * (s32) volRight) >> 16);
            wetLeft = (s16) (((s32) left * volWet) >> 16);
            wetRight = (s16) (((s32) right * volWet) >> 16);
            top[index] = wetLeft;
            top[PSP_AUDIO_VME_BUFFER_WORDS + index] = wetRight;
            top[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] = left;
            top[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] = right;
            base[index] = local[1][index];
            base[PSP_AUDIO_VME_BUFFER_WORDS + index] = local[2][index];
            base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] = local[3][index];
            base[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] = local[4][index];
        }
        meLibSync();

        start = PspAudioMe_BenchReadCount();
        vmeLibStart();
        vmeLibLoadCustomContext(addContext);
        result->setupTicks += psp_audio_me_bench_elapsed(start);
        start = PspAudioMe_BenchReadCount();
        vmeLibFinish();
        meLibSync();
        result->runTicks += psp_audio_me_bench_elapsed(start);

        start = PspAudioMe_BenchReadCount();
        meCoreDcacheWritebackInvalidateRange(
            &local[5][0], 4 * 192 * sizeof(s16));
        vmeLibMemFrom16(
            (u32) (uintptr_t) local[5], PSP_AUDIO_VME_BASE_WORD_OFFSET,
            192, VME_DMAC_TRANSFERT_NO_WAIT);
        vmeLibMemFrom16(
            (u32) (uintptr_t) local[6],
            PSP_AUDIO_VME_BASE_WORD_OFFSET + PSP_AUDIO_VME_LANE_WORDS,
            192, VME_DMAC_TRANSFERT_WAIT_FINISH);
        vmeLibMemFrom16(
            (u32) (uintptr_t) local[7],
            PSP_AUDIO_VME_BASE_WORD_OFFSET + 2 * PSP_AUDIO_VME_LANE_WORDS,
            192, VME_DMAC_TRANSFERT_NO_WAIT);
        vmeLibMemFrom16(
            (u32) (uintptr_t) local[8],
            PSP_AUDIO_VME_BASE_WORD_OFFSET + 3 * PSP_AUDIO_VME_LANE_WORDS,
            192, VME_DMAC_TRANSFERT_WAIT_FINISH);
        meCoreDcacheInvalidateRange(&local[5][0], 4 * 192 * sizeof(s16));
        meLibSync();
        result->transferOutTicks += psp_audio_me_bench_elapsed(start);

        start = PspAudioMe_BenchReadCount();
        for (index = 0; index < 192; index++) {
            u32 output;

            for (output = 0; output < 4; output++) {
                sVmeEnvBoundaryOutput[output][index] =
                    local[5 + output][index];
            }
        }
        meCoreDcacheWritebackRange(
            sVmeEnvBoundaryOutput, sizeof(sVmeEnvBoundaryOutput));
        result->materializeTicks += psp_audio_me_bench_elapsed(start);

        volLeft = 0x7000;
        volRight = 0x5000;
        volWet = 0x3000;
        for (index = 0; index < 192; index++) {
            s32 input;
            s16 left;
            s16 right;
            s16 expected[4];
            u32 output;

            if ((index != 0) && ((index & 7) == 0)) {
                volLeft += 0x0111;
                volRight += (u16) -0x00b3;
                volWet += 0x0057;
            }
            input = local[0][index];
            left = (s16) ((input * (s32) volLeft) >> 16);
            right = (s16) ((input * (s32) volRight) >> 16);
            expected[0] = psp_audio_me_clamp16(
                local[1][index] +
                (s16) (((s32) left * volWet) >> 16));
            expected[1] = psp_audio_me_clamp16(
                local[2][index] +
                (s16) (((s32) right * volWet) >> 16));
            expected[2] = psp_audio_me_clamp16(local[3][index] + left);
            expected[3] = psp_audio_me_clamp16(local[4][index] + right);
            for (output = 0; output < 4; output++) {
                s32 actual = sVmeEnvBoundaryOutput[output][index];

                if (actual != expected[output]) {
                    psp_audio_me_vme_boundary_mismatch(
                        result, output, index, expected[output], actual);
                }
            }
        }
        result->calls++;
        if (result->mismatches != 0) {
            vmeLibWipe();
            meLibSync();
            return -1;
        }
    }
    vmeLibWipe();
    meLibSync();
    return 0;
}

static int psp_audio_me_vme_env_bench(void) {
    static const u32 sCases[PSP_AUDIO_VME_ENV_BENCH_CASES] = {
        192, 384, 768, 1536, 2048,
    };
    volatile s32* top = (volatile s32*) VME_TOP_BUFFERS;
    volatile s32* base = (volatile s32*) VME_BASE_BUFFERS;
    u32 caseIndex;

    for (caseIndex = 0; caseIndex < PSP_AUDIO_VME_ENV_BENCH_CASES;
         caseIndex++) {
        volatile PspAudioVmeEnvBenchResult* result =
            &PSP_AUDIO_VME_ENV_BENCH_STATS[caseIndex];
        PspAudioVmeEnvContextParam param;
        u32 count = sCases[caseIndex];
        u32 index;
        u32 run;
        u16 volLeft = 0x7000;
        u16 volRight = 0x5000;
        u16 volWet = 0x3000;

        result->samples = count;
        param.count = count;
        for (index = 0; index < count; index++) {
            PspAudioVmeEnvSample* sample = &sVmeEnvSamples[index];

            if ((index != 0) && ((index & 7) == 0)) {
                volLeft += 0x0111;
                volRight += (u16) -0x00b3;
                volWet += 0x0057;
            }
            sample->input =
                (s16) ((index * 1973 + caseIndex * 7919) & 0xffff);
            sample->volLeft = volLeft;
            sample->volRight = volRight;
            sample->volWet = volWet;
            sample->accumulator[0] =
                (s16) ((index * 3181 + 0x7211) & 0xffff);
            sample->accumulator[1] =
                (s16) ((index * 2377 + 0x1943) & 0xffff);
            sample->accumulator[2] =
                (s16) ((index * 1877 + 0x6d31) & 0xffff);
            sample->accumulator[3] =
                (s16) ((index * 1559 + 0x29b7) & 0xffff);
        }

        for (run = 0; run < PSP_AUDIO_VME_ENV_BENCH_REPEATS; run++) {
            void* multiplyContext;
            void* addContext;
            u32 start;

            volLeft = 0x7000;
            volRight = 0x5000;
            volWet = 0x3000;
            start = PspAudioMe_BenchReadCount();
            for (index = 0; index < count; index++) {
                PspAudioVmeEnvSample* sample = &sVmeEnvSamples[index];
                s32 input;
                s16 left;
                s16 right;
                s16 wetLeft;
                s16 wetRight;

                if ((index != 0) && ((index & 7) == 0)) {
                    volLeft += 0x0111;
                    volRight += (u16) -0x00b3;
                    volWet += 0x0057;
                }
                input = sample->input;
                left = (s16) ((input * (s32) volLeft) >> 16);
                right = (s16) ((input * (s32) volRight) >> 16);
                wetLeft = (s16) (((s32) left * volWet) >> 16);
                wetRight = (s16) (((s32) right * volWet) >> 16);
                sample->expected[0] = psp_audio_me_clamp16(
                    sample->accumulator[0] + wetLeft);
                sample->expected[1] = psp_audio_me_clamp16(
                    sample->accumulator[1] + wetRight);
                sample->expected[2] = psp_audio_me_clamp16(
                    sample->accumulator[2] + left);
                sample->expected[3] = psp_audio_me_clamp16(
                    sample->accumulator[3] + right);
            }
            result->scalarTicks += psp_audio_me_bench_elapsed(start);

            vmeLibWipe();
            for (index = 0; index < count; index++) {
                PspAudioVmeEnvSample* sample = &sVmeEnvSamples[index];
                s32 left = (s16) (((s32) sample->input *
                    (s32) sample->volLeft) >> 16);
                s32 right = (s16) (((s32) sample->input *
                    (s32) sample->volRight) >> 16);

                top[index] = sample->input;
                top[PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->input;
                top[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    left;
                top[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    right;
                base[index] = sample->volLeft;
                base[PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->volRight;
                base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->volWet;
                base[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->volWet;
            }
            meLibSync();
            multiplyContext =
                psp_audio_me_vme_env_multiply_context(&param);
            addContext = psp_audio_me_vme_env_add_context(&param);

            start = PspAudioMe_BenchReadCount();
            vmeLibStart();
            vmeLibLoadCustomContext(multiplyContext);
            vmeLibFinish();
            meLibSync();
            result->vmeTicks += psp_audio_me_bench_elapsed(start);

            start = PspAudioMe_BenchReadCount();
            for (index = 0; index < count; index++) {
                PspAudioVmeEnvSample* sample = &sVmeEnvSamples[index];
                s32 left = (s16) (((s32) sample->input *
                    (s32) sample->volLeft) >> 16);
                s32 right = (s16) (((s32) sample->input *
                    (s32) sample->volRight) >> 16);
                s32 expected[4];
                u32 output;

                expected[0] = left;
                expected[1] = right;
                expected[2] = (s16) ((left * sample->volWet) >> 16);
                expected[3] = (s16) ((right * sample->volWet) >> 16);
                for (output = 0; output < 4; output++) {
                    s32 actual =
                        base[output * PSP_AUDIO_VME_BUFFER_WORDS + index];

                    if (actual != expected[output]) {
                        if (result->multiplyMismatches == 0) {
                            result->multiplyFirstMismatch =
                                output * PSP_AUDIO_VME_ENV_MAX_SAMPLES +
                                index;
                            result->multiplyExpected = expected[output];
                            result->multiplyActual = actual;
                        }
                        result->multiplyMismatches++;
                    }
                }
            }
            result->validateTicks += psp_audio_me_bench_elapsed(start);

            vmeLibWipe();
            for (index = 0; index < count; index++) {
                PspAudioVmeEnvSample* sample = &sVmeEnvSamples[index];
                s32 left = (s16) (((s32) sample->input *
                    (s32) sample->volLeft) >> 16);
                s32 right = (s16) (((s32) sample->input *
                    (s32) sample->volRight) >> 16);
                s32 wetLeft = (s16) ((left * sample->volWet) >> 16);
                s32 wetRight = (s16) ((right * sample->volWet) >> 16);

                top[index] = wetLeft;
                top[PSP_AUDIO_VME_BUFFER_WORDS + index] = wetRight;
                top[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] = left;
                top[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] = right;
                base[index] = sample->accumulator[0];
                base[PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->accumulator[1];
                base[2 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->accumulator[2];
                base[3 * PSP_AUDIO_VME_BUFFER_WORDS + index] =
                    sample->accumulator[3];
            }
            meLibSync();

            start = PspAudioMe_BenchReadCount();
            vmeLibStart();
            vmeLibLoadCustomContext(addContext);
            vmeLibFinish();
            meLibSync();
            result->vmeTicks += psp_audio_me_bench_elapsed(start);

            start = PspAudioMe_BenchReadCount();
            for (index = 0; index < count; index++) {
                u32 output;

                for (output = 0; output < 4; output++) {
                    s32 actual =
                        base[output * PSP_AUDIO_VME_BUFFER_WORDS + index];
                    s32 expected =
                        sVmeEnvSamples[index].expected[output];

                    if (actual != expected) {
                        if (result->mismatches == 0) {
                            result->firstMismatch =
                                output * PSP_AUDIO_VME_ENV_MAX_SAMPLES +
                                index;
                            result->expected = expected;
                            result->actual = actual;
                        }
                        result->mismatches++;
                    }
                }
            }
            result->bestOffset = 0;
            result->offsetMismatches = 0xffffffff;
            {
                s32 offset;

                for (offset = -4; offset <= 4; offset++) {
                    u32 offsetMismatches = 0;
                    u32 output;

                    for (output = 0; output < 4; output++) {
                        for (index = 0; index < count; index++) {
                            s32 expectedIndex = (s32) index + offset;

                            if ((expectedIndex >= 0) &&
                                (expectedIndex < (s32) count) &&
                                (base[output * PSP_AUDIO_VME_BUFFER_WORDS +
                                      index] !=
                                 sVmeEnvSamples[expectedIndex]
                                     .expected[output])) {
                                offsetMismatches++;
                            }
                        }
                    }
                    if (offsetMismatches < result->offsetMismatches) {
                        result->bestOffset = offset;
                        result->offsetMismatches = offsetMismatches;
                    }
                }
            }
            result->validateTicks += psp_audio_me_bench_elapsed(start);
            result->calls++;
            if ((result->multiplyMismatches != 0) ||
                (result->mismatches != 0)) {
                return -1;
            }
        }
    }
    vmeLibWipe();
    meLibSync();
    return 0;
}
#endif

int PspAudioMe_ValidateVmeResample(u32 count, const s16* inputs,
                                   const s16* coefficients,
                                   const s16* output,
                                   const s16* expectedState,
                                   const s16* actualState,
                                   u32 prepareTicks, u32 scalarTicks) {
    volatile s32* vmeOutput = (volatile s32*) VME_BASE_BUFFERS;
#if PSP_AUDIO_VME_BENCH
    volatile PspAudioVmeResampleBenchResult* bench =
        PSP_AUDIO_VME_RESAMPLE_BENCH_STATS;
    u32 start;
#else
    (void) prepareTicks;
    (void) scalarTicks;
#endif
    u32 index;

    if (sVmeState != PSP_AUDIO_VME_READY) {
        return 0;
    }
    if ((count == 0) || (count > PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES) ||
        (inputs == NULL) || (coefficients == NULL) || (output == NULL) ||
        (expectedState == NULL) || (actualState == NULL)) {
        sVmeResampleSkipped++;
        return 0;
    }

    psp_audio_me_vme_resample_command(
        count, inputs, coefficients, PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES,
        PSP_AUDIO_VME_RESAMPLE_COEFF_OFFSET, 1, 0,
#if PSP_AUDIO_VME_BENCH
        PSP_AUDIO_VME_RESAMPLE_BENCH_STATS
#else
        NULL
#endif
    );
#if PSP_AUDIO_VME_BENCH
    start = PspAudioMe_BenchReadCount();
#endif
    for (index = 0; index < count; index++) {
        sVmeResampleOutput[index] = (s16) vmeOutput[index];
    }
#if PSP_AUDIO_VME_BENCH
    bench->readbackTicks += psp_audio_me_bench_elapsed(start);
    start = PspAudioMe_BenchReadCount();
#endif
    sVmeResampleCommands++;
    sVmeResampleProducts += count * PSP_AUDIO_VME_RESAMPLE_LANES;
    for (index = 0; index < count; index++) {
        s32 product0 =
            ((s32) inputs[index] * coefficients[index] + 0x4000) >> 15;
        s32 product1 =
            ((s32) inputs[PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES + index] *
                 coefficients[PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES + index] +
             0x4000) >> 15;
        s32 product2 =
            ((s32) inputs[2 * PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES + index] *
                 coefficients[2 * PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES + index] +
             0x4000) >> 15;
        s32 product3 =
            ((s32) inputs[3 * PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES + index] *
                 coefficients[3 * PSP_AUDIO_VME_RESAMPLE_MAX_SAMPLES + index] +
             0x4000) >> 15;
        s32 pair01 = product0 + product1;
        s32 pair23 = product2 + product3;
        s32 actual = sVmeResampleOutput[index];

        sVmeResampleOutputs++;
        if ((sVmeResamplePair01[index] != pair01) ||
            (sVmeResamplePair23[index] != pair23)) {
            if (sVmeResamplePairMismatches == 0) {
                sVmeResampleFirstPairIndex = index;
                sVmeResamplePair01Expected = (u32) pair01;
                sVmeResamplePair01Actual =
                    (u32) sVmeResamplePair01[index];
                sVmeResamplePair23Expected = (u32) pair23;
                sVmeResamplePair23Actual =
                    (u32) sVmeResamplePair23[index];
            }
            sVmeResamplePairMismatches++;
        }
        if (actual != output[index]) {
            if (sVmeResampleOutputMismatches == 0) {
                sVmeResampleFirstOutputIndex = index;
                sVmeResampleOutputExpected = (u32) (s32) output[index];
                sVmeResampleOutputActual = (u32) actual;
            }
            sVmeResampleOutputMismatches++;
        }
    }
    for (index = 0; index < 16; index++) {
        if (actualState[index] != expectedState[index]) {
            if (sVmeResampleStateMismatches == 0) {
                sVmeResampleFirstStateIndex = index;
                sVmeResampleStateExpected =
                    (u32) (s32) expectedState[index];
                sVmeResampleStateActual = (u32) (s32) actualState[index];
            }
            sVmeResampleStateMismatches++;
        }
    }
#if PSP_AUDIO_VME_BENCH
    bench->validateTicks += psp_audio_me_bench_elapsed(start);
    bench->calls++;
    bench->samples += count;
    bench->prepareTicks +=
        prepareTicks > sVmeBenchReadOverhead ?
            prepareTicks - sVmeBenchReadOverhead : 0;
    bench->scalarTicks +=
        scalarTicks > sVmeBenchReadOverhead ?
            scalarTicks - sVmeBenchReadOverhead : 0;
#endif
    if ((sVmeResamplePairMismatches != 0) ||
        (sVmeResampleOutputMismatches != 0) ||
        (sVmeResampleStateMismatches != 0)) {
        vmeLibDisable();
        sVmeState = PSP_AUDIO_VME_FAULT;
        meLibSync();
        return -1;
    }
#if PSP_AUDIO_VME_BENCH
    start = PspAudioMe_BenchReadCount();
#endif
    vmeLibWipe();
    meLibSync();
#if PSP_AUDIO_VME_BENCH
    bench->wipeTicks += psp_audio_me_bench_elapsed(start);
#endif
    return 1;
}
#else
u32 PspAudioMe_BenchReadCount(void) {
    return 0;
}

int PspAudioMe_ValidateVmeMix(u16 count, s16 gain, const s16* input,
                              const s16* output) {
    (void) count;
    (void) gain;
    (void) input;
    (void) output;
    return 0;
}

int PspAudioMe_ValidateVmeResample(u32 count, const s16* inputs,
                                   const s16* coefficients,
                                   const s16* output,
                                   const s16* expectedState,
                                   const s16* actualState,
                                   u32 prepareTicks, u32 scalarTicks) {
    (void) count;
    (void) inputs;
    (void) coefficients;
    (void) output;
    (void) expectedState;
    (void) actualState;
    (void) prepareTicks;
    (void) scalarTicks;
    return 0;
}

#endif

__attribute__((noinline, aligned(4))) void meLibOnException(void) {
    sMeState = PSP_AUDIO_ME_FAULT;
    meLibSync();
    if (sMeCompletionEnabled) {
        meLibSendExternalSoftInterrupt();
    }
    meLibHalt();
}

__attribute__((noinline, aligned(4))) void meLibOnExternalInterrupt(void) {
    sMeState = PSP_AUDIO_ME_FAULT;
    meLibSync();
    if (sMeCompletionEnabled) {
        meLibSendExternalSoftInterrupt();
    }
    meLibHalt();
}

__attribute__((noinline, aligned(4))) void meLibOnProcess(void) {
    sMeProgress = 1;
    meLibSync();

    while (sMeState == PSP_AUDIO_ME_BOOTING) {
        meLibDelayPipeline();
    }

    psp_audio_me_invalidate_range_me(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
    sMeProgress = PSP_AUDIO_ME_READY;
    meLibSync();

    while (sMeState != PSP_AUDIO_ME_STOP) {
        if (sMeState == PSP_AUDIO_ME_RUN) {
            const Acmd* commands = (const Acmd*) (uintptr_t) sMeCommands;
            s32 commandCount = (s32) sMeCommandCount;

#if defined(PSP_LOG_ENABLED) && PSP_LOG_ENABLED
            meLibDcacheWritebackInvalidateAll();
#else
            psp_audio_me_invalidate_inputs_me(commands, commandCount);
#endif
            sMeResult = PspAudioMixer_ExecuteCommandListMe(commands, commandCount);
            if ((sMeResult == 0) && !PspAudioMixer_ValidateState()) {
                sMeResult = -3;
            }
#if defined(PSP_LOG_ENABLED) && PSP_LOG_ENABLED
            meLibDcacheWritebackInvalidateAll();
#else
            psp_audio_me_writeback_outputs_me(commands, commandCount);
#endif
            meLibSync();
            sMeState = PSP_AUDIO_ME_IDLE;
            meLibSync();
            if (sMeCompletionEnabled) {
                meLibSendExternalSoftInterrupt();
            }
#if PSP_AUDIO_VME
        } else if (sMeState == PSP_AUDIO_ME_VME_SMOKE) {
            sMeResult = psp_audio_me_vme_smoke();
            sMeState = PSP_AUDIO_ME_IDLE;
            meLibSync();
            if (sMeCompletionEnabled) {
                meLibSendExternalSoftInterrupt();
            }
#endif
        } else {
            meLibDelayPipeline();
        }
    }

    sMeState = PSP_AUDIO_ME_HALTED;
    meLibSync();
    meLibHalt();
}

int PspAudioMe_Boot(void) {
    if (sBootStarted) {
        return sBootResult;
    }

    sMeCommands = 0;
    sMeCommandCount = 0;
    sMeResult = 0;
    sMeProgress = 0;
    sMeCompletionEnabled = 0;
#if PSP_AUDIO_VME
    sVmeState = PSP_AUDIO_VME_UNINITIALIZED;
    sVmeCheckpoint = 0;
    sVmeTimedOut = 0;
#if PSP_AUDIO_VME_VALIDATE
    sVmeMixCalls = 0;
    sVmeMixSamples = 0;
    sVmeMixMismatches = 0;
    sVmeMixFirstIndex = (u32) -1;
    sVmeMixInput = 0;
    sVmeMixOldOutput = 0;
    sVmeMixGain = 0;
    sVmeMixExpected = 0;
    sVmeMixActual = 0;
    sVmeFilterRuns = 0;
    sVmeFilterOutputs = 0;
    sVmeFilterMismatches = 0;
    sVmeFilterFirstCase = (u32) -1;
    sVmeFilterFirstIndex = (u32) -1;
    sVmeFilterExpected = 0;
    sVmeFilterActual = 0;
    sVmeResampleRuns = 0;
    sVmeResampleProducts = 0;
    sVmeResampleMismatches = 0;
    sVmeResampleFirstLane = (u32) -1;
    sVmeResampleFirstIndex = (u32) -1;
    sVmeResampleInput = 0;
    sVmeResampleCoefficient = 0;
    sVmeResampleExpected = 0;
    sVmeResampleActual = 0;
    sVmeResampleCommands = 0;
    sVmeResampleOutputs = 0;
    sVmeResamplePairMismatches = 0;
    sVmeResampleOutputMismatches = 0;
    sVmeResampleStateMismatches = 0;
    sVmeResampleSkipped = 0;
    sVmeResampleFirstOutputIndex = (u32) -1;
    sVmeResampleOutputExpected = 0;
    sVmeResampleOutputActual = 0;
    sVmeResampleFirstPairIndex = (u32) -1;
    sVmeResamplePair01Expected = 0;
    sVmeResamplePair01Actual = 0;
    sVmeResamplePair23Expected = 0;
    sVmeResamplePair23Actual = 0;
    sVmeResampleFirstStateIndex = (u32) -1;
    sVmeResampleStateExpected = 0;
    sVmeResampleStateActual = 0;
#endif
#endif
    sMeState = PSP_AUDIO_ME_BOOTING;
    meLibSync();

    sBootStarted = 1;
    sBootResult = meLibDefaultInit();
    if (sBootResult < 0) {
        sLastError = sBootResult;
    }
    return sBootResult;
}

int PspAudioMe_Init(void) {
    u32 start;
    int result;

    if (sInitialized) {
        return 0;
    }

    result = PspAudioMe_Boot();
    if (result < 0) {
        return result;
    }

    psp_audio_me_enable_completion();
    psp_audio_me_writeback_range_cpu(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
    sMeState = PSP_AUDIO_ME_IDLE;
    meLibSync();
    start = sceKernelGetSystemTimeLow();
    while (sMeProgress != PSP_AUDIO_ME_READY) {
        if ((sceKernelGetSystemTimeLow() - start) >= PSP_AUDIO_ME_TIMEOUT_US) {
            sMeState = PSP_AUDIO_ME_STOP;
            meLibSync();
            sLastError = -1;
            return sLastError;
        }
        sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
    }

    sInitialized = 1;
#if PSP_AUDIO_VME
    psp_audio_me_drain_completion();
    sMeResult = 0;
    sMeState = PSP_AUDIO_ME_VME_SMOKE;
    meLibSync();
    start = sceKernelGetSystemTimeLow();
    while (sMeState == PSP_AUDIO_ME_VME_SMOKE) {
        if ((sceKernelGetSystemTimeLow() - start) >= PSP_AUDIO_ME_TIMEOUT_US) {
            meLibEmitSoftwareInterrupt();
            sVmeTimedOut = 1;
            sInitialized = 0;
            sLastError = -4;
            return sLastError;
        }
        sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
    }
    if (sMeState != PSP_AUDIO_ME_IDLE) {
        sVmeTimedOut = 1;
        sInitialized = 0;
        sLastError = -4;
        return sLastError;
    }
#endif
    return 0;
}

static s32 psp_audio_me_is_running(void) {
    return sMeState == PSP_AUDIO_ME_RUN;
}

static void psp_audio_me_wait(PspAudioProfileWaitReason reason) {
#if PSP_AUDIO_PROFILE
    u32 waitStart;
    s32 blocked;
#endif

#if !PSP_AUDIO_PROFILE
    (void) reason;
#endif

    if (!sPending) {
        return;
    }

#if PSP_AUDIO_PROFILE
    blocked = psp_audio_me_is_running();
    waitStart = sceKernelGetSystemTimeLow();
#endif

    while (psp_audio_me_is_running()) {
        if (sCompletionReady) {
            u32 elapsed = sceKernelGetSystemTimeLow() - sPendingStart;
            SceUInt timeout;

            if (elapsed >= PSP_AUDIO_ME_TIMEOUT_US) {
                break;
            }
            timeout = PSP_AUDIO_ME_TIMEOUT_US - elapsed;
            sceKernelWaitSema(sCompletionSema, 1, &timeout);
            continue;
        }
        if ((sceKernelGetSystemTimeLow() - sPendingStart) >= PSP_AUDIO_ME_TIMEOUT_US) {
            break;
        }
        sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
    }

    if (psp_audio_me_is_running()) {
        u32 interruptStart;

        meLibEmitSoftwareInterrupt();
        interruptStart = sceKernelGetSystemTimeLow();
        while (psp_audio_me_is_running() &&
               ((sceKernelGetSystemTimeLow() - interruptStart) < PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US)) {
            if (sCompletionReady) {
                SceUInt timeout = PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US;

                sceKernelWaitSema(sCompletionSema, 1, &timeout);
            } else {
                sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
            }
        }
    }

    PspAudioProfile_RecordCompletion(sceKernelGetSystemTimeLow() - sPendingStart);
    if (sMeState == PSP_AUDIO_ME_IDLE) {
        psp_audio_me_invalidate_writes();
        sLastError = (s32) sMeResult;
        if (sLastError < 0) {
            psp_audio_me_invalidate_range_cpu(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
        }
    } else if (sMeState == PSP_AUDIO_ME_FAULT) {
        sceKernelDcacheWritebackInvalidateAll();
        PspAudioProfile_RecordFallback();
        sLastError = PspAudioMixer_ExecuteCommandList(sPendingCommands, sPendingCommandCount);
        if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
            sLastError = -3;
        }
        psp_audio_me_reset_ranges();
        sInitialized = 0;
    } else {
        sLastError = -2;
        psp_audio_me_reset_ranges();
        sInitialized = 0;
    }

    if (sLastError < 0) {
        sInitialized = 0;
    }
    sPending = 0;
#if PSP_AUDIO_PROFILE
    PspAudioProfile_RecordWait(reason, blocked,
                               sceKernelGetSystemTimeLow() - waitStart);
    PspAudioProfile_Report();
#endif
}

void PspAudioMe_Wait(void) {
    psp_audio_me_wait(PSP_AUDIO_PROFILE_WAIT_PUBLIC);
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    if ((commands == NULL) || (commandCount <= 0)) {
        return;
    }

    psp_audio_me_wait(PSP_AUDIO_PROFILE_WAIT_SUBMIT);
    if (!sInitialized || (sMeState != PSP_AUDIO_ME_IDLE)) {
        PspAudioProfile_RecordFallback();
        sLastError = PspAudioMixer_ExecuteCommandList(commands, commandCount);
        if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
            sLastError = -3;
        }
        return;
    }

    psp_audio_me_collect_ranges(commands, commandCount);
    psp_audio_me_writeback_inputs();
    psp_audio_me_drain_completion();
    sMeCommands = (u32) (uintptr_t) commands;
    sMeCommandCount = commandCount;
    sMeResult = 0;
    sPendingCommands = commands;
    sPendingCommandCount = commandCount;
    sPendingStart = sceKernelGetSystemTimeLow();
    sPending = 1;
    meLibSync();
    sMeState = PSP_AUDIO_ME_RUN;
    meLibSync();
}

int PspAudioMe_IsActive(void) {
    return sInitialized;
}

int PspAudioMe_GetLastError(void) {
    return sLastError;
}

void PspAudioMe_GetVmeSmokeResult(PspAudioVmeSmokeResult* result) {
    if (result == NULL) {
        return;
    }
#if PSP_AUDIO_VME
    result->state = sVmeTimedOut ? PSP_AUDIO_VME_FAULT : (PspAudioVmeState) sVmeState;
    result->checkpoint = sVmeCheckpoint;
    result->runs = sVmeRuns;
    result->samples = sVmeSamples;
    result->mismatches = sVmeMismatches;
    result->firstIndex = (s32) sVmeFirstIndex;
    result->input = (s32) sVmeInput;
    result->factor = (s32) sVmeFactor;
    result->expected = (s32) sVmeExpected;
    result->actual = (s32) sVmeActual;
#else
    result->state = PSP_AUDIO_VME_DISABLED;
    result->checkpoint = 0;
    result->runs = 0;
    result->samples = 0;
    result->mismatches = 0;
    result->firstIndex = -1;
    result->input = 0;
    result->factor = 0;
    result->expected = 0;
    result->actual = 0;
#endif
}

void PspAudioMe_GetVmeMixResult(PspAudioVmeMixResult* result) {
    if (result == NULL) {
        return;
    }
#if PSP_AUDIO_VME_VALIDATE
    result->calls = sVmeMixCalls;
    result->samples = sVmeMixSamples;
    result->mismatches = sVmeMixMismatches;
    result->firstIndex = (s32) sVmeMixFirstIndex;
    result->input = (s32) sVmeMixInput;
    result->oldOutput = (s32) sVmeMixOldOutput;
    result->gain = (s32) sVmeMixGain;
    result->expected = (s32) sVmeMixExpected;
    result->actual = (s32) sVmeMixActual;
#else
    result->calls = 0;
    result->samples = 0;
    result->mismatches = 0;
    result->firstIndex = -1;
    result->input = 0;
    result->oldOutput = 0;
    result->gain = 0;
    result->expected = 0;
    result->actual = 0;
#endif
}

void PspAudioMe_GetVmeFilterResult(PspAudioVmeFilterResult* result) {
    if (result == NULL) {
        return;
    }
#if PSP_AUDIO_VME_VALIDATE
    result->runs = sVmeFilterRuns;
    result->outputs = sVmeFilterOutputs;
    result->mismatches = sVmeFilterMismatches;
    result->firstCase = (s32) sVmeFilterFirstCase;
    result->firstIndex = (s32) sVmeFilterFirstIndex;
    result->expected = (s32) sVmeFilterExpected;
    result->actual = (s32) sVmeFilterActual;
#else
    result->runs = 0;
    result->outputs = 0;
    result->mismatches = 0;
    result->firstCase = -1;
    result->firstIndex = -1;
    result->expected = 0;
    result->actual = 0;
#endif
}

void PspAudioMe_GetVmeResampleResult(PspAudioVmeResampleResult* result) {
    if (result == NULL) {
        return;
    }
#if PSP_AUDIO_VME_VALIDATE
    result->runs = sVmeResampleRuns;
    result->products = sVmeResampleProducts;
    result->mismatches = sVmeResampleMismatches;
    result->firstLane = (s32) sVmeResampleFirstLane;
    result->firstIndex = (s32) sVmeResampleFirstIndex;
    result->input = (s32) sVmeResampleInput;
    result->coefficient = (s32) sVmeResampleCoefficient;
    result->expected = (s32) sVmeResampleExpected;
    result->actual = (s32) sVmeResampleActual;
    result->commands = sVmeResampleCommands;
    result->outputs = sVmeResampleOutputs;
    result->pairMismatches = sVmeResamplePairMismatches;
    result->outputMismatches = sVmeResampleOutputMismatches;
    result->stateMismatches = sVmeResampleStateMismatches;
    result->skipped = sVmeResampleSkipped;
    result->firstOutputIndex = (s32) sVmeResampleFirstOutputIndex;
    result->outputExpected = (s32) sVmeResampleOutputExpected;
    result->outputActual = (s32) sVmeResampleOutputActual;
    result->firstPairIndex = (s32) sVmeResampleFirstPairIndex;
    result->pair01Expected = (s32) sVmeResamplePair01Expected;
    result->pair01Actual = (s32) sVmeResamplePair01Actual;
    result->pair23Expected = (s32) sVmeResamplePair23Expected;
    result->pair23Actual = (s32) sVmeResamplePair23Actual;
    result->firstStateIndex = (s32) sVmeResampleFirstStateIndex;
    result->stateExpected = (s32) sVmeResampleStateExpected;
    result->stateActual = (s32) sVmeResampleStateActual;
#else
    result->runs = 0;
    result->products = 0;
    result->mismatches = 0;
    result->firstLane = -1;
    result->firstIndex = -1;
    result->input = 0;
    result->coefficient = 0;
    result->expected = 0;
    result->actual = 0;
    result->commands = 0;
    result->outputs = 0;
    result->pairMismatches = 0;
    result->outputMismatches = 0;
    result->stateMismatches = 0;
    result->skipped = 0;
    result->firstOutputIndex = -1;
    result->outputExpected = 0;
    result->outputActual = 0;
    result->firstPairIndex = -1;
    result->pair01Expected = 0;
    result->pair01Actual = 0;
    result->pair23Expected = 0;
    result->pair23Actual = 0;
    result->firstStateIndex = -1;
    result->stateExpected = 0;
    result->stateActual = 0;
#endif
}

void PspAudioMe_RecordScalarMix(u32 samples, u32 ticks) {
#if PSP_AUDIO_VME_BENCH
    if ((sVmeBenchLastRow == NULL) ||
        (sVmeBenchLastRow->samples != samples)) {
        return;
    }
    sVmeBenchLastRow->scalarCalls++;
    sVmeBenchLastRow->scalarTicks +=
        ticks > sVmeBenchReadOverhead ? ticks - sVmeBenchReadOverhead : 0;
    sVmeBenchLastRow = NULL;
#else
    (void) samples;
    (void) ticks;
#endif
}

void PspAudioMe_GetVmeBenchRow(u32 index, PspAudioVmeBenchRow* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    if (index < PSP_AUDIO_VME_BENCH_ROWS) {
        volatile u32* in = (volatile u32*) &PSP_AUDIO_VME_BENCH_STATS[index];

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
        return;
    }
#else
    (void) index;
#endif
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeResampleBenchResult(
    PspAudioVmeResampleBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    {
        volatile u32* in =
            (volatile u32*) PSP_AUDIO_VME_RESAMPLE_BENCH_STATS;

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
    }
#else
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
#endif
}

void PspAudioMe_GetVmeResampleBatchBenchResult(
    PspAudioVmeResampleBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    {
        volatile u32* in =
            (volatile u32*) PSP_AUDIO_VME_RESAMPLE_BATCH_BENCH_STATS;

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
    }
#else
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
#endif
}

void PspAudioMe_GetVmeResampleDmacBenchResult(
    PspAudioVmeResampleBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    {
        volatile u32* in =
            (volatile u32*) PSP_AUDIO_VME_RESAMPLE_DMAC_BENCH_STATS;

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
    }
#else
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
#endif
}

void PspAudioMe_GetVmeEnvBenchResult(
    u32 index, PspAudioVmeEnvBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    if (index < PSP_AUDIO_VME_ENV_BENCH_CASES) {
        volatile u32* in =
            (volatile u32*) &PSP_AUDIO_VME_ENV_BENCH_STATS[index];

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
        return;
    }
#else
    (void) index;
#endif
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeTransportBenchResult(
    u32 index, PspAudioVmeTransportBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    if (index < PSP_AUDIO_VME_TRANSPORT_BENCH_CASES) {
        volatile u32* in = (volatile u32*)
            &PSP_AUDIO_VME_TRANSPORT_BENCH_STATS[index];

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
        return;
    }
#else
    (void) index;
#endif
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeEnvBoundaryBenchResult(
    PspAudioVmeEnvBoundaryBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    {
        volatile u32* in =
            (volatile u32*) PSP_AUDIO_VME_ENV_BOUNDARY_BENCH_STATS;

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
    }
#else
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
#endif
}

void PspAudioMe_GetVmeEnvPipelineResult(
    PspAudioVmeEnvPipelineResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
#if PSP_AUDIO_VME_BENCH
    {
        volatile u32* in =
            (volatile u32*) PSP_AUDIO_VME_ENV_PIPELINE_STATS;

        for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
            out[i] = in[i];
        }
    }
#else
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
#endif
}

#else

int PspAudioMe_Boot(void) {
    return 0;
}

int PspAudioMe_Init(void) {
    return 0;
}

void PspAudioMe_Wait(void) {
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    (void) commands;
    (void) commandCount;
}

int PspAudioMe_IsActive(void) {
    return 0;
}

int PspAudioMe_GetLastError(void) {
    return 0;
}

u32 PspAudioMe_BenchReadCount(void) {
    return 0;
}

int PspAudioMe_ValidateVmeMix(u16 count, s16 gain, const s16* input,
                              const s16* output) {
    (void) count;
    (void) gain;
    (void) input;
    (void) output;
    return 0;
}

int PspAudioMe_ValidateVmeResample(u32 count, const s16* inputs,
                                   const s16* coefficients,
                                   const s16* output,
                                   const s16* expectedState,
                                   const s16* actualState,
                                   u32 prepareTicks, u32 scalarTicks) {
    (void) count;
    (void) inputs;
    (void) coefficients;
    (void) output;
    (void) expectedState;
    (void) actualState;
    (void) prepareTicks;
    (void) scalarTicks;
    return 0;
}

void PspAudioMe_GetVmeSmokeResult(PspAudioVmeSmokeResult* result) {
    if (result == NULL) {
        return;
    }
    result->state = PSP_AUDIO_VME_DISABLED;
    result->checkpoint = 0;
    result->runs = 0;
    result->samples = 0;
    result->mismatches = 0;
    result->firstIndex = -1;
    result->input = 0;
    result->factor = 0;
    result->expected = 0;
    result->actual = 0;
}

void PspAudioMe_GetVmeMixResult(PspAudioVmeMixResult* result) {
    if (result == NULL) {
        return;
    }
    result->calls = 0;
    result->samples = 0;
    result->mismatches = 0;
    result->firstIndex = -1;
    result->input = 0;
    result->oldOutput = 0;
    result->gain = 0;
    result->expected = 0;
    result->actual = 0;
}

void PspAudioMe_GetVmeFilterResult(PspAudioVmeFilterResult* result) {
    if (result == NULL) {
        return;
    }
    result->runs = 0;
    result->outputs = 0;
    result->mismatches = 0;
    result->firstCase = -1;
    result->firstIndex = -1;
    result->expected = 0;
    result->actual = 0;
}

void PspAudioMe_GetVmeResampleResult(PspAudioVmeResampleResult* result) {
    if (result == NULL) {
        return;
    }
    result->runs = 0;
    result->products = 0;
    result->mismatches = 0;
    result->firstLane = -1;
    result->firstIndex = -1;
    result->input = 0;
    result->coefficient = 0;
    result->expected = 0;
    result->actual = 0;
    result->commands = 0;
    result->outputs = 0;
    result->pairMismatches = 0;
    result->outputMismatches = 0;
    result->stateMismatches = 0;
    result->skipped = 0;
    result->firstOutputIndex = -1;
    result->outputExpected = 0;
    result->outputActual = 0;
    result->firstPairIndex = -1;
    result->pair01Expected = 0;
    result->pair01Actual = 0;
    result->pair23Expected = 0;
    result->pair23Actual = 0;
    result->firstStateIndex = -1;
    result->stateExpected = 0;
    result->stateActual = 0;
}

void PspAudioMe_RecordScalarMix(u32 samples, u32 ticks) {
    (void) samples;
    (void) ticks;
}

void PspAudioMe_GetVmeBenchRow(u32 index, PspAudioVmeBenchRow* result) {
    volatile u32* out;
    u32 i;

    (void) index;
    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeResampleBenchResult(
    PspAudioVmeResampleBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeResampleBatchBenchResult(
    PspAudioVmeResampleBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeResampleDmacBenchResult(
    PspAudioVmeResampleBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeEnvBenchResult(
    u32 index, PspAudioVmeEnvBenchResult* result) {
    volatile u32* out;
    u32 i;

    (void) index;
    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeTransportBenchResult(
    u32 index, PspAudioVmeTransportBenchResult* result) {
    volatile u32* out;
    u32 i;

    (void) index;
    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeEnvBoundaryBenchResult(
    PspAudioVmeEnvBoundaryBenchResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

void PspAudioMe_GetVmeEnvPipelineResult(
    PspAudioVmeEnvPipelineResult* result) {
    volatile u32* out;
    u32 i;

    if (result == NULL) {
        return;
    }
    out = (volatile u32*) result;
    for (i = 0; i < sizeof(*result) / sizeof(u32); i++) {
        out[i] = 0;
    }
}

#endif
