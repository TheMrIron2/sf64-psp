#include <pspkernel.h>
#include <pspintrman.h>
#include <pspthreadman.h>

#include "src/psp/audio_me.h"
#include "src/psp/audio_mixer.h"
#include "src/psp/gfx/gfx_me_replay.h"

#include <stdio.h>

void PspPlatform_LogLine(const char* line);

#ifndef PSP_AUDIO
#define PSP_AUDIO 0
#endif
#ifndef PSP_GFX_ME_REPLAY
#define PSP_GFX_ME_REPLAY 0
#endif

#if PSP_AUDIO || PSP_GFX_ME_REPLAY
#include <me-core-mapper/me-core.h>

#define PSP_AUDIO_ME_TIMEOUT_US 250000
#define PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US 10000
#define PSP_AUDIO_ME_POLL_US 100
#define PSP_AUDIO_ME_READY 0x100
#define PSP_AUDIO_ME_UNCACHED 0x40000000
#define PSP_AUDIO_ME_CACHE_LINE_SIZE 64
#define PSP_AUDIO_ME_MAX_INPUT_RANGES 16
#define PSP_AUDIO_ME_MAX_WRITE_RANGES 512
#define PSP_GFX_ME_POOL_SIZE 0x2AD50

typedef enum {
    PSP_AUDIO_ME_BOOTING,
    PSP_AUDIO_ME_IDLE,
    PSP_AUDIO_ME_RUN,
    PSP_AUDIO_ME_STOP,
    PSP_AUDIO_ME_HALTED,
    PSP_AUDIO_ME_FAULT,
} PspAudioMeState;

typedef enum {
    PSP_ME_JOB_NONE,
    PSP_ME_JOB_AUDIO,
    PSP_ME_JOB_GFX_REPLAY,
} PspMeJob;

enum {
    PSP_AUDIO_ME_SHARED_STATE,
    PSP_AUDIO_ME_SHARED_COMMANDS,
    PSP_AUDIO_ME_SHARED_COMMAND_COUNT,
    PSP_AUDIO_ME_SHARED_RESULT,
    PSP_AUDIO_ME_SHARED_PROGRESS,
    PSP_AUDIO_ME_SHARED_COMPLETION_ENABLED,
    PSP_AUDIO_ME_SHARED_JOB,
    PSP_AUDIO_ME_SHARED_GFX_SOURCE_BASE,
    PSP_AUDIO_ME_SHARED_GFX_SNAPSHOT_BASE,
    PSP_AUDIO_ME_SHARED_GFX_SNAPSHOT_SIZE,
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
#define sMeJob PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_JOB]
#define sMeGfxSourceBase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_SOURCE_BASE]
#define sMeGfxSnapshotBase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_SNAPSHOT_BASE]
#define sMeGfxSnapshotSize PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_SNAPSHOT_SIZE]

static volatile PspGfxMeReplayStats sGfxReplayResultStorage
    __attribute__((aligned(64), section(".uncached")));
#define PSP_GFX_ME_REPLAY_RESULT \
    ((volatile PspGfxMeReplayStats*) \
        (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) &sGfxReplayResultStorage))

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
static PspMeJob sPendingJob;
static s32 sLastError;
#if PSP_GFX_ME_REPLAY
static PspGfxMeReplayStats sPendingGfxExpected;
static u32 sPendingGfxTaskIndex;
static s32 sPendingGfxCountResult;
static s32 sPendingGfxLogResult;
static const void* sPendingGfxSourcePool;
static u32 sGfxReplayMatches;
static u32 sGfxReplayMismatches;
static u32 sGfxReplaySkippedBusy;
static s32 sGfxReplaySubmissionStarted;
static s32 sGfxReplayInactiveLogged;
#endif
static SceUID sCompletionSema = -1;
#if PSP_GFX_ME_REPLAY
static SceUID sDispatchSema = -1;
#endif
static s32 sCompletionReady;
#if PSP_AUDIO
static PspAudioMeCacheRange sInputRanges[PSP_AUDIO_ME_MAX_INPUT_RANGES];
#endif
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

static void psp_me_dispatch_lock(void) {
#if PSP_GFX_ME_REPLAY
    if (sDispatchSema > 0) {
        sceKernelWaitSema(sDispatchSema, 1, NULL);
    }
#endif
}

static void psp_me_dispatch_unlock(void) {
#if PSP_GFX_ME_REPLAY
    if (sDispatchSema > 0) {
        sceKernelSignalSema(sDispatchSema, 1);
    }
#endif
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

#if PSP_AUDIO
static u32 psp_audio_me_command_dma_size(u32 w0) {
    return ((w0 >> 16) & 0xFF) << 4;
}
#endif

static void psp_audio_me_reset_ranges(void) {
    sInputRangeCount = 0;
    sInputRangeOverflow = false;
    sWriteRangeCount = 0;
    sWriteRangeOverflow = false;
}

#if PSP_AUDIO
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
#endif

#if PSP_AUDIO
static void psp_audio_me_record_input(const void* address, u32 size) {
    psp_audio_me_record_range(sInputRanges, &sInputRangeCount, &sInputRangeOverflow,
                              PSP_AUDIO_ME_MAX_INPUT_RANGES, (void*) address, size);
}
#endif

#if PSP_AUDIO
static void psp_audio_me_record_write(void* address, u32 size) {
    psp_audio_me_record_range(sWriteRanges, &sWriteRangeCount, &sWriteRangeOverflow,
                              PSP_AUDIO_ME_MAX_WRITE_RANGES, address, size);
}
#endif

#if PSP_AUDIO
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
#endif

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

#if PSP_AUDIO && (!defined(PSP_LOG_ENABLED) || !PSP_LOG_ENABLED)
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

#if PSP_GFX_ME_REPLAY
static void psp_gfx_me_publish_result(const PspGfxMeReplayStats* result) {
    volatile u32* dst = (volatile u32*) PSP_GFX_ME_REPLAY_RESULT;
    const u32* src = (const u32*) result;
    u32 i;

    for (i = 0; i < (sizeof(*result) / sizeof(u32)); i++) {
        dst[i] = src[i];
    }
}

static void psp_gfx_me_read_result(PspGfxMeReplayStats* result) {
    const volatile u32* src = (const volatile u32*) PSP_GFX_ME_REPLAY_RESULT;
    u32* dst = (u32*) result;
    u32 i;

    for (i = 0; i < (sizeof(*result) / sizeof(u32)); i++) {
        dst[i] = src[i];
    }
}

static int psp_gfx_me_stats_match(const PspGfxMeReplayStats* expected,
                                  const PspGfxMeReplayStats* actual) {
    return (expected->commandCount == actual->commandCount) &&
           (expected->nestedDlCount == actual->nestedDlCount) &&
           (expected->gvtxCommandCount == actual->gvtxCommandCount) &&
           (expected->loadedVertexCount == actual->loadedVertexCount) &&
           (expected->matrixCommandCount == actual->matrixCommandCount) &&
           (expected->tri1CommandCount == actual->tri1CommandCount) &&
           (expected->tri2CommandCount == actual->tri2CommandCount) &&
           (expected->inputTriangleCount == actual->inputTriangleCount) &&
           (expected->textureRectangleCount == actual->textureRectangleCount) &&
           (expected->commandHash == actual->commandHash) &&
           (expected->commandLimitHit == actual->commandLimitHit) &&
           (expected->depthLimitHit == actual->depthLimitHit) &&
           (expected->transformedVertexCount == actual->transformedVertexCount) &&
           (expected->transformHash == actual->transformHash);
}

static int psp_gfx_me_structure_matches(const PspGfxMeReplayStats* expected,
                                        const PspGfxMeReplayStats* actual) {
    return (expected->commandCount == actual->commandCount) &&
           (expected->nestedDlCount == actual->nestedDlCount) &&
           (expected->gvtxCommandCount == actual->gvtxCommandCount) &&
           (expected->loadedVertexCount == actual->loadedVertexCount) &&
           (expected->matrixCommandCount == actual->matrixCommandCount) &&
           (expected->tri1CommandCount == actual->tri1CommandCount) &&
           (expected->tri2CommandCount == actual->tri2CommandCount) &&
           (expected->inputTriangleCount == actual->inputTriangleCount) &&
           (expected->textureRectangleCount == actual->textureRectangleCount) &&
           (expected->commandHash == actual->commandHash) &&
           (expected->commandLimitHit == actual->commandLimitHit) &&
           (expected->depthLimitHit == actual->depthLimitHit) &&
           (expected->transformedVertexCount == actual->transformedVertexCount);
}

static void psp_gfx_me_report_result(void) {
    PspGfxMeReplayStats actual;
    char line[512];
    int match;
    int structureMatch;

    psp_gfx_me_read_result(&actual);
    match = (sLastError == 0) && psp_gfx_me_stats_match(&sPendingGfxExpected, &actual);
    structureMatch = psp_gfx_me_structure_matches(&sPendingGfxExpected, &actual);
    if (sPendingGfxCountResult) {
        if (match) {
            sGfxReplayMatches++;
        } else {
            sGfxReplayMismatches++;
        }
    }
    if ((sLastError != 0) || !structureMatch ||
        (sPendingGfxCountResult && sPendingGfxLogResult &&
         ((sPendingGfxTaskIndex <= 4) || ((sPendingGfxTaskIndex % 30) == 0)))) {
        snprintf(line, sizeof(line),
                 "[psp-me-gfx] task=%lu %s cmds=%lu/%lu dl=%lu/%lu vtx=%lu/%lu "
                 "loaded=%lu/%lu mtx=%lu/%lu tri1=%lu/%lu tri2=%lu/%lu hash=%08lx/%08lx "
                 "xvtx=%lu/%lu xhash=%08lx/%08lx matches=%lu mismatches=%lu skips=%lu result=%ld",
                 (unsigned long) sPendingGfxTaskIndex, match ? "match" : "MISMATCH",
                 (unsigned long) actual.commandCount, (unsigned long) sPendingGfxExpected.commandCount,
                 (unsigned long) actual.nestedDlCount, (unsigned long) sPendingGfxExpected.nestedDlCount,
                 (unsigned long) actual.gvtxCommandCount,
                 (unsigned long) sPendingGfxExpected.gvtxCommandCount,
                 (unsigned long) actual.loadedVertexCount,
                 (unsigned long) sPendingGfxExpected.loadedVertexCount,
                 (unsigned long) actual.matrixCommandCount,
                 (unsigned long) sPendingGfxExpected.matrixCommandCount,
                 (unsigned long) actual.tri1CommandCount,
                 (unsigned long) sPendingGfxExpected.tri1CommandCount,
                 (unsigned long) actual.tri2CommandCount,
                 (unsigned long) sPendingGfxExpected.tri2CommandCount,
                 (unsigned long) actual.commandHash,
                 (unsigned long) sPendingGfxExpected.commandHash,
                 (unsigned long) actual.transformedVertexCount,
                 (unsigned long) sPendingGfxExpected.transformedVertexCount,
                 (unsigned long) actual.transformHash,
                 (unsigned long) sPendingGfxExpected.transformHash,
                 (unsigned long) sGfxReplayMatches, (unsigned long) sGfxReplayMismatches,
                 (unsigned long) sGfxReplaySkippedBusy,
                 (long) sLastError);
        PspPlatform_LogLine(line);
    }
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
            if (0) {
#if PSP_AUDIO
            } else if (sMeJob == PSP_ME_JOB_AUDIO) {
                const Acmd* commands = (const Acmd*) (uintptr_t) sMeCommands;
                s32 commandCount = (s32) sMeCommandCount;

#if defined(PSP_LOG_ENABLED) && PSP_LOG_ENABLED
                meLibDcacheWritebackInvalidateAll();
#else
                psp_audio_me_invalidate_inputs_me(commands, commandCount);
#endif
                sMeResult = PspAudioMixer_ExecuteCommandList(commands, commandCount);
                if ((sMeResult == 0) && !PspAudioMixer_ValidateState()) {
                    sMeResult = -3;
                }
#if defined(PSP_LOG_ENABLED) && PSP_LOG_ENABLED
                meLibDcacheWritebackInvalidateAll();
#else
                psp_audio_me_writeback_outputs_me(commands, commandCount);
#endif
#endif
#if PSP_GFX_ME_REPLAY
            } else if (sMeJob == PSP_ME_JOB_GFX_REPLAY) {
                PspGfxMeReplayStats result;

                psp_audio_me_invalidate_range_me(
                    (const void*) (uintptr_t) sMeGfxSnapshotBase,
                    sMeGfxSnapshotSize);
                sMeResult = PspGfxMeReplay_Walk(
                    (const Gfx*) (uintptr_t) sMeCommands, &result,
                    (const void*) (uintptr_t) sMeGfxSourceBase,
                    (const void*) (uintptr_t) sMeGfxSnapshotBase,
                    sMeGfxSnapshotSize);
                psp_gfx_me_publish_result(&result);
#endif
            } else {
                sMeResult = -4;
            }
            meLibSync();
            sMeJob = PSP_ME_JOB_NONE;
            sMeState = PSP_AUDIO_ME_IDLE;
            meLibSync();
            if (sMeCompletionEnabled) {
                meLibSendExternalSoftInterrupt();
            }
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
    sMeJob = PSP_ME_JOB_NONE;
    sMeGfxSourceBase = 0;
    sMeGfxSnapshotBase = 0;
    sMeGfxSnapshotSize = 0;
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

#if PSP_GFX_ME_REPLAY
    if (sDispatchSema < 0) {
        sDispatchSema = sceKernelCreateSema("sf64_me_dispatch", 0, 1, 1, NULL);
        if (sDispatchSema <= 0) {
            sLastError = sDispatchSema;
            sDispatchSema = -1;
            return sLastError;
        }
    }
#endif

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
#if PSP_GFX_ME_REPLAY
#if PSP_AUDIO
    PspPlatform_LogLine("[psp-me-gfx] direct-pool quarter-rate matrix validation v13 active");
#else
    PspPlatform_LogLine("[psp-me-gfx] direct-pool full-rate graphics-only matrix validation v13 active");
#endif
#endif
    return 0;
}

static s32 psp_audio_me_is_running(void) {
    return sMeState == PSP_AUDIO_ME_RUN;
}

static void psp_audio_me_wait_locked(void) {
    if (!sPending) {
        return;
    }

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

    if (sMeState == PSP_AUDIO_ME_IDLE) {
        sLastError = (s32) sMeResult;
        if (sPendingJob == PSP_ME_JOB_AUDIO) {
            psp_audio_me_invalidate_writes();
            if (sLastError < 0) {
                psp_audio_me_invalidate_range_cpu(PspAudioMixer_GetStateAddress(),
                                                  PspAudioMixer_GetStateSize());
            }
#if PSP_GFX_ME_REPLAY
        } else if (sPendingJob == PSP_ME_JOB_GFX_REPLAY) {
            psp_gfx_me_report_result();
#endif
        }
    } else if (sMeState == PSP_AUDIO_ME_FAULT) {
        sceKernelDcacheWritebackInvalidateAll();
        if (sPendingJob == PSP_ME_JOB_AUDIO) {
            sLastError = PspAudioMixer_ExecuteCommandList(sPendingCommands, sPendingCommandCount);
            if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
                sLastError = -3;
            }
        } else {
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
    sPendingJob = PSP_ME_JOB_NONE;
}

void PspAudioMe_Wait(void) {
    psp_me_dispatch_lock();
    psp_audio_me_wait_locked();
    psp_me_dispatch_unlock();
}

void PspMe_WaitGfxReplayPool(const void* task) {
#if PSP_GFX_ME_REPLAY
    if (task == NULL) {
        return;
    }
    psp_me_dispatch_lock();
    if (sPending && (sPendingJob == PSP_ME_JOB_GFX_REPLAY) &&
        (sPendingGfxSourcePool == task)) {
        psp_audio_me_wait_locked();
    }
    psp_me_dispatch_unlock();
#else
    (void) task;
#endif
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
#if !PSP_AUDIO
    (void) commands;
    (void) commandCount;
    return;
#else
    if ((commands == NULL) || (commandCount <= 0)) {
        return;
    }

    psp_me_dispatch_lock();
    psp_audio_me_wait_locked();
    if (!sInitialized || (sMeState != PSP_AUDIO_ME_IDLE)) {
        sLastError = PspAudioMixer_ExecuteCommandList(commands, commandCount);
        if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
            sLastError = -3;
        }
        psp_me_dispatch_unlock();
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
    sPendingJob = PSP_ME_JOB_AUDIO;
    sPending = 1;
    meLibSync();
    sMeJob = PSP_ME_JOB_AUDIO;
    sMeState = PSP_AUDIO_ME_RUN;
    meLibSync();
    psp_me_dispatch_unlock();
#endif
}

#if PSP_GFX_ME_REPLAY
static void psp_gfx_me_start_locked(const Gfx* dl, u32 taskIndex,
                                    const PspGfxMeReplayStats* expected,
                                    const void* sourceBase, const void* snapshotBase,
                                    u32 snapshotSize, s32 countResult,
                                    s32 logResult) {
    psp_audio_me_drain_completion();
    sMeCommands = (u32) (uintptr_t) dl;
    sMeCommandCount = 0;
    sMeResult = 0;
    sMeGfxSourceBase = (u32) (uintptr_t) sourceBase;
    sMeGfxSnapshotBase = (u32) (uintptr_t) snapshotBase;
    sMeGfxSnapshotSize = snapshotSize;
    sPendingGfxExpected = *expected;
    sPendingGfxTaskIndex = taskIndex;
    sPendingGfxCountResult = countResult;
    sPendingGfxLogResult = logResult;
    sPendingGfxSourcePool = sourceBase;
    sPendingStart = sceKernelGetSystemTimeLow();
    sPendingJob = PSP_ME_JOB_GFX_REPLAY;
    sPending = 1;
    meLibSync();
    sMeJob = PSP_ME_JOB_GFX_REPLAY;
    sMeState = PSP_AUDIO_ME_RUN;
    meLibSync();
}

#endif

void PspMe_SubmitGfxReplay(const void* task, const Gfx* dl, u32 taskIndex,
                           const PspGfxMeReplayStats* expected) {
#if PSP_GFX_ME_REPLAY
    uintptr_t dlOffset;

    if ((task == NULL) || (dl == NULL) || (expected == NULL)) {
        return;
    }
    dlOffset = (uintptr_t) dl - (uintptr_t) task;
    if (dlOffset >= PSP_GFX_ME_POOL_SIZE) {
        return;
    }

    if (!sGfxReplaySubmissionStarted) {
        PspPlatform_LogLine("[psp-me-gfx] title replay submission begin");
        sGfxReplaySubmissionStarted = 1;
    }

    psp_me_dispatch_lock();
    if (sPending && psp_audio_me_is_running()) {
        sGfxReplaySkippedBusy++;
        psp_me_dispatch_unlock();
        return;
    }
    psp_audio_me_wait_locked();
    if (!sInitialized || (sMeState != PSP_AUDIO_ME_IDLE)) {
        if (!sGfxReplayInactiveLogged) {
            char line[128];

            snprintf(line, sizeof(line),
                     "[psp-me-gfx] replay inactive task=%lu initialized=%ld state=%lu error=%ld",
                     (unsigned long) taskIndex, (long) sInitialized,
                     (unsigned long) sMeState, (long) sLastError);
            PspPlatform_LogLine(line);
            sGfxReplayInactiveLogged = 1;
        }
        psp_me_dispatch_unlock();
        return;
    }

    psp_gfx_me_start_locked(
        dl, taskIndex, expected,
        task, task, PSP_GFX_ME_POOL_SIZE, 1, 1);
    psp_me_dispatch_unlock();
#else
    (void) task;
    (void) dl;
    (void) taskIndex;
    (void) expected;
#endif
}

int PspAudioMe_IsActive(void) {
    return sInitialized;
}

int PspAudioMe_GetLastError(void) {
    return sLastError;
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

void PspMe_WaitGfxReplayPool(const void* task) {
    (void) task;
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    (void) commands;
    (void) commandCount;
}

void PspMe_SubmitGfxReplay(const void* task, const Gfx* dl, u32 taskIndex,
                           const PspGfxMeReplayStats* expected) {
    (void) task;
    (void) dl;
    (void) taskIndex;
    (void) expected;
}

int PspAudioMe_IsActive(void) {
    return 0;
}

int PspAudioMe_GetLastError(void) {
    return 0;
}

#endif
