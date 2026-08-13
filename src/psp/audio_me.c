#include <pspkernel.h>
#include <pspintrman.h>
#include <pspthreadman.h>

#include "src/psp/audio_me.h"
#include "src/psp/audio_mixer.h"
#include "src/psp/gfx/gfx_me_replay.h"

#include <stddef.h>
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
#define PSP_GFX_ME_READY_QUEUE_CAPACITY 256
#define PSP_GFX_ME_READY_VERTEX_CAPACITY 4096

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
    PSP_ME_JOB_GFX_READY,
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
    PSP_AUDIO_ME_SHARED_GFX_TRACE_BASE,
    PSP_AUDIO_ME_SHARED_GFX_TRACE_CAPACITY,
    PSP_AUDIO_ME_SHARED_GFX_TRACE_PUBLISHED,
    PSP_AUDIO_ME_SHARED_GFX_TRIANGLE_BASE,
    PSP_AUDIO_ME_SHARED_GFX_TRIANGLE_CAPACITY,
    PSP_AUDIO_ME_SHARED_GFX_TRIANGLE_PUBLISHED,
    PSP_AUDIO_ME_SHARED_GFX_VME_STAGE,
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
#define sMeGfxTraceBase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_TRACE_BASE]
#define sMeGfxTraceCapacity PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_TRACE_CAPACITY]
#define sMeGfxTracePublishedBase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_TRACE_PUBLISHED]
#define sMeGfxTriangleBase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_TRIANGLE_BASE]
#define sMeGfxTriangleCapacity PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_TRIANGLE_CAPACITY]
#define sMeGfxTrianglePublishedBase PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_TRIANGLE_PUBLISHED]
#define sMeGfxVmeStage PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_GFX_VME_STAGE]

static volatile PspGfxMeReplayStats sGfxReplayResultStorage
    __attribute__((aligned(64), section(".uncached")));
#define PSP_GFX_ME_REPLAY_RESULT \
    ((volatile PspGfxMeReplayStats*) \
        (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) &sGfxReplayResultStorage))

#if PSP_GFX_ME_REPLAY
static PspGfxMeReadyPacket sGfxReadyQueueStorage[PSP_GFX_ME_READY_QUEUE_CAPACITY]
    __attribute__((aligned(64), section(".uncached")));
static PspGfxMeVertex sGfxReadyVertexStorage[PSP_GFX_ME_READY_VERTEX_CAPACITY]
    __attribute__((aligned(64), section(".uncached")));
static u32 sGfxReadyVertexCount;
static volatile u32 sGfxReadyPublishedStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile u32 sGfxReadyConsumedStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile u32 sGfxReadyAppliedStorage
    __attribute__((aligned(64), section(".uncached")));
static volatile u32 sGfxReadyDoneStorage
    __attribute__((aligned(64), section(".uncached")));
#define PSP_GFX_READY_QUEUE \
    ((volatile PspGfxMeReadyPacket*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) sGfxReadyQueueStorage))
#define PSP_GFX_READY_PUBLISHED \
    (*(volatile u32*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) &sGfxReadyPublishedStorage))
#define PSP_GFX_READY_CONSUMED \
    (*(volatile u32*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) &sGfxReadyConsumedStorage))
#define PSP_GFX_READY_APPLIED \
    (*(volatile u32*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) &sGfxReadyAppliedStorage))
#define PSP_GFX_READY_DONE \
    (*(volatile u32*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) &sGfxReadyDoneStorage))

static PspGfxMeTransformTrace
    sGfxReplayTraceStorage[2][PSP_GFX_ME_TRANSFORM_TRACE_CAPACITY]
        __attribute__((aligned(64), section(".uncached")));
#define sGfxReplayTrace(slot) \
    ((PspGfxMeTransformTrace*) \
        (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) sGfxReplayTraceStorage[slot]))

static PspGfxMeTriangleCode
    sGfxReplayTriangleStorage[2][PSP_GFX_ME_TRIANGLE_TRACE_CAPACITY]
        __attribute__((aligned(64), section(".uncached")));
#define sGfxReplayTriangles(slot) \
    ((PspGfxMeTriangleCode*) \
        (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) sGfxReplayTriangleStorage[slot]))

static volatile u32 sGfxReplayTracePublishedStorage[2]
    __attribute__((aligned(64), section(".uncached")));
static volatile u32 sGfxReplayTrianglePublishedStorage[2]
    __attribute__((aligned(64), section(".uncached")));
#define sGfxReplayTracePublished(slot) \
    ((volatile u32*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) &sGfxReplayTracePublishedStorage[slot]))
#define sGfxReplayTrianglePublished(slot) \
    ((volatile u32*) (PSP_AUDIO_ME_UNCACHED | \
        (u32) (uintptr_t) &sGfxReplayTrianglePublishedStorage[slot]))
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
static PspMeJob sPendingJob;
static s32 sLastError;
#if PSP_GFX_ME_REPLAY
static PspGfxMeReplayStats sPendingGfxExpected;
static const PspGfxMeTransformTrace* sPendingGfxExpectedTrace;
static u32 sPendingGfxTaskIndex;
static s32 sPendingGfxCountResult;
static s32 sPendingGfxLogResult;
static const void* sPendingGfxSourcePool;
static const void* sGfxReplaySlotPool[2];
static u32 sPendingGfxSlot;
static u32 sGfxReplayNextSlot;
static u32 sGfxReplayWithinFine;
static u32 sGfxReplayWithinCoarse;
static u32 sGfxReplayOverCoarse;
static u32 sGfxReplayStructuralMismatches;
static u32 sGfxReplayMaxErrorQ16;
static u32 sGfxReplaySkippedBusy;
static u32 sGfxReplayPrivateLitVertices;
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

#if PSP_GFX_ME_REPLAY
static u32 psp_gfx_me_ready_color(u32 r, u32 g, u32 b, u32 a,
                                  int premultiplied) {
    if (premultiplied) {
        u32 value;

        value = r * a;
        r = (value + 1U + (value >> 8)) >> 8;
        value = g * a;
        g = (value + 1U + (value >> 8)) >> 8;
        value = b * a;
        b = (value + 1U + (value >> 8)) >> 8;
    }
    return r | (g << 8) | (b << 16) | (a << 24);
}

static void psp_gfx_me_ready_build(const PspGfxMeReadyPacket* packet) {
    PspGfxMeReadyVertex* destination =
        (PspGfxMeReadyVertex*) packet->output;
    int pretransformed = (packet->flags & PSP_GFX_ME_READY_PRETRANSFORMED) != 0;
    int depthBias = (packet->flags & PSP_GFX_ME_READY_DEPTH_BIAS) != 0;
    int premultiplied = (packet->flags & PSP_GFX_ME_READY_PREMULTIPLIED) != 0;
    int shade = (packet->flags & PSP_GFX_ME_READY_SHADE) != 0;
    u32 i;

    for (i = 0; i < packet->vertexCount; i++) {
        const PspGfxMeVertex* src =
            (const PspGfxMeVertex*) (PSP_AUDIO_ME_UNCACHED |
                (u32) (uintptr_t) packet->source[i]);
        PspGfxMeReadyVertex* dst = &destination[i];
        u32 r = src->r;
        u32 g = src->g;
        u32 b = src->b;
        u32 a = src->a;

        if (pretransformed) {
            float inverseW = 1.0f / src->clipW;

            dst->x = src->clipX * inverseW;
            dst->y = src->clipY * inverseW;
            dst->z = src->clipZ * inverseW;
        } else {
            dst->x = src->viewX;
            dst->y = src->viewY;
            dst->z = src->viewZ;
        }
        if (depthBias) {
            dst->z += pretransformed ? -0.0005f : 0.5f;
        }
        if ((packet->combineMode == 2) || (packet->combineMode == 7) || !shade) {
            r = packet->primitive[0];
            g = packet->primitive[1];
            b = packet->primitive[2];
            a = packet->primitive[3];
        } else if (packet->combineMode == 8) {
            u32 value;

            value = r * packet->primitive[0];
            r = (value + 1U + (value >> 8)) >> 8;
            value = g * packet->primitive[1];
            g = (value + 1U + (value >> 8)) >> 8;
            value = b * packet->primitive[2];
            b = (value + 1U + (value >> 8)) >> 8;
            a = packet->primitive[3];
        } else if (packet->combineMode == 9) {
            if ((packet->flags & PSP_GFX_ME_READY_BAKED_ENV) != 0) {
                r = g = b = 255;
            } else {
                r = packet->environment[0];
                g = packet->environment[1];
                b = packet->environment[2];
            }
            a = packet->primitive[3];
        } else if ((packet->combineMode == 3) || (packet->combineMode == 4)) {
            r = g = b = 255;
        } else if (packet->combineMode == 5) {
            a = 255;
        }
        dst->color = psp_gfx_me_ready_color(r, g, b, a, premultiplied);
        dst->u = (float) src->s * packet->textureScaleS - packet->textureOffsetS;
        dst->v = (float) src->t * packet->textureScaleT - packet->textureOffsetT;
    }
}

static void psp_gfx_me_ready_run(void) {
    while (!PSP_GFX_READY_DONE ||
           (PSP_GFX_READY_CONSUMED != PSP_GFX_READY_PUBLISHED)) {
        u32 consumed = PSP_GFX_READY_CONSUMED;

        if (consumed == PSP_GFX_READY_PUBLISHED) {
            meLibSync();
            continue;
        }
        psp_gfx_me_ready_build(
            (const PspGfxMeReadyPacket*)
                &PSP_GFX_READY_QUEUE[consumed % PSP_GFX_ME_READY_QUEUE_CAPACITY]);
        meLibSync();
        PSP_GFX_READY_CONSUMED = consumed + 1;
    }
}
#endif

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
           (expected->transformedVertexCount == actual->transformedVertexCount) &&
           (expected->transformTraceCount == actual->transformTraceCount) &&
           (expected->transformTraceOverflow == actual->transformTraceOverflow) &&
           (actual->transformTraceOverflow == 0);
}

static float psp_gfx_me_abs(float value) {
    return value < 0.0f ? -value : value;
}

static u32 psp_gfx_me_error_q16(float error) {
    if ((error != error) || (error >= 65535.999f)) {
        return 0xFFFFFFFFU;
    }
    return (u32) (error * 65536.0f + 0.5f);
}

static int psp_gfx_me_trace_max_error(const PspGfxMeReplayStats* actual,
                                      float* maxError) {
    const PspGfxMeTransformTrace* expected = sPendingGfxExpectedTrace;
    const PspGfxMeTransformTrace* result = sGfxReplayTrace(sPendingGfxSlot);
    u32 count = actual->transformTraceCount;
    u32 i;

    if ((expected == NULL) || (count > PSP_GFX_ME_TRANSFORM_TRACE_CAPACITY)) {
        return 0;
    }
    *maxError = 0.0f;
    for (i = 0; i < count; i++) {
        u32 componentCount;
        u32 component;

        if ((expected[i].slot != result[i].slot) ||
            (expected[i].flags != result[i].flags)) {
            return 0;
        }
        componentCount = (expected[i].flags & PSP_GFX_ME_TRANSFORM_VALID) ? 8 : 4;
        for (component = 0; component < componentCount; component++) {
            float expectedValue = component < 4 ? expected[i].view[component] :
                                                  expected[i].clip[component - 4];
            float resultValue = component < 4 ? result[i].view[component] :
                                                result[i].clip[component - 4];
            float error = psp_gfx_me_abs(expectedValue - resultValue);

            if (error != error) {
                *maxError = error;
                return 1;
            }
            if (error > *maxError) {
                *maxError = error;
            }
        }
    }
    return 1;
}

static void psp_gfx_me_report_result(void) {
    PspGfxMeReplayStats actual;
    char line[512];
    int structureMatch;
    float maxError = 0.0f;
    u32 maxErrorQ16;

    psp_gfx_me_read_result(&actual);
    sGfxReplayPrivateLitVertices += actual.privateLitVertexCount;
    if (!sPendingGfxCountResult) {
        return;
    }
    structureMatch = psp_gfx_me_structure_matches(&sPendingGfxExpected, &actual);
    if ((sLastError == 0) && structureMatch) {
        structureMatch = psp_gfx_me_trace_max_error(&actual, &maxError);
    }
    maxErrorQ16 = psp_gfx_me_error_q16(maxError);
    if (maxErrorQ16 > sGfxReplayMaxErrorQ16) {
        sGfxReplayMaxErrorQ16 = maxErrorQ16;
    }
    if (sPendingGfxCountResult) {
        if ((sLastError != 0) || !structureMatch) {
            sGfxReplayStructuralMismatches++;
        } else if (maxError <= (1.0f / 256.0f)) {
            sGfxReplayWithinFine++;
        } else if (maxError <= (1.0f / 16.0f)) {
            sGfxReplayWithinCoarse++;
        } else {
            sGfxReplayOverCoarse++;
        }
    }
    if ((sLastError != 0) || !structureMatch ||
        (sPendingGfxCountResult && sPendingGfxLogResult &&
         ((sPendingGfxTaskIndex <= 4) || ((sPendingGfxTaskIndex % 30) == 0)))) {
        snprintf(line, sizeof(line),
                 "[psp-me-gfx] task=%lu %s cmds=%lu/%lu dl=%lu/%lu vtx=%lu/%lu "
                 "loaded=%lu/%lu mtx=%lu/%lu tri1=%lu/%lu tri2=%lu/%lu hash=%08lx/%08lx "
                 "xvtx=%lu/%lu trace=%lu/%lu errorQ16=%lu fine=%lu coarse=%lu over=%lu "
                 "struct=%lu skips=%lu result=%ld",
                 (unsigned long) sPendingGfxTaskIndex,
                 ((sLastError == 0) && structureMatch &&
                  (maxError <= (1.0f / 16.0f))) ? "match" : "MISMATCH",
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
                 (unsigned long) actual.transformTraceCount,
                 (unsigned long) sPendingGfxExpected.transformTraceCount,
                 (unsigned long) maxErrorQ16,
                 (unsigned long) sGfxReplayWithinFine,
                 (unsigned long) sGfxReplayWithinCoarse,
                 (unsigned long) sGfxReplayOverCoarse,
                 (unsigned long) sGfxReplayStructuralMismatches,
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

    meLibExceptionHandlerInit(0);

    while (sMeState == PSP_AUDIO_ME_BOOTING) {
        meLibDelayPipeline();
    }

    psp_audio_me_invalidate_range_me(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
#if PSP_GFX_ME_REPLAY
    PspGfxMeReplay_VmeInit(&sMeGfxVmeStage);
#endif
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
            } else if (sMeJob == PSP_ME_JOB_GFX_READY) {
                psp_gfx_me_ready_run();
                sMeResult = 0;
            } else if (sMeJob == PSP_ME_JOB_GFX_REPLAY) {
                PspGfxMeReplayStats result;
                volatile u32* tracePublished =
                    (volatile u32*) (uintptr_t) sMeGfxTracePublishedBase;
                volatile u32* trianglePublished =
                    (volatile u32*) (uintptr_t) sMeGfxTrianglePublishedBase;
                u32 traceCount;

                psp_audio_me_invalidate_range_me(
                    (const void*) (uintptr_t) sMeGfxSnapshotBase,
                    sMeGfxSnapshotSize);
                sMeResult = PspGfxMeReplay_Walk(
                    (const Gfx*) (uintptr_t) sMeCommands, &result,
                    (const void*) (uintptr_t) sMeGfxSourceBase,
                    (const void*) (uintptr_t) sMeGfxSnapshotBase,
                    sMeGfxSnapshotSize,
                    (PspGfxMeTransformTrace*) (uintptr_t) sMeGfxTraceBase,
                    sMeGfxTraceCapacity, tracePublished,
                    (PspGfxMeTriangleCode*) (uintptr_t) sMeGfxTriangleBase,
                    sMeGfxTriangleCapacity, trianglePublished,
                    &sMeGfxVmeStage);
                traceCount = result.transformTraceCount < sMeGfxTraceCapacity ?
                                 result.transformTraceCount : sMeGfxTraceCapacity;
                meLibSync();
                *tracePublished = PSP_GFX_ME_TRACE_DONE | traceCount;
                meLibSync();
                *trianglePublished = PSP_GFX_ME_TRACE_DONE |
                    (result.triangleResultCount < sMeGfxTriangleCapacity ?
                         result.triangleResultCount : sMeGfxTriangleCapacity);
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
    sMeGfxTraceBase = 0;
    sMeGfxTraceCapacity = 0;
    sMeGfxTracePublishedBase = 0;
    sMeGfxTriangleBase = 0;
    sMeGfxTriangleCapacity = 0;
    sMeGfxTrianglePublishedBase = 0;
    sMeGfxVmeStage = 0;
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
    PspPlatform_LogLine("[psp-me-gfx] reciprocal GE-ready packets v43 active");
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
    if (sPending && ((sPendingJob == PSP_ME_JOB_GFX_REPLAY) ||
                     (sPendingJob == PSP_ME_JOB_GFX_READY)) &&
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
                                    const PspGfxMeTransformTrace* expectedTrace,
                                    const void* sourceBase, const void* snapshotBase,
                                    u32 snapshotSize, s32 countResult,
                                    s32 logResult) {
    u32 slot = sGfxReplayNextSlot;

    sGfxReplayNextSlot ^= 1U;
    psp_audio_me_drain_completion();
    sMeCommands = (u32) (uintptr_t) dl;
    sMeCommandCount = 0;
    sMeResult = 0;
    sMeGfxSourceBase = (u32) (uintptr_t) sourceBase;
    sMeGfxSnapshotBase = (u32) (uintptr_t) snapshotBase;
    sMeGfxSnapshotSize = snapshotSize;
    sMeGfxTraceBase = (u32) (uintptr_t) sGfxReplayTrace(slot);
    sMeGfxTraceCapacity = PSP_GFX_ME_TRANSFORM_TRACE_CAPACITY;
    *sGfxReplayTracePublished(slot) = 0;
    sMeGfxTracePublishedBase =
        (u32) (uintptr_t) sGfxReplayTracePublished(slot);
    sMeGfxTriangleBase = (u32) (uintptr_t) sGfxReplayTriangles(slot);
    sMeGfxTriangleCapacity = PSP_GFX_ME_TRIANGLE_TRACE_CAPACITY;
    *sGfxReplayTrianglePublished(slot) = 0;
    sMeGfxTrianglePublishedBase =
        (u32) (uintptr_t) sGfxReplayTrianglePublished(slot);
    sPendingGfxExpected = *expected;
    sPendingGfxExpectedTrace = expectedTrace;
    sPendingGfxTaskIndex = taskIndex;
    sPendingGfxCountResult = countResult;
    sPendingGfxLogResult = logResult;
    sPendingGfxSourcePool = sourceBase;
    sPendingGfxSlot = slot;
    sGfxReplaySlotPool[slot] = sourceBase;
    sPendingStart = sceKernelGetSystemTimeLow();
    sPendingJob = PSP_ME_JOB_GFX_REPLAY;
    sPending = 1;
    meLibSync();
    sMeJob = PSP_ME_JOB_GFX_REPLAY;
    sMeState = PSP_AUDIO_ME_RUN;
    meLibSync();
}

#endif

int PspMe_StartGfxFrontend(const void* task, const Gfx* dl, u32 taskIndex) {
#if PSP_GFX_ME_REPLAY
    uintptr_t dlOffset;
    int result = -1;

    if ((task == NULL) || (dl == NULL)) {
        return -1;
    }
    dlOffset = (uintptr_t) dl - (uintptr_t) task;
    if (dlOffset >= PSP_GFX_ME_POOL_SIZE) {
        return -1;
    }

    psp_me_dispatch_lock();
    psp_audio_me_wait_locked();
    if (sInitialized && (sMeState == PSP_AUDIO_ME_IDLE)) {
        psp_audio_me_drain_completion();
        PSP_GFX_READY_PUBLISHED = 0;
        PSP_GFX_READY_CONSUMED = 0;
        PSP_GFX_READY_APPLIED = 0;
        sGfxReadyVertexCount = 0;
        PSP_GFX_READY_DONE = 0;
        sPendingGfxSourcePool = task;
        sPendingGfxTaskIndex = taskIndex;
        sPendingStart = sceKernelGetSystemTimeLow();
        sPendingJob = PSP_ME_JOB_GFX_READY;
        sPending = 1;
        meLibSync();
        sMeJob = PSP_ME_JOB_GFX_READY;
        sMeState = PSP_AUDIO_ME_RUN;
        meLibSync();
        result = 0;
    }
    psp_me_dispatch_unlock();
    return result;
#else
    (void) task;
    (void) dl;
    (void) taskIndex;
    return -1;
#endif
}

int PspMe_StageGfxReadyVertices(const PspGfxMeVertex* vertices, u32 count,
                                const PspGfxMeVertex** staged) {
#if PSP_GFX_ME_REPLAY
    PspGfxMeVertex* destination;
    u32 i;

    if ((vertices == NULL) || (staged == NULL) || (count == 0) ||
        !sPending || (sPendingJob != PSP_ME_JOB_GFX_READY) ||
        ((sGfxReadyVertexCount + count) > PSP_GFX_ME_READY_VERTEX_CAPACITY)) {
        return 0;
    }
    destination = &sGfxReadyVertexStorage[sGfxReadyVertexCount];
    for (i = 0; i < count; i++) {
        destination[i] = vertices[i];
        staged[i] = &destination[i];
    }
    sceKernelDcacheWritebackRange(destination, count * sizeof(*destination));
    sGfxReadyVertexCount += count;
    return 1;
#else
    (void) vertices;
    (void) count;
    (void) staged;
    return 0;
#endif
}

int PspMe_SubmitGfxReadyPacket(const PspGfxMeReadyPacket* packet) {
#if PSP_GFX_ME_REPLAY
    volatile u32* destination;
    const u32* source;
    u32 i;
    u32 published;

    if ((packet == NULL) || !sPending ||
        (sPendingJob != PSP_ME_JOB_GFX_READY)) {
        return 0;
    }
    published = PSP_GFX_READY_PUBLISHED;
    while ((published - PSP_GFX_READY_APPLIED) >=
           PSP_GFX_ME_READY_QUEUE_CAPACITY) {
        if (!psp_audio_me_is_running()) {
            return 0;
        }
    }
    destination = (volatile u32*)
        &PSP_GFX_READY_QUEUE[published % PSP_GFX_ME_READY_QUEUE_CAPACITY];
    source = (const u32*) packet;
    for (i = 0; i < (offsetof(PspGfxMeReadyPacket, output) / sizeof(u32)); i++) {
        destination[i] = source[i];
    }
    __asm__ volatile("sync" ::: "memory");
    PSP_GFX_READY_PUBLISHED = published + 1;
    return 1;
#else
    (void) packet;
    return 0;
#endif
}

void PspMe_WaitGfxReadyPackets(void) {
#if PSP_GFX_ME_REPLAY
    u32 published;

    if (!sPending || (sPendingJob != PSP_ME_JOB_GFX_READY)) {
        return;
    }
    published = PSP_GFX_READY_PUBLISHED;

    while (PSP_GFX_READY_CONSUMED != published) {
        if (!psp_audio_me_is_running()) {
            break;
        }
    }
    while (PSP_GFX_READY_APPLIED != PSP_GFX_READY_CONSUMED) {
        u32 applied = PSP_GFX_READY_APPLIED;
        PspGfxMeReadyPacket* packet =
            &sGfxReadyQueueStorage[applied % PSP_GFX_ME_READY_QUEUE_CAPACITY];
        PspGfxMeReadyVertex* destination =
            (PspGfxMeReadyVertex*) packet->destination;
        u32 i;

        sceKernelDcacheInvalidateRange(packet, sizeof(*packet));
        for (i = 0; i < packet->vertexCount; i++) {
            destination[i] = packet->output[i];
        }
        PSP_GFX_READY_APPLIED = applied + 1;
    }
#endif
}

void PspMe_FinishGfxReadyPackets(void) {
#if PSP_GFX_ME_REPLAY
    if (!sPending || (sPendingJob != PSP_ME_JOB_GFX_READY)) {
        return;
    }
    PSP_GFX_READY_DONE = 1;
    meLibSync();
    psp_me_dispatch_lock();
    psp_audio_me_wait_locked();
    psp_me_dispatch_unlock();
#endif
}

int PspMe_AcquireGfxFrontend(const void* task,
                             const PspGfxMeTransformTrace** trace,
                             volatile const u32** tracePublished,
                             const PspGfxMeTriangleCode** triangles,
                             volatile const u32** trianglesPublished) {
#if PSP_GFX_ME_REPLAY
    int result = -1;

    if ((task == NULL) || (trace == NULL) || (tracePublished == NULL) ||
        (triangles == NULL) || (trianglesPublished == NULL)) {
        return -1;
    }
    psp_me_dispatch_lock();
    {
        u32 slot;

        for (slot = 0; slot < 2; slot++) {
            if (sGfxReplaySlotPool[slot] != task) {
                continue;
            }
            *trace = NULL;
            *tracePublished = NULL;
            *triangles = sGfxReplayTriangleStorage[slot];
            *trianglesPublished = sGfxReplayTrianglePublished(slot);
            result = 0;
            break;
        }
    }
    psp_me_dispatch_unlock();
    return result;
#else
    (void) task;
    (void) trace;
    (void) tracePublished;
    (void) triangles;
    (void) trianglesPublished;
    return -1;
#endif
}

void PspMe_SubmitGfxReplay(const void* task, const Gfx* dl, u32 taskIndex,
                           const PspGfxMeReplayStats* expected,
                           const PspGfxMeTransformTrace* expectedTrace) {
#if PSP_GFX_ME_REPLAY
    uintptr_t dlOffset;

    if ((task == NULL) || (dl == NULL) || (expected == NULL) ||
        (expectedTrace == NULL)) {
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
        dl, taskIndex, expected, expectedTrace,
        task, task, PSP_GFX_ME_POOL_SIZE, 1, 1);
    psp_me_dispatch_unlock();
#else
    (void) task;
    (void) dl;
    (void) taskIndex;
    (void) expected;
    (void) expectedTrace;
#endif
}

int PspAudioMe_IsActive(void) {
    return sInitialized;
}

int PspAudioMe_GetLastError(void) {
    return sLastError;
}

u32 PspMe_GetGfxVmeStage(void) {
    return sMeGfxVmeStage;
}

u32 PspMe_GetGfxPrivateLitVertices(void) {
    return sGfxReplayPrivateLitVertices;
}

void PspMe_GetGfxReplayCounts(u32* withinFine, u32* withinCoarse,
                              u32* overCoarse, u32* structuralMismatches,
                              u32* maxErrorQ16, u32* skippedBusy) {
#if PSP_GFX_ME_REPLAY
    if (withinFine != NULL) {
        *withinFine = sGfxReplayWithinFine;
    }
    if (withinCoarse != NULL) {
        *withinCoarse = sGfxReplayWithinCoarse;
    }
    if (overCoarse != NULL) {
        *overCoarse = sGfxReplayOverCoarse;
    }
    if (structuralMismatches != NULL) {
        *structuralMismatches = sGfxReplayStructuralMismatches;
    }
    if (maxErrorQ16 != NULL) {
        *maxErrorQ16 = sGfxReplayMaxErrorQ16;
    }
    if (skippedBusy != NULL) {
        *skippedBusy = sGfxReplaySkippedBusy;
    }
#else
    if (withinFine != NULL) {
        *withinFine = 0;
    }
    if (withinCoarse != NULL) {
        *withinCoarse = 0;
    }
    if (overCoarse != NULL) {
        *overCoarse = 0;
    }
    if (structuralMismatches != NULL) {
        *structuralMismatches = 0;
    }
    if (maxErrorQ16 != NULL) {
        *maxErrorQ16 = 0;
    }
    if (skippedBusy != NULL) {
        *skippedBusy = 0;
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

void PspMe_WaitGfxReplayPool(const void* task) {
    (void) task;
}

int PspMe_StartGfxFrontend(const void* task, const Gfx* dl, u32 taskIndex) {
    (void) task;
    (void) dl;
    (void) taskIndex;
    return -1;
}

int PspMe_SubmitGfxReadyPacket(const PspGfxMeReadyPacket* packet) {
    (void) packet;
    return 0;
}

int PspMe_StageGfxReadyVertices(const PspGfxMeVertex* vertices, u32 count,
                                const PspGfxMeVertex** staged) {
    (void) vertices;
    (void) count;
    (void) staged;
    return 0;
}

void PspMe_WaitGfxReadyPackets(void) {
}

void PspMe_FinishGfxReadyPackets(void) {
}

int PspMe_AcquireGfxFrontend(const void* task,
                             const PspGfxMeTransformTrace** trace,
                             volatile const u32** tracePublished,
                             const PspGfxMeTriangleCode** triangles,
                             volatile const u32** trianglesPublished) {
    (void) task;
    (void) trace;
    (void) tracePublished;
    (void) triangles;
    (void) trianglesPublished;
    return -1;
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    (void) commands;
    (void) commandCount;
}

void PspMe_SubmitGfxReplay(const void* task, const Gfx* dl, u32 taskIndex,
                           const PspGfxMeReplayStats* expected,
                           const PspGfxMeTransformTrace* expectedTrace) {
    (void) task;
    (void) dl;
    (void) taskIndex;
    (void) expected;
    (void) expectedTrace;
}

int PspAudioMe_IsActive(void) {
    return 0;
}

int PspAudioMe_GetLastError(void) {
    return 0;
}

u32 PspMe_GetGfxVmeStage(void) {
    return 0;
}

u32 PspMe_GetGfxPrivateLitVertices(void) {
    return 0;
}

void PspMe_GetGfxReplayCounts(u32* withinFine, u32* withinCoarse,
                              u32* overCoarse, u32* structuralMismatches,
                              u32* maxErrorQ16, u32* skippedBusy) {
    if (withinFine != NULL) {
        *withinFine = 0;
    }
    if (withinCoarse != NULL) {
        *withinCoarse = 0;
    }
    if (overCoarse != NULL) {
        *overCoarse = 0;
    }
    if (structuralMismatches != NULL) {
        *structuralMismatches = 0;
    }
    if (maxErrorQ16 != NULL) {
        *maxErrorQ16 = 0;
    }
    if (skippedBusy != NULL) {
        *skippedBusy = 0;
    }
}

#endif
