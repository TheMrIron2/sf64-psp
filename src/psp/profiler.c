#include "src/psp/profiler.h"

#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspdebug.h>
#include <psppower.h>
#include <stdio.h>

#if SF64_PSP_PROFILE_FRAME_TRACE
#include "src/psp/title_trace.h"

extern s32 gGameFrameCount;
extern u32 gSysFrameCount;
extern u8 gVIsPerFrame;
extern int gGameState;
extern s32 gSceneId;
extern int gDrawMode;
#endif

#if SF64_PSP_GPROF
#include <pspprof.h>
#endif

#ifndef SF64_PSP_GPROF
#define SF64_PSP_GPROF 0
#endif
#ifndef SF64_PSP_PROFILE_PHASES
#define SF64_PSP_PROFILE_PHASES 0
#endif
#ifndef SF64_PSP_PROFILE_FRAME_TRACE
#define SF64_PSP_PROFILE_FRAME_TRACE 0
#endif
#ifndef SF64_PSP_PROFILE_FRAME_TRACE_FRAMES
#define SF64_PSP_PROFILE_FRAME_TRACE_FRAMES 240
#endif
#ifndef SF64_PSP_PSPGL_PROFILE
#define SF64_PSP_PSPGL_PROFILE 0
#endif
#ifndef SF64_PSP_PSPGL_VBO_STREAM
#define SF64_PSP_PSPGL_VBO_STREAM 1
#endif
#ifndef SF64_PSP_DIRECT_TRI_FASTPATH
#define SF64_PSP_DIRECT_TRI_FASTPATH 1
#endif
#ifndef SF64_PSP_BATCH_STATE_CACHE
#define SF64_PSP_BATCH_STATE_CACHE 1
#endif
#ifndef SF64_PSP_TEXTURE_WRAP_CACHE
#define SF64_PSP_TEXTURE_WRAP_CACHE 1
#endif
#ifndef SF64_GIT_SHA
#define SF64_GIT_SHA "unknown"
#endif
#ifndef N64PSP_GIT_SHA
#define N64PSP_GIT_SHA "unknown"
#endif
#ifndef PSPGL_GIT_SHA
#define PSPGL_GIT_SHA "unknown"
#endif
#ifndef PSPGL_GIT_DIRTY
#define PSPGL_GIT_DIRTY "unknown"
#endif
#ifndef PSPGL_SOURCE_MODE
#define PSPGL_SOURCE_MODE "system"
#endif
#ifndef PERFECT_DARK_PSP_SHA
#define PERFECT_DARK_PSP_SHA "unknown"
#endif
#ifndef SF64_PSP_COMPILER
#define SF64_PSP_COMPILER "unknown"
#endif
#ifndef SF64_PSP_OPT_FLAGS
#define SF64_PSP_OPT_FLAGS "unknown"
#endif
#ifndef SF64_PSP_PROFILE_CAPTURE_FRAMES
#define SF64_PSP_PROFILE_CAPTURE_FRAMES 300
#endif
#ifndef USE_N64PSP_SINCOS
#define USE_N64PSP_SINCOS 0
#endif

#if SF64_PSP_PROFILE_PHASES && SF64_PSP_PSPGL_PROFILE
#include <pspgl_profile.h>
#endif

#define PSP_PROFILE_DIR "ms0:/PSP/GAME/SF64PROFILE"
#define PSP_PROFILE_MAX_SLOT 999
#define PSP_PROFILE_MAX_THREADS 8
#define PSP_PROFILE_TIMER_SAMPLES 128
#define PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS 512
#define PSP_PROFILE_TEXTURE_CACHE_REUSE_BINS 8

#define PSP_PROFILE_ATTR __attribute__((no_instrument_function, no_profile_instrument_function))

#if SF64_PSP_PROFILE_FRAME_TRACE && !SF64_PSP_PROFILE_PHASES
#error "SF64_PSP_PROFILE_FRAME_TRACE requires SF64_PSP_PROFILE_PHASES"
#endif
#if SF64_PSP_PROFILE_FRAME_TRACE && (SF64_PSP_PROFILE_FRAME_TRACE_FRAMES < 1)
#error "SF64_PSP_PROFILE_FRAME_TRACE_FRAMES must be at least 1"
#endif
#if SF64_PSP_PROFILE_FRAME_TRACE && (SF64_PSP_PROFILE_FRAME_TRACE_FRAMES > 3600)
#error "SF64_PSP_PROFILE_FRAME_TRACE_FRAMES is too large for bounded PSP trace storage"
#endif

typedef enum {
    PSP_PROF_STATUS_OFF,
    PSP_PROF_STATUS_REC,
    PSP_PROF_STATUS_SAVED,
    PSP_PROF_STATUS_ERROR
} PspProfilerStatus;

#if SF64_PSP_PROFILE_PHASES
typedef struct {
    u64 calls;
    u64 totalUs;
    u64 items;
} PspProfilePhaseState;

typedef struct {
    SceUID threadId;
    u64 startUs[PSP_PROFILE_PHASE_COUNT];
    u8 active[PSP_PROFILE_PHASE_COUNT];
} PspProfileThreadState;

typedef struct {
    u64 keyHash;
    u64 baseHash;
    u64 lastSeenSeq;
    u64 evictionSeq;
    u8 used;
    u8 resident;
    u8 evicted;
} PspProfileTextureCacheKey;

typedef struct {
    u64 baseHash;
    u64 variants;
    u8 used;
} PspProfileTextureCacheBaseKey;

typedef struct {
    u64 lookups;
    u64 hits;
    u64 misses;
    u64 insertions;
    u64 evictions;
    u64 uniqueKeys;
    u64 uniqueKeyOverflow;
    u64 uniqueBaseKeys;
    u64 uniqueBaseKeyOverflow;
    u64 maxVariantsPerBaseKey;
    u64 reuseAfterEviction;
    u64 maxReuseDistance;
    u64 reuseDistanceBins[PSP_PROFILE_TEXTURE_CACHE_REUSE_BINS];
    u32 capacity;
    u32 currentEntries;
    u32 peakEntries;
} PspProfileTextureCacheState;

typedef struct {
    u64 displayListTasks;
    u64 opcodeCounts[256];
    u64 gvtxCommands;
    u64 verticesLoaded;
    u64 gvtxHistogram[65];
    u64 modelviewMatrixCommands;
    u64 projectionMatrixCommands;
    u64 matrixCompositions;
    u64 litVertices;
    u64 unlitVertices;
    u64 normalTransforms;
    u64 normalisations;
    u64 lightingEvaluations;
    u64 clipCodeCalculations;
    u64 perspectiveDivides;
    u64 tri1Commands;
    u64 tri2Commands;
    u64 inputTriangles;
    u64 triviallyAcceptedTriangles;
    u64 triviallyRejectedTriangles;
    u64 partiallyClippedTriangles;
    u64 generatedClippingVertices;
    u64 outputTriangles;
    u64 directFastpathTriangles;
    u64 generalPathTriangles;
    u64 perspectivePathTriangles;
    u64 clippedPathTriangles;
    u64 directVerticesWritten;
    u64 effectiveStateResolves;
    u64 effectiveStateReuses;
    u64 materialStateResolves;
    u64 depthStateResolves;
    u64 fogStateResolves;
    u64 batchFlushes;
    u64 flushReasons[PSP_PROFILE_FLUSH_COUNT];
    u64 batchStateTransitions[PSP_PROFILE_BATCH_STATE_COUNT];
    u64 textureFlushSources[PSP_PROFILE_TEXTURE_FLUSH_COUNT];
    u64 verticesSubmitted;
    u64 drawCalls;
    u64 vboDrawCalls;
    u64 vboVertices;
    u64 smallVboDrawCalls;
    u64 largeVboDrawCalls;
    u64 smallVboVertices;
    u64 largeVboVertices;
    u64 vertexStreamUploadCalls;
    u64 vertexStreamUploadBytes;
    u64 clientArrayFallbackDraws;
    u64 clientArrayFallbackVertices;
    u64 vertexStreamPageSwitches;
    u64 vertexStreamCapacityBytes;
    u64 vertexStreamHighWaterBytes;
    u64 glFlushCalls;
    u64 syncCalls;
    u64 textureCacheHits;
    u64 textureCacheMisses;
    u64 textureDecodes;
    u64 textureUploads;
    u64 textureBytesUploaded;
    u64 textureWrapRequestsS;
    u64 textureWrapRequestsT;
    u64 textureWrapCallsEmittedS;
    u64 textureWrapCallsEmittedT;
    u64 textureWrapCallsSkippedS;
    u64 textureWrapCallsSkippedT;
    u64 textureParameterCacheMisses;
    u64 textureParameterCacheReplacements;
    PspProfileTextureCacheState textureCache[PSP_PROFILE_TEXTURE_CACHE_COUNT];
    PspProfileTextureCacheKey textureCacheKeys[PSP_PROFILE_TEXTURE_CACHE_COUNT][PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS];
    PspProfileTextureCacheBaseKey
        textureCacheBaseKeys[PSP_PROFILE_TEXTURE_CACHE_COUNT][PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS];
    u64 textureCacheLookupSeq;
} PspProfileCounters;

#if SF64_PSP_PROFILE_FRAME_TRACE
typedef struct {
    u64 displayListTasks;
    u64 gvtxCommands;
    u64 verticesLoaded;
    u64 modelviewMatrixCommands;
    u64 projectionMatrixCommands;
    u64 matrixCompositions;
    u64 litVertices;
    u64 unlitVertices;
    u64 normalTransforms;
    u64 normalisations;
    u64 lightingEvaluations;
    u64 clipCodeCalculations;
    u64 perspectiveDivides;
    u64 tri1Commands;
    u64 tri2Commands;
    u64 inputTriangles;
    u64 triviallyAcceptedTriangles;
    u64 triviallyRejectedTriangles;
    u64 partiallyClippedTriangles;
    u64 generatedClippingVertices;
    u64 outputTriangles;
    u64 directFastpathTriangles;
    u64 generalPathTriangles;
    u64 perspectivePathTriangles;
    u64 clippedPathTriangles;
    u64 directVerticesWritten;
    u64 effectiveStateResolves;
    u64 effectiveStateReuses;
    u64 materialStateResolves;
    u64 depthStateResolves;
    u64 fogStateResolves;
    u64 batchFlushes;
    u64 flushReasons[PSP_PROFILE_FLUSH_COUNT];
    u64 batchStateTransitions[PSP_PROFILE_BATCH_STATE_COUNT];
    u64 textureFlushSources[PSP_PROFILE_TEXTURE_FLUSH_COUNT];
    u64 verticesSubmitted;
    u64 drawCalls;
    u64 vboDrawCalls;
    u64 vboVertices;
    u64 smallVboDrawCalls;
    u64 largeVboDrawCalls;
    u64 smallVboVertices;
    u64 largeVboVertices;
    u64 vertexStreamUploadCalls;
    u64 vertexStreamUploadBytes;
    u64 clientArrayFallbackDraws;
    u64 clientArrayFallbackVertices;
    u64 vertexStreamPageSwitches;
    u64 vertexStreamCapacityBytes;
    u64 vertexStreamHighWaterBytes;
    u64 glFlushCalls;
    u64 syncCalls;
    u64 textureCacheHits;
    u64 textureCacheMisses;
    u64 textureDecodes;
    u64 textureUploads;
    u64 textureBytesUploaded;
    u64 textureWrapRequestsS;
    u64 textureWrapRequestsT;
    u64 textureWrapCallsEmittedS;
    u64 textureWrapCallsEmittedT;
    u64 textureWrapCallsSkippedS;
    u64 textureWrapCallsSkippedT;
    u64 textureParameterCacheMisses;
    u64 textureParameterCacheReplacements;
} PspProfileFrameCounters;

typedef struct {
    u32 captureFrameIndex;
    u32 gameFrameCounter;
    u32 sysFrameCounter;
    u32 viPerFrame;
    s32 gameState;
    s32 sceneId;
    s32 drawMode;
    u64 frameStartTick;
    u64 frameIntervalUs;
    u64 totalProfiledFrameUsRaw;
    u64 totalProfiledFrameUsAdjusted;
    u64 incompletePhaseMask;
    u32 incompletePhaseDepth;
    PspProfilePhaseState phases[PSP_PROFILE_PHASE_COUNT];
    PspProfileFrameCounters counters;
    PspTitleTraceMarkers title;
} PspProfileFrameRecord;
#endif
#endif

static volatile int sExitRequested;
static PspProfilerStatus sStatus;
static u32 sStatusSlot;
static u32 sNextSlot;
static u32 sPreviousButtons;
static int sCaptureActive;
static int sCaptureStarted;
static int sCaptureDumped;
static int sInitialized;

#if SF64_PSP_GPROF || SF64_PSP_PROFILE_PHASES
static char sCapturePath[96];
#endif

#if SF64_PSP_PROFILE_PHASES
static PspProfilePhaseState sPhase[PSP_PROFILE_PHASE_COUNT];
static PspProfileThreadState sThreadPhase[PSP_PROFILE_MAX_THREADS];
static PspProfileCounters sCounters;
static u32 sCaptureFrames;
static u32 sForcedActivePhaseEnds;
static u64 sCaptureStartUs;
static u64 sCaptureEndUs;
static u64 sTimerReadPairOverheadUs;
#if SF64_PSP_PROFILE_FRAME_TRACE
static PspProfileFrameRecord sFrameTrace[SF64_PSP_PROFILE_FRAME_TRACE_FRAMES];
static PspProfilePhaseState sFramePhase[PSP_PROFILE_PHASE_COUNT];
static PspProfileFrameCounters sFrameCounters;
static u32 sFrameTraceCount;
static u32 sFrameTraceDropped;
static u32 sFrameTraceComplete;
static u64 sFrameStartUs;
static int sFrameStarted;
#endif
#if SF64_PSP_PSPGL_PROFILE
static struct pspgl_profile_stats sPspglProfileStats;
static int sPspglProfileCaptured;
#endif
#endif

#if SF64_PSP_PROFILE_PHASES
PSP_PROFILE_ATTR static u32 psp_profiler_strlen(const char* text) {
    u32 len = 0;

    while ((text != NULL) && (text[len] != '\0')) {
        len++;
    }
    return len;
}

PSP_PROFILE_ATTR static void psp_profiler_zero(void* ptr, u32 size) {
    u8* bytes = (u8*) ptr;
    u32 i;

    for (i = 0; i < size; i++) {
        bytes[i] = 0;
    }
}

PSP_PROFILE_ATTR static u64 psp_profiler_now_us(void) {
    return (u64) sceKernelGetSystemTimeWide();
}

PSP_PROFILE_ATTR static int psp_profiler_lock(void) {
    return sceKernelCpuSuspendIntr();
}

PSP_PROFILE_ATTR static void psp_profiler_unlock(int state) {
    sceKernelCpuResumeIntr(state);
}

PSP_PROFILE_ATTR static u64 psp_profiler_measure_timer_overhead(void) {
    u32 i;
    u64 best = 0;
    u64 a;
    u64 b;
    u64 delta;

    for (i = 0; i < PSP_PROFILE_TIMER_SAMPLES; i++) {
        a = psp_profiler_now_us();
        b = psp_profiler_now_us();
        delta = (b >= a) ? (b - a) : 0;

        if ((i == 0) || (delta < best)) {
            best = delta;
        }
    }
    return best;
}

PSP_PROFILE_ATTR static PspProfileThreadState* psp_profiler_get_thread_state_locked(SceUID threadId) {
    u32 i;
    PspProfileThreadState* empty = NULL;

    for (i = 0; i < PSP_PROFILE_MAX_THREADS; i++) {
        if (sThreadPhase[i].threadId == threadId) {
            return &sThreadPhase[i];
        }
        if ((empty == NULL) && (sThreadPhase[i].threadId < 0)) {
            empty = &sThreadPhase[i];
        }
    }
    if (empty != NULL) {
        empty->threadId = threadId;
    }
    return empty;
}

PSP_PROFILE_ATTR static PspProfileThreadState* psp_profiler_get_thread_state(void) {
    int lockState;
    SceUID threadId;
    PspProfileThreadState* state;

    threadId = sceKernelGetThreadId();
    lockState = psp_profiler_lock();
    state = psp_profiler_get_thread_state_locked(threadId);
    psp_profiler_unlock(lockState);
    return state;
}
#endif

#if SF64_PSP_GPROF || SF64_PSP_PROFILE_PHASES
PSP_PROFILE_ATTR static void psp_profiler_mkdir(void) {
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/GAME", 0777);
    sceIoMkdir(PSP_PROFILE_DIR, 0777);
}

PSP_PROFILE_ATTR static int psp_profiler_exists(const char* path) {
    SceIoStat stat;

    return sceIoGetstat(path, &stat) >= 0;
}
#endif

#if SF64_PSP_GPROF
PSP_PROFILE_ATTR static int psp_profiler_open_unique(const char* prefix, const char* suffix, char* path, u32 pathSize,
                                                     u32* slotOut) {
    u32 slot;

    psp_profiler_mkdir();
    for (slot = sNextSlot; slot <= PSP_PROFILE_MAX_SLOT; slot++) {
        snprintf(path, pathSize, "%s/%s-%03lu.%s", PSP_PROFILE_DIR, prefix, (unsigned long) slot, suffix);
        if (!psp_profiler_exists(path)) {
            *slotOut = slot;
            sNextSlot = slot + 1;
            return 1;
        }
    }
    return 0;
}
#endif

#if SF64_PSP_PROFILE_PHASES
PSP_PROFILE_ATTR static int psp_profiler_write_all(SceUID fd, const char* text) {
    u32 length = psp_profiler_strlen(text);

    return sceIoWrite(fd, text, length) == (int) length;
}
#endif

PSP_PROFILE_ATTR static void psp_profiler_set_status(PspProfilerStatus status, u32 slot) {
    sStatus = status;
    sStatusSlot = slot;
}

#if SF64_PSP_PROFILE_PHASES
PSP_PROFILE_ATTR static void psp_profiler_reset_phase_capture(void) {
    u32 i;

    psp_profiler_zero(sPhase, sizeof(sPhase));
    psp_profiler_zero(sThreadPhase, sizeof(sThreadPhase));
    for (i = 0; i < PSP_PROFILE_MAX_THREADS; i++) {
        sThreadPhase[i].threadId = -1;
    }
    psp_profiler_zero(&sCounters, sizeof(sCounters));
#if SF64_PSP_PROFILE_FRAME_TRACE
    psp_profiler_zero(sFrameTrace, sizeof(sFrameTrace));
    psp_profiler_zero(sFramePhase, sizeof(sFramePhase));
    psp_profiler_zero(&sFrameCounters, sizeof(sFrameCounters));
    sFrameTraceCount = 0;
    sFrameTraceDropped = 0;
    sFrameTraceComplete = 0;
    sFrameStarted = 0;
#endif
#if SF64_PSP_PSPGL_PROFILE
    psp_profiler_zero(&sPspglProfileStats, sizeof(sPspglProfileStats));
    sPspglProfileCaptured = 0;
    pspgl_profile_reset();
#endif
    sCaptureFrames = 0;
    sForcedActivePhaseEnds = 0;
    sTimerReadPairOverheadUs = psp_profiler_measure_timer_overhead();
    sCaptureStartUs = psp_profiler_now_us();
    sCaptureEndUs = sCaptureStartUs;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameStartUs = sCaptureStartUs;
    sFrameStarted = 1;
#endif
}

static const char* psp_profiler_phase_name(PspProfilePhase phase) {
    static const char* names[PSP_PROFILE_PHASE_COUNT] = {
        "graphics task total",
        "display-list traversal and dispatch",
        "G_VTX processing total",
        "triangle processing total",
        "software clipping/subdivision",
        "texture lookup/decode/preparation",
        "texture decode/conversion",
        "texture upload",
        "batch construction",
        "batch flush total",
        "PSPGL state/setup",
        "PSPGL vertex stream upload",
        "PSPGL vertex stream upload small draws",
        "PSPGL vertex stream upload large draws",
        "PSPGL draw submission",
        "PSPGL draw submission small draws",
        "PSPGL draw submission large draws",
        "glFlush queue flush",
        "graphics finish/synchronisation",
        "audio task dispatch",
        "audio synthesis task creation",
        "audio update work",
        "game/update work",
        "graphics task completion/backpressure wait",
        "vblank or idle wait"
    };

    return names[phase];
}

static const char* psp_profiler_flush_name(PspProfileFlushReason reason) {
    static const char* names[PSP_PROFILE_FLUSH_COUNT] = {
        "buffer full",
        "texture change",
        "blend or render-state change",
        "projection or transform-state change",
        "clipping path",
        "end of task",
        "explicit synchronisation",
        "other"
    };

    return names[reason];
}

static const char* psp_profiler_batch_state_name(PspProfileBatchStateField field) {
    static const char* names[PSP_PROFILE_BATCH_STATE_COUNT] = {
        "texture_id",
        "texture_env",
        "wrap_s",
        "wrap_t",
        "alpha_test",
        "blend",
        "premultiplied"
    };

    return names[field];
}

static const char* psp_profiler_texture_flush_source_name(PspProfileTextureFlushSource source) {
    static const char* names[PSP_PROFILE_TEXTURE_FLUSH_COUNT] = {
        "material_key",
        "texture_enable",
        "cache_miss_upload",
        "set_texture_image"
    };

    return names[source];
}

static const char* psp_profiler_texture_cache_name(PspProfileTextureCacheClass cache) {
    static const char* names[PSP_PROFILE_TEXTURE_CACHE_COUNT] = {
        "ci8",
        "rgba16",
        "converted"
    };

    return names[cache];
}

#if SF64_PSP_PROFILE_FRAME_TRACE
static u64 psp_profiler_adjust_phase_us(const PspProfilePhaseState* phase) {
    u64 overheadUs = phase->calls * sTimerReadPairOverheadUs;

    return (phase->totalUs > overheadUs) ? (phase->totalUs - overheadUs) : 0;
}

static void psp_profiler_reset_frame_local(u64 frameStartUs) {
    psp_profiler_zero(sFramePhase, sizeof(sFramePhase));
    psp_profiler_zero(&sFrameCounters, sizeof(sFrameCounters));
    sFrameStartUs = frameStartUs;
    sFrameStarted = 1;
}

static void psp_profiler_frame_trace_sample_title(PspProfileFrameRecord* record) {
    psp_profiler_zero(&record->title, sizeof(record->title));
    Title_PspGetTraceMarkers(&record->title);
}

static void psp_profiler_capture_frame_record_locked(u64 now, u32 frameIndex) {
    PspProfileFrameRecord* record;
    u32 i;
    u32 thread;

    if (!sFrameStarted) {
        psp_profiler_reset_frame_local(now);
        return;
    }
    if (sFrameTraceCount >= SF64_PSP_PROFILE_FRAME_TRACE_FRAMES) {
        sFrameTraceDropped++;
        psp_profiler_reset_frame_local(now);
        return;
    }

    record = &sFrameTrace[sFrameTraceCount];
    psp_profiler_zero(record, sizeof(*record));
    record->captureFrameIndex = frameIndex;
    record->gameFrameCounter = (u32) gGameFrameCount;
    record->sysFrameCounter = gSysFrameCount;
    record->viPerFrame = gVIsPerFrame;
    record->gameState = gGameState;
    record->sceneId = gSceneId;
    record->drawMode = gDrawMode;
    record->frameStartTick = sFrameStartUs;
    record->frameIntervalUs = (now >= sFrameStartUs) ? (now - sFrameStartUs) : 0;
    record->counters = sFrameCounters;
    for (i = 0; i < PSP_PROFILE_PHASE_COUNT; i++) {
        record->phases[i] = sFramePhase[i];
        record->totalProfiledFrameUsRaw += sFramePhase[i].totalUs;
        record->totalProfiledFrameUsAdjusted += psp_profiler_adjust_phase_us(&sFramePhase[i]);
    }
    for (thread = 0; thread < PSP_PROFILE_MAX_THREADS; thread++) {
        if (sThreadPhase[thread].threadId < 0) {
            continue;
        }
        for (i = 0; i < PSP_PROFILE_PHASE_COUNT; i++) {
            if (sThreadPhase[thread].active[i]) {
                record->incompletePhaseMask |= 1ULL << i;
                record->incompletePhaseDepth++;
            }
        }
    }
    psp_profiler_frame_trace_sample_title(record);
    sFrameTraceCount++;
    sFrameTraceComplete = (sFrameTraceCount >= SF64_PSP_PROFILE_FRAME_TRACE_FRAMES);
    psp_profiler_reset_frame_local(now);
}

static u64 psp_profiler_frame_phase_adjusted(const PspProfileFrameRecord* record, PspProfilePhase phase) {
    return psp_profiler_adjust_phase_us(&record->phases[phase]);
}

static void psp_profiler_csv_append(char* line, u32 size, u32* offset, const char* text) {
    int wrote;

    if (*offset >= size) {
        return;
    }
    wrote = snprintf(line + *offset, size - *offset, "%s", text);
    if (wrote > 0) {
        *offset += (u32) wrote;
    }
}

static void psp_profiler_csv_append_u64(char* line, u32 size, u32* offset, u64 value) {
    char text[32];

    snprintf(text, sizeof(text), "%s%llu", (*offset == 0) ? "" : ",", value);
    psp_profiler_csv_append(line, size, offset, text);
}

static void psp_profiler_csv_append_u32(char* line, u32 size, u32* offset, u32 value) {
    char text[32];

    snprintf(text, sizeof(text), "%s%lu", (*offset == 0) ? "" : ",", (unsigned long) value);
    psp_profiler_csv_append(line, size, offset, text);
}

static void psp_profiler_csv_append_s32(char* line, u32 size, u32* offset, s32 value) {
    char text[32];

    snprintf(text, sizeof(text), "%s%ld", (*offset == 0) ? "" : ",", (long) value);
    psp_profiler_csv_append(line, size, offset, text);
}

static void psp_profiler_csv_append_hex64(char* line, u32 size, u32* offset, u64 value) {
    char text[32];

    snprintf(text, sizeof(text), "%s0x%llx", (*offset == 0) ? "" : ",", value);
    psp_profiler_csv_append(line, size, offset, text);
}

static void psp_profiler_csv_append_f32(char* line, u32 size, u32* offset, f32 value) {
    char text[40];

    snprintf(text, sizeof(text), "%s%.6f", (*offset == 0) ? "" : ",", (double) value);
    psp_profiler_csv_append(line, size, offset, text);
}
#endif

static PspProfileTextureCacheKey* psp_profiler_find_texture_cache_key(PspProfileTextureCacheClass cache,
                                                                      u64 keyHash) {
    u32 i;

    for (i = 0; i < PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS; i++) {
        PspProfileTextureCacheKey* key = &sCounters.textureCacheKeys[cache][i];

        if (key->used && (key->keyHash == keyHash)) {
            return key;
        }
    }
    return NULL;
}

static PspProfileTextureCacheKey* psp_profiler_track_texture_cache_key(PspProfileTextureCacheClass cache,
                                                                       u64 keyHash, u64 baseHash) {
    PspProfileTextureCacheState* state = &sCounters.textureCache[cache];
    u32 i;

    for (i = 0; i < PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS; i++) {
        PspProfileTextureCacheKey* key = &sCounters.textureCacheKeys[cache][i];

        if (key->used && (key->keyHash == keyHash)) {
            return key;
        }
    }
    for (i = 0; i < PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS; i++) {
        PspProfileTextureCacheKey* key = &sCounters.textureCacheKeys[cache][i];

        if (!key->used) {
            key->used = 1;
            key->keyHash = keyHash;
            key->baseHash = baseHash;
            state->uniqueKeys++;
            return key;
        }
    }
    state->uniqueKeyOverflow++;
    return NULL;
}

static u64 psp_profiler_popcount64(u64 value) {
    u64 count = 0;

    while (value != 0) {
        count += value & 1ULL;
        value >>= 1;
    }
    return count;
}

static void psp_profiler_track_texture_cache_base_key(PspProfileTextureCacheClass cache, u64 baseHash,
                                                      u64 keyHash) {
    PspProfileTextureCacheState* state = &sCounters.textureCache[cache];
    PspProfileTextureCacheBaseKey* empty = NULL;
    u32 i;

    for (i = 0; i < PSP_PROFILE_TEXTURE_CACHE_TRACKED_KEYS; i++) {
        PspProfileTextureCacheBaseKey* base = &sCounters.textureCacheBaseKeys[cache][i];

        if (!base->used) {
            if (empty == NULL) {
                empty = base;
            }
            continue;
        }
        if (base->baseHash == baseHash) {
            u64 variantMask = 1ULL << (keyHash & 63ULL);

            if ((base->variants & variantMask) == 0) {
                base->variants |= variantMask;
                {
                    u64 variants = psp_profiler_popcount64(base->variants);

                    if (variants > state->maxVariantsPerBaseKey) {
                        state->maxVariantsPerBaseKey = variants;
                    }
                }
            }
            return;
        }
    }
    if (empty != NULL) {
        empty->used = 1;
        empty->baseHash = baseHash;
        empty->variants = 1ULL << (keyHash & 63ULL);
        state->uniqueBaseKeys++;
        if (state->maxVariantsPerBaseKey == 0) {
            state->maxVariantsPerBaseKey = 1;
        }
    } else {
        state->uniqueBaseKeyOverflow++;
    }
}

static void psp_profiler_count_texture_cache_reuse_distance(PspProfileTextureCacheState* state, u64 distance) {
    static const u64 limits[PSP_PROFILE_TEXTURE_CACHE_REUSE_BINS - 1] = {
        1, 4, 8, 16, 32, 64, 128
    };
    u32 bin;

    for (bin = 0; bin < PSP_PROFILE_TEXTURE_CACHE_REUSE_BINS - 1; bin++) {
        if (distance <= limits[bin]) {
            state->reuseDistanceBins[bin]++;
            return;
        }
    }
    state->reuseDistanceBins[PSP_PROFILE_TEXTURE_CACHE_REUSE_BINS - 1]++;
}

static void psp_profiler_write_csv_row(SceUID fd, PspProfilePhase phase) {
    char line[192];
    const PspProfilePhaseState* p = &sPhase[phase];
    u64 overheadUs = p->calls * sTimerReadPairOverheadUs;
    u64 adjustedUs = (p->totalUs > overheadUs) ? (p->totalUs - overheadUs) : 0;
    u64 perFrame = (sCaptureFrames != 0) ? (adjustedUs / sCaptureFrames) : 0;
    u64 perCall = (p->calls != 0) ? (adjustedUs / p->calls) : 0;
    u64 perItem = (p->items != 0) ? (p->totalUs / p->items) : 0;
    u64 captureUs = (sCaptureEndUs > sCaptureStartUs) ? (sCaptureEndUs - sCaptureStartUs) : 0;
    u64 percent100 = (captureUs != 0) ? ((adjustedUs * 10000ULL) / captureUs) : 0;

    snprintf(line, sizeof(line), "%s,inclusive,%llu,%llu,%llu,%llu,%llu,%llu.%02llu,%llu,%llu\n",
             psp_profiler_phase_name(phase), p->calls, p->totalUs, adjustedUs, perFrame, perCall,
             percent100 / 100, percent100 % 100, p->items, perItem);
    psp_profiler_write_all(fd, line);
}

#if SF64_PSP_PSPGL_PROFILE
static void psp_profiler_write_pspgl_profile(SceUID fd) {
    char line[160];
    const struct pspgl_profile_stats* p;

    if (!sPspglProfileCaptured) {
        pspgl_profile_snapshot(&sPspglProfileStats);
        sPspglProfileCaptured = 1;
    }
    p = &sPspglProfileStats;

    psp_profiler_write_all(fd, "\npspgl_profile_begin\n");
    snprintf(line, sizeof(line), "enabled=1\ncaptured=%d\n", sPspglProfileCaptured);
    psp_profiler_write_all(fd, line);
#define PSPGL_PROFILE_WRITE_FIELD(field) do { \
        snprintf(line, sizeof(line), #field "=%llu\n", p->field); \
        psp_profiler_write_all(fd, line); \
    } while (0)
    PSPGL_PROFILE_WRITE_FIELD(texture_bind_calls);
    PSPGL_PROFILE_WRITE_FIELD(texture_bind_changes);
    PSPGL_PROFILE_WRITE_FIELD(texture_bind_redundant);
    PSPGL_PROFILE_WRITE_FIELD(texture_objects_created);
    PSPGL_PROFILE_WRITE_FIELD(texture_cache_flush_requests);
    PSPGL_PROFILE_WRITE_FIELD(texture_cache_flush_commands);
    PSPGL_PROFILE_WRITE_FIELD(texture_cache_sync_requests);
    PSPGL_PROFILE_WRITE_FIELD(texture_cache_sync_commands);
    PSPGL_PROFILE_WRITE_FIELD(texture_image_uploads);
    PSPGL_PROFILE_WRITE_FIELD(texture_compressed_image_uploads);
    PSPGL_PROFILE_WRITE_FIELD(texture_sub_image_uploads);
    PSPGL_PROFILE_WRITE_FIELD(texture_copy_image_uploads);
    PSPGL_PROFILE_WRITE_FIELD(texture_color_table_uploads);
    PSPGL_PROFILE_WRITE_FIELD(texture_mipmap_updates);
    PSPGL_PROFILE_WRITE_FIELD(texture_memory_modifications);
    PSPGL_PROFILE_WRITE_FIELD(texture_upload_bytes);
    PSPGL_PROFILE_WRITE_FIELD(draw_calls);
    PSPGL_PROFILE_WRITE_FIELD(draw_arrays_calls);
    PSPGL_PROFILE_WRITE_FIELD(draw_elements_calls);
    PSPGL_PROFILE_WRITE_FIELD(draw_range_elements_calls);
    PSPGL_PROFILE_WRITE_FIELD(draw_zero_count_skips);
    PSPGL_PROFILE_WRITE_FIELD(vertices_submitted);
    PSPGL_PROFILE_WRITE_FIELD(indices_submitted);
    PSPGL_PROFILE_WRITE_FIELD(array_locked_fast_paths);
    PSPGL_PROFILE_WRITE_FIELD(array_convert_paths);
    PSPGL_PROFILE_WRITE_FIELD(native_vertex_array_copies);
    PSPGL_PROFILE_WRITE_FIELD(converted_vertex_array_copies);
    PSPGL_PROFILE_WRITE_FIELD(native_vertex_array_vertices);
    PSPGL_PROFILE_WRITE_FIELD(converted_vertex_array_vertices);
    PSPGL_PROFILE_WRITE_FIELD(index_buffer_direct_paths);
    PSPGL_PROFILE_WRITE_FIELD(index_convert_paths);
    PSPGL_PROFILE_WRITE_FIELD(vertex_buffer_temp_allocations);
    PSPGL_PROFILE_WRITE_FIELD(index_buffer_temp_allocations);
    PSPGL_PROFILE_WRITE_FIELD(client_memory_draw_paths);
    PSPGL_PROFILE_WRITE_FIELD(buffer_object_draw_paths);
    PSPGL_PROFILE_WRITE_FIELD(render_setup_calls);
    PSPGL_PROFILE_WRITE_FIELD(render_prim_calls);
    PSPGL_PROFILE_WRITE_FIELD(ge_register_groups_considered);
    PSPGL_PROFILE_WRITE_FIELD(ge_register_groups_nonempty);
    PSPGL_PROFILE_WRITE_FIELD(ge_register_bit_iterations);
    PSPGL_PROFILE_WRITE_FIELD(ge_dirty_registers);
    PSPGL_PROFILE_WRITE_FIELD(ge_registers_emitted);
    PSPGL_PROFILE_WRITE_FIELD(ge_dirty_registers_not_emitted);
    PSPGL_PROFILE_WRITE_FIELD(ge_uncached_register_writes);
    PSPGL_PROFILE_WRITE_FIELD(matrix_uploads);
    PSPGL_PROFILE_WRITE_FIELD(matrix_words);
    PSPGL_PROFILE_WRITE_FIELD(buffer_pin_requests);
    PSPGL_PROFILE_WRITE_FIELD(buffer_pin_new);
    PSPGL_PROFILE_WRITE_FIELD(buffer_pin_repeated);
    PSPGL_PROFILE_WRITE_FIELD(buffer_unpins);
    PSPGL_PROFILE_WRITE_FIELD(buffer_dlist_syncs);
    PSPGL_PROFILE_WRITE_FIELD(buffer_dlist_sync_waits);
    PSPGL_PROFILE_WRITE_FIELD(buffer_vidmem_wants);
    PSPGL_PROFILE_WRITE_FIELD(command_words);
    PSPGL_PROFILE_WRITE_FIELD(command_list_submissions);
    PSPGL_PROFILE_WRITE_FIELD(command_list_rollovers);
    PSPGL_PROFILE_WRITE_FIELD(command_list_high_water_words);
    PSPGL_PROFILE_WRITE_FIELD(command_list_capacity_words);
    PSPGL_PROFILE_WRITE_FIELD(command_list_pool_wraps);
    PSPGL_PROFILE_WRITE_FIELD(command_list_outstanding_current);
    PSPGL_PROFILE_WRITE_FIELD(command_list_outstanding_high_water);
    PSPGL_PROFILE_WRITE_FIELD(command_list_reuse_syncs);
    PSPGL_PROFILE_WRITE_FIELD(command_list_reuse_sync_wait_us);
    PSPGL_PROFILE_WRITE_FIELD(command_list_reuse_sync_wait_max_us);
    PSPGL_PROFILE_WRITE_FIELD(command_list_insert_space_calls);
    PSPGL_PROFILE_WRITE_FIELD(command_list_insert_space_words);
    PSPGL_PROFILE_WRITE_FIELD(command_list_insert_space_rollovers);
    PSPGL_PROFILE_WRITE_FIELD(explicit_flushes);
    PSPGL_PROFILE_WRITE_FIELD(finishes);
    PSPGL_PROFILE_WRITE_FIELD(await_completion_calls);
    PSPGL_PROFILE_WRITE_FIELD(queue_waits);
#undef PSPGL_PROFILE_WRITE_FIELD
    psp_profiler_write_all(fd, "pspgl_profile_end\n");
}
#endif

#if SF64_PSP_PROFILE_FRAME_TRACE
static void psp_profiler_write_frame_summary(u32 slot) {
    char path[96];
    char line[4096];
    SceUID fd;
    u32 i;

    snprintf(path, sizeof(path), "%s/profile-%03lu-frames.csv", PSP_PROFILE_DIR, (unsigned long) slot);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
    if (fd < 0) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, slot);
        return;
    }
    psp_profiler_write_all(fd,
                           "capture_frame_index,game_frame_counter,sys_frame_counter,vi_per_frame,game_state,scene_id,draw_mode,frame_start_tick,frame_interval_us,total_profiled_frame_us_raw,total_profiled_frame_us_adjusted,graphics_task_us,game_update_us,display_list_processing_us,batch_flush_us,vertex_processing_or_upload_us,lighting_us,clipping_us,texture_processing_or_upload_us,pspgl_state_setup_us,draw_submission_us,gl_flush_us,finish_or_sync_us,gfx_backpressure_us,vblank_wait_us,incomplete_phase_mask,incomplete_phase_depth,title_valid,title_state,title_cutscene_state,title_scene_state,title_timer1,title_timer2,title_timer3,title_msg_frame_count,title_hold_timer,title_selected_team,title_light_pitch,title_light_yaw,title_light_dir_x,title_light_dir_y,title_light_dir_z,title_light_target_x,title_light_target_y,title_light_target_z,title_ambient_r,title_ambient_g,title_ambient_b,title_camera_eye_x,title_camera_eye_y,title_camera_eye_z,title_camera_at_x,title_camera_at_y,title_camera_at_z,title_flags,display_list_tasks,gvtx_commands,vertices_loaded,tri1_commands,tri2_commands,input_triangles,output_triangles,lit_vertices,lighting_evaluations,perspective_divides,batch_flushes,draw_calls,vertices_submitted,texture_uploads,texture_bytes_uploaded,vertex_stream_upload_calls,vertex_stream_upload_bytes,clipping_input_triangles,trivially_accepted_triangles,trivially_rejected_triangles,partially_clipped_triangles,generated_clipping_vertices,modelview_matrix_commands,projection_matrix_commands,matrix_compositions,glFlush_calls,sync_calls\n");
    for (i = 0; i < sFrameTraceCount; i++) {
        const PspProfileFrameRecord* r = &sFrameTrace[i];
        u64 vertexUs = psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_G_VTX) +
                       psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
        u64 textureUs = psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_TEXTURE_PREPARE) +
                        psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_TEXTURE_DECODE) +
                        psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
        u64 syncUs = psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_FINISH_SYNC);

        u32 offset = 0;

        psp_profiler_csv_append_u32(line, sizeof(line), &offset, r->captureFrameIndex);
        psp_profiler_csv_append_u32(line, sizeof(line), &offset, r->gameFrameCounter);
        psp_profiler_csv_append_u32(line, sizeof(line), &offset, r->sysFrameCounter);
        psp_profiler_csv_append_u32(line, sizeof(line), &offset, r->viPerFrame);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->gameState);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->sceneId);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->drawMode);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->frameStartTick);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->frameIntervalUs);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->totalProfiledFrameUsRaw);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->totalProfiledFrameUsAdjusted);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_GFX_TASK));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_GAME_UPDATE));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_DL_TRAVERSAL));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_BATCH_FLUSH));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, vertexUs);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_G_VTX));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_CLIPPING));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, textureUs);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_PSPGL_STATE_SETUP));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_PSPGL_SUBMIT));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_GL_FLUSH));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, syncUs);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_GFX_TASK_BACKPRESSURE));
        psp_profiler_csv_append_u64(line, sizeof(line), &offset,
                                    psp_profiler_frame_phase_adjusted(r, PSP_PROFILE_PHASE_VBLANK_WAIT));
        psp_profiler_csv_append_hex64(line, sizeof(line), &offset, r->incompletePhaseMask);
        psp_profiler_csv_append_u32(line, sizeof(line), &offset, r->incompletePhaseDepth);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.valid);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.title_state);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.cutscene_state);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.scene_state);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.timer1);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.timer2);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.timer3);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.title_msg_frame_count);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.title_hold_timer);
        psp_profiler_csv_append_s32(line, sizeof(line), &offset, r->title.selected_team);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_pitch);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_yaw);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_dir_x);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_dir_y);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_dir_z);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_target_x);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_target_y);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.light_target_z);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.ambient_r);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.ambient_g);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.ambient_b);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.camera_eye_x);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.camera_eye_y);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.camera_eye_z);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.camera_at_x);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.camera_at_y);
        psp_profiler_csv_append_f32(line, sizeof(line), &offset, r->title.camera_at_z);
        psp_profiler_csv_append_u32(line, sizeof(line), &offset, r->title.flags);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.displayListTasks);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.gvtxCommands);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.verticesLoaded);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.tri1Commands);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.tri2Commands);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.inputTriangles);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.outputTriangles);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.litVertices);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.lightingEvaluations);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.perspectiveDivides);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.batchFlushes);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.drawCalls);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.verticesSubmitted);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.textureUploads);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.textureBytesUploaded);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.vertexStreamUploadCalls);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.vertexStreamUploadBytes);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.inputTriangles);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.triviallyAcceptedTriangles);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.triviallyRejectedTriangles);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.partiallyClippedTriangles);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.generatedClippingVertices);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.modelviewMatrixCommands);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.projectionMatrixCommands);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.matrixCompositions);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.glFlushCalls);
        psp_profiler_csv_append_u64(line, sizeof(line), &offset, r->counters.syncCalls);
        psp_profiler_csv_append(line, sizeof(line), &offset, "\n");
        psp_profiler_write_all(fd, line);
    }
    sceIoClose(fd);
}

static void psp_profiler_write_frame_phases(u32 slot) {
    char path[96];
    char line[256];
    SceUID fd;
    u32 frame;
    u32 phase;

    snprintf(path, sizeof(path), "%s/profile-%03lu-phases.csv", PSP_PROFILE_DIR, (unsigned long) slot);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
    if (fd < 0) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, slot);
        return;
    }
    psp_profiler_write_all(fd, "frame,phase,calls,total_us_raw,total_us_adjusted,items\n");
    for (frame = 0; frame < sFrameTraceCount; frame++) {
        for (phase = 0; phase < PSP_PROFILE_PHASE_COUNT; phase++) {
            const PspProfilePhaseState* p = &sFrameTrace[frame].phases[phase];

            snprintf(line, sizeof(line), "%lu,%s,%llu,%llu,%llu,%llu\n", (unsigned long) frame,
                     psp_profiler_phase_name((PspProfilePhase) phase), p->calls, p->totalUs,
                     psp_profiler_adjust_phase_us(p), p->items);
            psp_profiler_write_all(fd, line);
        }
    }
    sceIoClose(fd);
}

static void psp_profiler_write_frame_categories(u32 slot) {
    char path[96];
    char line[192];
    SceUID fd;
    u32 frame;
    u32 i;

    snprintf(path, sizeof(path), "%s/profile-%03lu-frame-categories.csv", PSP_PROFILE_DIR, (unsigned long) slot);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
    if (fd < 0) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, slot);
        return;
    }
    psp_profiler_write_all(fd, "frame,category,name,count\n");
    for (frame = 0; frame < sFrameTraceCount; frame++) {
        const PspProfileFrameCounters* c = &sFrameTrace[frame].counters;

        for (i = 0; i < PSP_PROFILE_FLUSH_COUNT; i++) {
            snprintf(line, sizeof(line), "%lu,flush_reason,%s,%llu\n", (unsigned long) frame,
                     psp_profiler_flush_name((PspProfileFlushReason) i), c->flushReasons[i]);
            psp_profiler_write_all(fd, line);
        }
        for (i = 0; i < PSP_PROFILE_BATCH_STATE_COUNT; i++) {
            snprintf(line, sizeof(line), "%lu,batch_state_transition,%s,%llu\n", (unsigned long) frame,
                     psp_profiler_batch_state_name((PspProfileBatchStateField) i),
                     c->batchStateTransitions[i]);
            psp_profiler_write_all(fd, line);
        }
        for (i = 0; i < PSP_PROFILE_TEXTURE_FLUSH_COUNT; i++) {
            snprintf(line, sizeof(line), "%lu,texture_flush_source,%s,%llu\n", (unsigned long) frame,
                     psp_profiler_texture_flush_source_name((PspProfileTextureFlushSource) i),
                     c->textureFlushSources[i]);
            psp_profiler_write_all(fd, line);
        }
    }
    sceIoClose(fd);
}

static void psp_profiler_write_frame_trace_files(u32 slot) {
    psp_profiler_write_frame_summary(slot);
    psp_profiler_write_frame_phases(slot);
    psp_profiler_write_frame_categories(slot);
}
#endif

static void psp_profiler_write_phase_files(u32 slot) {
    char path[96];
    char line[1024];
    SceUID fd;
    u32 i;

    snprintf(path, sizeof(path), "%s/profile-%03lu.csv", PSP_PROFILE_DIR, (unsigned long) slot);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
    if (fd < 0) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, slot);
        return;
    }
    psp_profiler_write_all(fd,
                           "phase,inclusive_or_exclusive,calls,total_us_raw,total_us_adjusted,us_per_frame_adjusted,us_per_call_adjusted,percent_of_capture_adjusted,items,us_per_item_raw\n");
    for (i = 0; i < PSP_PROFILE_PHASE_COUNT; i++) {
        psp_profiler_write_csv_row(fd, (PspProfilePhase) i);
    }
    sceIoClose(fd);

    snprintf(path, sizeof(path), "%s/profile-%03lu.txt", PSP_PROFILE_DIR, (unsigned long) slot);
    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
    if (fd < 0) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, slot);
        return;
    }

    snprintf(line, sizeof(line),
             "SF64 git SHA: %s\nn64psp submodule SHA: %s\nPSPGL source mode: %s\nPSPGL git SHA: %s\nPSPGL worktree: %s\nPerfect Dark reference SHA: %s\ncompiler: %s\noptimisation flags: %s\nPROFILE_PSP: %d\nSF64_PSP_PROFILE_PHASES: %d\nSF64_PSP_PSPGL_PROFILE: %d\nSF64_PSP_PSPGL_DLIST_SIZE_WORDS: %d\nSF64_PSP_PSPGL_VBO_STREAM: %d\nSF64_PSP_DIRECT_TRI_FASTPATH: %d\nSF64_PSP_BATCH_STATE_CACHE: %d\nSF64_PSP_TEXTURE_WRAP_CACHE: %d\nUSE_N64PSP_SINCOS: %d\nCPU clock: %lu\nbus clock: %lu\ncapture slot: %lu\nrequested frame count: %d\nactual frame count: %lu\ntimer overhead us: %llu\n\n",
             SF64_GIT_SHA, N64PSP_GIT_SHA, PSPGL_SOURCE_MODE, PSPGL_GIT_SHA, PSPGL_GIT_DIRTY,
             PERFECT_DARK_PSP_SHA, SF64_PSP_COMPILER, SF64_PSP_OPT_FLAGS,
             SF64_PSP_GPROF, SF64_PSP_PROFILE_PHASES, SF64_PSP_PSPGL_PROFILE, SF64_PSP_PSPGL_DLIST_SIZE_WORDS,
             SF64_PSP_PSPGL_VBO_STREAM, SF64_PSP_DIRECT_TRI_FASTPATH,
             SF64_PSP_BATCH_STATE_CACHE, SF64_PSP_TEXTURE_WRAP_CACHE, USE_N64PSP_SINCOS,
             (unsigned long) scePowerGetCpuClockFrequency(),
             (unsigned long) scePowerGetBusClockFrequency(), (unsigned long) slot, SF64_PSP_PROFILE_CAPTURE_FRAMES,
             (unsigned long) sCaptureFrames, sTimerReadPairOverheadUs);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "SF64_PSP_PROFILE_FRAME_TRACE: %d\nSF64_PSP_PROFILE_FRAME_TRACE_FRAMES: %d\nframe trace recorded frames: %lu\nframe trace complete: %lu\nframe trace dropped frames: %lu\nframe trace static bytes: %lu\n\n",
             SF64_PSP_PROFILE_FRAME_TRACE, SF64_PSP_PROFILE_FRAME_TRACE_FRAMES,
#if SF64_PSP_PROFILE_FRAME_TRACE
             (unsigned long) sFrameTraceCount, (unsigned long) sFrameTraceComplete,
             (unsigned long) sFrameTraceDropped, (unsigned long) sizeof(sFrameTrace)
#else
             0UL, 0UL, 0UL, 0UL
#endif
    );
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "timer overhead samples: %d\nphase totals: inclusive raw and timer-adjusted; nested time is not subtracted\nforced active phase ends on stop: %lu\n\n",
             PSP_PROFILE_TIMER_SAMPLES, (unsigned long) sForcedActivePhaseEnds);
    psp_profiler_write_all(fd, line);

    psp_profiler_write_all(fd, "[opcode counts]\nopcode,count\n");
    for (i = 0; i < 256; i++) {
        if (sCounters.opcodeCounts[i] != 0) {
            snprintf(line, sizeof(line), "0x%02lx,%llu\n", (unsigned long) i, sCounters.opcodeCounts[i]);
            psp_profiler_write_all(fd, line);
        }
    }
    psp_profiler_write_all(fd, "\n[G_VTX batch histogram]\ncount,commands\n");
    for (i = 0; i < 65; i++) {
        if (sCounters.gvtxHistogram[i] != 0) {
            snprintf(line, sizeof(line), "%lu,%llu\n", (unsigned long) i, sCounters.gvtxHistogram[i]);
            psp_profiler_write_all(fd, line);
        }
    }
    psp_profiler_write_all(fd, "\n[flush reasons]\nreason,count\n");
    for (i = 0; i < PSP_PROFILE_FLUSH_COUNT; i++) {
        snprintf(line, sizeof(line), "%s,%llu\n", psp_profiler_flush_name((PspProfileFlushReason) i),
                 sCounters.flushReasons[i]);
        psp_profiler_write_all(fd, line);
    }
    psp_profiler_write_all(fd, "\n[batch state transitions]\nfield,count\n");
    for (i = 0; i < PSP_PROFILE_BATCH_STATE_COUNT; i++) {
        snprintf(line, sizeof(line), "%s,%llu\n", psp_profiler_batch_state_name((PspProfileBatchStateField) i),
                 sCounters.batchStateTransitions[i]);
        psp_profiler_write_all(fd, line);
    }
    psp_profiler_write_all(fd, "\n[texture flush sources]\nsource,count\n");
    for (i = 0; i < PSP_PROFILE_TEXTURE_FLUSH_COUNT; i++) {
        snprintf(line, sizeof(line), "%s,%llu\n",
                 psp_profiler_texture_flush_source_name((PspProfileTextureFlushSource) i),
                 sCounters.textureFlushSources[i]);
        psp_profiler_write_all(fd, line);
    }
    snprintf(line, sizeof(line),
             "\n[texture statistics]\ncache_hits,%llu\ncache_misses,%llu\ndecodes_or_conversions,%llu\nuploads,%llu\nbytes_uploaded,%llu\n",
             sCounters.textureCacheHits, sCounters.textureCacheMisses, sCounters.textureDecodes,
             sCounters.textureUploads, sCounters.textureBytesUploaded);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "\n[texture parameter cache]\nwrap_requests_s,%llu\nwrap_requests_t,%llu\nwrap_calls_emitted_s,%llu\nwrap_calls_emitted_t,%llu\nwrap_calls_skipped_s,%llu\nwrap_calls_skipped_t,%llu\ncache_misses,%llu\nreplacements_or_collisions,%llu\n",
             sCounters.textureWrapRequestsS, sCounters.textureWrapRequestsT, sCounters.textureWrapCallsEmittedS,
             sCounters.textureWrapCallsEmittedT, sCounters.textureWrapCallsSkippedS,
             sCounters.textureWrapCallsSkippedT, sCounters.textureParameterCacheMisses,
             sCounters.textureParameterCacheReplacements);
    psp_profiler_write_all(fd, line);
    psp_profiler_write_all(fd,
                           "\n[texture cache behaviour]\ncache,capacity,current_entries,peak_entries,unique_keys,unique_base_keys,max_variants_per_base,lookups,hits,misses,insertions,evictions,reuse_after_eviction,max_reuse_distance,unique_key_overflow,unique_base_overflow\n");
    for (i = 0; i < PSP_PROFILE_TEXTURE_CACHE_COUNT; i++) {
        const PspProfileTextureCacheState* cache = &sCounters.textureCache[i];

        snprintf(line, sizeof(line),
                 "%s,%lu,%lu,%lu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                 psp_profiler_texture_cache_name((PspProfileTextureCacheClass) i),
                 (unsigned long) cache->capacity, (unsigned long) cache->currentEntries,
                 (unsigned long) cache->peakEntries, cache->uniqueKeys, cache->uniqueBaseKeys,
                 cache->maxVariantsPerBaseKey, cache->lookups, cache->hits, cache->misses, cache->insertions,
                 cache->evictions, cache->reuseAfterEviction, cache->maxReuseDistance,
                 cache->uniqueKeyOverflow, cache->uniqueBaseKeyOverflow);
        psp_profiler_write_all(fd, line);
    }
    psp_profiler_write_all(fd, "\n[texture cache eviction reuse distance]\ncache,1,2-4,5-8,9-16,17-32,33-64,65-128,129+\n");
    for (i = 0; i < PSP_PROFILE_TEXTURE_CACHE_COUNT; i++) {
        const PspProfileTextureCacheState* cache = &sCounters.textureCache[i];

        snprintf(line, sizeof(line), "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                 psp_profiler_texture_cache_name((PspProfileTextureCacheClass) i),
                 cache->reuseDistanceBins[0], cache->reuseDistanceBins[1], cache->reuseDistanceBins[2],
                 cache->reuseDistanceBins[3], cache->reuseDistanceBins[4], cache->reuseDistanceBins[5],
                 cache->reuseDistanceBins[6], cache->reuseDistanceBins[7]);
        psp_profiler_write_all(fd, line);
    }
    snprintf(line, sizeof(line),
             "\n[PSPGL vertex stream]\nenabled,%d\nvbo_draw_calls,%llu\nvbo_vertices,%llu\nsmall_vbo_draws,%llu\nlarge_vbo_draws,%llu\nsmall_vbo_vertices,%llu\nlarge_vbo_vertices,%llu\nupload_calls,%llu\nupload_bytes,%llu\nclient_array_fallback_draws,%llu\nclient_array_fallback_vertices,%llu\npage_switches,%llu\ncapacity_bytes,%llu\nhigh_water_bytes,%llu\n",
             SF64_PSP_PSPGL_VBO_STREAM, sCounters.vboDrawCalls, sCounters.vboVertices,
             sCounters.smallVboDrawCalls, sCounters.largeVboDrawCalls, sCounters.smallVboVertices,
             sCounters.largeVboVertices, sCounters.vertexStreamUploadCalls, sCounters.vertexStreamUploadBytes,
             sCounters.clientArrayFallbackDraws, sCounters.clientArrayFallbackVertices,
             sCounters.vertexStreamPageSwitches, sCounters.vertexStreamCapacityBytes,
             sCounters.vertexStreamHighWaterBytes);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "\n[clipping statistics]\ninput_triangles,%llu\ntrivially_accepted,%llu\ntrivially_rejected,%llu\npartially_clipped,%llu\ngenerated_vertices,%llu\noutput_triangles,%llu\n",
             sCounters.inputTriangles, sCounters.triviallyAcceptedTriangles, sCounters.triviallyRejectedTriangles,
             sCounters.partiallyClippedTriangles, sCounters.generatedClippingVertices, sCounters.outputTriangles);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "\n[triangle path statistics]\ndirect_fastpath_triangles,%llu\ngeneral_path_triangles,%llu\nperspective_path_triangles,%llu\nclipped_path_triangles,%llu\ndirect_vertices_written,%llu\n",
             sCounters.directFastpathTriangles, sCounters.generalPathTriangles,
             sCounters.perspectivePathTriangles, sCounters.clippedPathTriangles, sCounters.directVerticesWritten);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "\n[effective state cache]\neffective_state_resolves,%llu\neffective_state_reuses,%llu\nmaterial_state_resolves,%llu\ndepth_state_resolves,%llu\nfog_state_resolves,%llu\n",
             sCounters.effectiveStateResolves, sCounters.effectiveStateReuses,
             sCounters.materialStateResolves, sCounters.depthStateResolves, sCounters.fogStateResolves);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "\n[TnL statistics]\ndisplay_list_tasks,%llu\ngvtx_commands,%llu\nvertices_loaded,%llu\nmodelview_matrix_commands,%llu\nprojection_matrix_commands,%llu\nmatrix_compositions,%llu\nlit_vertices,%llu\nunlit_vertices,%llu\nnormal_transforms,%llu\nnormalisations,%llu\nlighting_evaluations,%llu\nclip_code_calculations,%llu\nperspective_divides,%llu\ntri1_commands,%llu\ntri2_commands,%llu\nbatch_flushes,%llu\nvertices_submitted,%llu\ndraw_calls,%llu\nglFlush_calls,%llu\nsync_calls,%llu\n",
             sCounters.displayListTasks, sCounters.gvtxCommands, sCounters.verticesLoaded, sCounters.modelviewMatrixCommands,
             sCounters.projectionMatrixCommands, sCounters.matrixCompositions, sCounters.litVertices,
             sCounters.unlitVertices, sCounters.normalTransforms, sCounters.normalisations,
             sCounters.lightingEvaluations, sCounters.clipCodeCalculations, sCounters.perspectiveDivides,
             sCounters.tri1Commands, sCounters.tri2Commands, sCounters.batchFlushes, sCounters.verticesSubmitted,
             sCounters.drawCalls, sCounters.glFlushCalls, sCounters.syncCalls);
    psp_profiler_write_all(fd, line);
#if SF64_PSP_PSPGL_PROFILE
    psp_profiler_write_pspgl_profile(fd);
#endif
    sceIoClose(fd);
#if SF64_PSP_PROFILE_FRAME_TRACE
    psp_profiler_write_frame_trace_files(slot);
#endif
    psp_profiler_set_status(PSP_PROF_STATUS_SAVED, slot);
}
#endif

void PspProfiler_Init(void) {
    u32 slot = 0;

    if (sInitialized) {
        return;
    }
    sInitialized = 1;
    sStatus = PSP_PROF_STATUS_OFF;
    sStatusSlot = 0;
    sNextSlot = 0;
#if SF64_PSP_GPROF
    sCapturePath[0] = '\0';
    if (psp_profiler_open_unique("gmon", "out", sCapturePath, sizeof(sCapturePath), &slot)) {
        sCaptureStarted = 1;
        sCaptureActive = 1;
        sCaptureDumped = 0;
        sStatusSlot = slot;
    } else {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, 0);
    }
#else
    (void) slot;
#endif
}

int PspProfiler_PollControls(u32 rawButtons) {
    u32 startCombo = PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER;
    u32 stopCombo = PSP_CTRL_SELECT | PSP_CTRL_RTRIGGER;
    int startPressed = ((rawButtons & startCombo) == startCombo) && ((sPreviousButtons & startCombo) != startCombo);
    int stopPressed = ((rawButtons & stopCombo) == stopCombo) && ((sPreviousButtons & stopCombo) != stopCombo);

    sPreviousButtons = rawButtons;
    if (startPressed) {
        PspProfiler_StartCapture();
        return 1;
    }
    if (stopPressed) {
        PspProfiler_StopCapture();
        PspProfiler_DumpCapture();
        return 1;
    }
    return 0;
}

void PspProfiler_StartCapture(void) {
    u32 slot = sNextSlot;

#if SF64_PSP_GPROF
    if (sCaptureDumped) {
        psp_profiler_set_status(PSP_PROF_STATUS_SAVED, sStatusSlot);
        return;
    }
    if (sCapturePath[0] == '\0') {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, sStatusSlot);
        return;
    }
    sCaptureStarted = 1;
    sCaptureActive = 1;
    psp_profiler_set_status(PSP_PROF_STATUS_REC, sStatusSlot);
#endif
#if SF64_PSP_PROFILE_PHASES && !SF64_PSP_GPROF
    if (sCaptureActive) {
        PspProfiler_StopCapture();
        PspProfiler_DumpCapture();
    }
    sCaptureStarted = 0;
    sCaptureDumped = 0;
#if !SF64_PSP_GPROF
    psp_profiler_mkdir();
    while (slot <= PSP_PROFILE_MAX_SLOT) {
        snprintf(sCapturePath, sizeof(sCapturePath), "%s/profile-%03lu.txt", PSP_PROFILE_DIR, (unsigned long) slot);
        if (!psp_profiler_exists(sCapturePath)) {
            break;
        }
        slot++;
    }
    if (slot > PSP_PROFILE_MAX_SLOT) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, sStatusSlot);
        return;
    }
    sNextSlot = slot + 1;
#endif
    psp_profiler_reset_phase_capture();
    sCaptureStarted = 1;
    sCaptureActive = 1;
    psp_profiler_set_status(PSP_PROF_STATUS_REC, slot);
#else
    (void) slot;
#endif
}

void PspProfiler_StopCapture(void) {
#if SF64_PSP_PROFILE_PHASES
    u64 now;
    int lockState;
    u32 thread;
    u32 phase;
#endif

    if (!sCaptureActive) {
        return;
    }
#if SF64_PSP_PROFILE_PHASES
    now = psp_profiler_now_us();
    lockState = psp_profiler_lock();
    for (thread = 0; thread < PSP_PROFILE_MAX_THREADS; thread++) {
        if (sThreadPhase[thread].threadId < 0) {
            continue;
        }
        for (phase = 0; phase < PSP_PROFILE_PHASE_COUNT; phase++) {
            if (sThreadPhase[thread].active[phase]) {
                if (now >= sThreadPhase[thread].startUs[phase]) {
                    sPhase[phase].totalUs += now - sThreadPhase[thread].startUs[phase];
                }
                sPhase[phase].calls++;
                sThreadPhase[thread].active[phase] = 0;
                sForcedActivePhaseEnds++;
            }
        }
    }
    sCaptureEndUs = now;
    psp_profiler_unlock(lockState);
#if SF64_PSP_PSPGL_PROFILE
    pspgl_profile_snapshot(&sPspglProfileStats);
    sPspglProfileCaptured = 1;
#endif
#endif
    sCaptureActive = 0;
}

void PspProfiler_DumpCapture(void) {
    if (!sCaptureStarted || sCaptureDumped) {
        return;
    }
    sCaptureDumped = 1;
#if SF64_PSP_GPROF
    if (sCapturePath[0] == '\0') {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, sStatusSlot);
        return;
    }
    gprof_stop(sCapturePath, 1);
    psp_profiler_set_status(PSP_PROF_STATUS_SAVED, sStatusSlot);
    sCaptureActive = 0;
#endif
#if SF64_PSP_PROFILE_PHASES
    psp_profiler_mkdir();
    psp_profiler_write_phase_files(sStatusSlot);
#endif
}

int PspProfiler_IsCapturing(void) {
    return sCaptureActive;
}

void PspProfiler_Shutdown(void) {
    PspProfiler_StopCapture();
    PspProfiler_DumpCapture();
}

void PspProfiler_DrawStatus(void) {
#if SF64_PSP_GPROF || SF64_PSP_PROFILE_PHASES
    const char* label = "PROF OFF";

    if (sStatus == PSP_PROF_STATUS_REC) {
        label = "PROF REC";
    } else if (sStatus == PSP_PROF_STATUS_SAVED) {
        label = "PROF SAVED";
    } else if (sStatus == PSP_PROF_STATUS_ERROR) {
        label = "PROF ERROR";
    }

    pspDebugScreenSetXY(0, 1);
    if (sStatus == PSP_PROF_STATUS_OFF || sStatus == PSP_PROF_STATUS_ERROR) {
        pspDebugScreenPrintf("%s       ", label);
    } else {
        pspDebugScreenPrintf("%s %03lu   ", label, (unsigned long) sStatusSlot);
    }
#endif
}

void PspProfiler_RequestExit(void) {
    sExitRequested = 1;
}

int PspProfiler_ExitRequested(void) {
    return sExitRequested;
}

#if SF64_PSP_PROFILE_PHASES
/*
 * Renderer counters are single-writer data owned by the PSP graphics task
 * thread. Phase-time aggregation remains locked because phases can be opened
 * by audio, main, and graphics threads.
 */
void PspProfiler_PhaseBegin(PspProfilePhase phase) {
    PspProfileThreadState* state;

    if (!sCaptureActive || (phase >= PSP_PROFILE_PHASE_COUNT)) {
        return;
    }
    state = psp_profiler_get_thread_state();
    if ((state == NULL) || state->active[phase]) {
        return;
    }
    state->startUs[phase] = psp_profiler_now_us();
    state->active[phase] = 1;
}

void PspProfiler_PhaseEnd(PspProfilePhase phase) {
    u64 now;
    u64 delta;
    int lockState;
    PspProfilePhaseState* p;
    PspProfileThreadState* state;

    if (!sCaptureActive || (phase >= PSP_PROFILE_PHASE_COUNT)) {
        return;
    }
    state = psp_profiler_get_thread_state();
    if ((state == NULL) || !state->active[phase]) {
        return;
    }
    now = psp_profiler_now_us();
    delta = (now >= state->startUs[phase]) ? (now - state->startUs[phase]) : 0;
    state->active[phase] = 0;

    lockState = psp_profiler_lock();
    p = &sPhase[phase];
    p->totalUs += delta;
    p->calls++;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFramePhase[phase].totalUs += delta;
    sFramePhase[phase].calls++;
#endif
    psp_profiler_unlock(lockState);
}

void PspProfiler_OnGfxTaskComplete(void) {
    int lockState;
    u32 frames;
#if SF64_PSP_PROFILE_FRAME_TRACE
    u64 now;
#endif

    if (!sCaptureActive) {
        return;
    }
#if SF64_PSP_PROFILE_FRAME_TRACE
    now = psp_profiler_now_us();
#endif
    lockState = psp_profiler_lock();
    sCaptureFrames++;
    frames = sCaptureFrames;
#if SF64_PSP_PROFILE_FRAME_TRACE
    psp_profiler_capture_frame_record_locked(now, frames - 1);
#endif
    psp_profiler_unlock(lockState);
    if (frames >= SF64_PSP_PROFILE_CAPTURE_FRAMES) {
        PspProfiler_StopCapture();
        PspProfiler_DumpCapture();
    }
}

void PspProfiler_CountDisplayListTask(void) {
    if (sCaptureActive) {
        sCounters.displayListTasks++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.displayListTasks++;
#endif
    }
}

void PspProfiler_CountOpcode(u8 opcode) {
    if (sCaptureActive) {
        sCounters.opcodeCounts[opcode]++;
    }
}

void PspProfiler_CountGvtx(u32 count, u32 lit) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.gvtxCommands++;
    sCounters.verticesLoaded += count;
    sCounters.gvtxHistogram[(count <= 64) ? count : 64]++;
    if (lit) {
        sCounters.litVertices += count;
    } else {
        sCounters.unlitVertices += count;
    }
    sPhase[PSP_PROFILE_PHASE_G_VTX].items += count;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.gvtxCommands++;
    sFrameCounters.verticesLoaded += count;
    if (lit) {
        sFrameCounters.litVertices += count;
    } else {
        sFrameCounters.unlitVertices += count;
    }
    sFramePhase[PSP_PROFILE_PHASE_G_VTX].items += count;
#endif
}

void PspProfiler_CountMatrixCommand(u32 projection, u32 composed) {
    if (!sCaptureActive) {
        return;
    }
    if (projection) {
        sCounters.projectionMatrixCommands++;
    } else {
        sCounters.modelviewMatrixCommands++;
    }
    if (composed) {
        sCounters.matrixCompositions++;
    }
#if SF64_PSP_PROFILE_FRAME_TRACE
    if (projection) {
        sFrameCounters.projectionMatrixCommands++;
    } else {
        sFrameCounters.modelviewMatrixCommands++;
    }
    if (composed) {
        sFrameCounters.matrixCompositions++;
    }
#endif
}

void PspProfiler_CountTriangleCommand(u32 triCount, u32 tri1, u32 tri2) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.inputTriangles += triCount;
    sCounters.tri1Commands += tri1;
    sCounters.tri2Commands += tri2;
    sPhase[PSP_PROFILE_PHASE_TRIANGLE].items += triCount;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.inputTriangles += triCount;
    sFrameCounters.tri1Commands += tri1;
    sFrameCounters.tri2Commands += tri2;
    sFramePhase[PSP_PROFILE_PHASE_TRIANGLE].items += triCount;
#endif
}

void PspProfiler_CountTriangleResult(u32 accepted, u32 rejected, u32 clipped, u32 generatedVertices,
                                     u32 outputTriangles) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.triviallyAcceptedTriangles += accepted;
    sCounters.triviallyRejectedTriangles += rejected;
    sCounters.partiallyClippedTriangles += clipped;
    sCounters.generatedClippingVertices += generatedVertices;
    sCounters.outputTriangles += outputTriangles;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.triviallyAcceptedTriangles += accepted;
    sFrameCounters.triviallyRejectedTriangles += rejected;
    sFrameCounters.partiallyClippedTriangles += clipped;
    sFrameCounters.generatedClippingVertices += generatedVertices;
    sFrameCounters.outputTriangles += outputTriangles;
#endif
}

void PspProfiler_CountTransformWork(u32 vertices, u32 normals, u32 normalizes, u32 lighting, u32 clipCodes,
                                    u32 divides) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.normalTransforms += normals;
    sCounters.normalisations += normalizes;
    sCounters.lightingEvaluations += lighting;
    sCounters.clipCodeCalculations += clipCodes;
    sCounters.perspectiveDivides += divides;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.normalTransforms += normals;
    sFrameCounters.normalisations += normalizes;
    sFrameCounters.lightingEvaluations += lighting;
    sFrameCounters.clipCodeCalculations += clipCodes;
    sFrameCounters.perspectiveDivides += divides;
#endif
    (void) vertices;
}

void PspProfiler_CountTextureEvent(u32 hit, u32 miss, u32 decode, u32 upload, u32 bytesUploaded) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.textureCacheHits += hit;
    sCounters.textureCacheMisses += miss;
    sCounters.textureDecodes += decode;
    sCounters.textureUploads += upload;
    sCounters.textureBytesUploaded += bytesUploaded;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.textureCacheHits += hit;
    sFrameCounters.textureCacheMisses += miss;
    sFrameCounters.textureDecodes += decode;
    sFrameCounters.textureUploads += upload;
    sFrameCounters.textureBytesUploaded += bytesUploaded;
#endif
}

void PspProfiler_CountTextureWrapRequest(u32 requestS, u32 requestT) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.textureWrapRequestsS += requestS;
    sCounters.textureWrapRequestsT += requestT;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.textureWrapRequestsS += requestS;
    sFrameCounters.textureWrapRequestsT += requestT;
#endif
}

void PspProfiler_CountTextureWrapCall(u32 emittedS, u32 emittedT) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.textureWrapCallsEmittedS += emittedS;
    sCounters.textureWrapCallsEmittedT += emittedT;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.textureWrapCallsEmittedS += emittedS;
    sFrameCounters.textureWrapCallsEmittedT += emittedT;
#endif
}

void PspProfiler_CountTextureWrapSkip(u32 skippedS, u32 skippedT) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.textureWrapCallsSkippedS += skippedS;
    sCounters.textureWrapCallsSkippedT += skippedT;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.textureWrapCallsSkippedS += skippedS;
    sFrameCounters.textureWrapCallsSkippedT += skippedT;
#endif
}

void PspProfiler_CountTextureParameterCacheMiss(void) {
    if (sCaptureActive) {
        sCounters.textureParameterCacheMisses++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.textureParameterCacheMisses++;
#endif
    }
}

void PspProfiler_CountTextureParameterCacheReplacement(void) {
    if (sCaptureActive) {
        sCounters.textureParameterCacheReplacements++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.textureParameterCacheReplacements++;
#endif
    }
}

void PspProfiler_RecordTextureCacheLookup(PspProfileTextureCacheClass cache, u32 capacity, u32 occupied,
                                          u64 keyHash, u64 baseHash, int hit) {
    PspProfileTextureCacheState* state;
    PspProfileTextureCacheKey* key;

    if (!sCaptureActive || (cache >= PSP_PROFILE_TEXTURE_CACHE_COUNT)) {
        return;
    }
    state = &sCounters.textureCache[cache];
    state->capacity = capacity;
    state->currentEntries = occupied;
    if (occupied > state->peakEntries) {
        state->peakEntries = occupied;
    }
    state->lookups++;
    sCounters.textureCacheLookupSeq++;

    key = psp_profiler_track_texture_cache_key(cache, keyHash, baseHash);
    psp_profiler_track_texture_cache_base_key(cache, baseHash, keyHash);
    if (key != NULL) {
        if (!hit && key->evicted && !key->resident) {
            u64 distance = (sCounters.textureCacheLookupSeq >= key->evictionSeq)
                               ? (sCounters.textureCacheLookupSeq - key->evictionSeq)
                               : 0;

            state->reuseAfterEviction++;
            if (distance > state->maxReuseDistance) {
                state->maxReuseDistance = distance;
            }
            psp_profiler_count_texture_cache_reuse_distance(state, distance);
            key->evicted = 0;
        }
        key->lastSeenSeq = sCounters.textureCacheLookupSeq;
        key->resident = hit ? 1 : key->resident;
    }
    if (hit) {
        state->hits++;
    } else {
        state->misses++;
    }
}

void PspProfiler_RecordTextureCacheInsertion(PspProfileTextureCacheClass cache, u32 capacity, u32 occupied,
                                             u64 keyHash, u64 baseHash) {
    PspProfileTextureCacheState* state;
    PspProfileTextureCacheKey* key;

    if (!sCaptureActive || (cache >= PSP_PROFILE_TEXTURE_CACHE_COUNT)) {
        return;
    }
    state = &sCounters.textureCache[cache];
    state->capacity = capacity;
    state->currentEntries = occupied;
    if (occupied > state->peakEntries) {
        state->peakEntries = occupied;
    }
    state->insertions++;
    key = psp_profiler_track_texture_cache_key(cache, keyHash, baseHash);
    psp_profiler_track_texture_cache_base_key(cache, baseHash, keyHash);
    if (key != NULL) {
        key->resident = 1;
        key->evicted = 0;
        key->baseHash = baseHash;
    }
}

void PspProfiler_RecordTextureCacheEviction(PspProfileTextureCacheClass cache, u64 keyHash) {
    PspProfileTextureCacheState* state;
    PspProfileTextureCacheKey* key;

    if (!sCaptureActive || (cache >= PSP_PROFILE_TEXTURE_CACHE_COUNT)) {
        return;
    }
    state = &sCounters.textureCache[cache];
    state->evictions++;
    key = psp_profiler_find_texture_cache_key(cache, keyHash);
    if (key != NULL) {
        key->resident = 0;
        key->evicted = 1;
        key->evictionSeq = sCounters.textureCacheLookupSeq;
    }
}

void PspProfiler_CountBatchFlush(PspProfileFlushReason reason, u32 submittedVertices) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.batchFlushes++;
    if (reason < PSP_PROFILE_FLUSH_COUNT) {
        sCounters.flushReasons[reason]++;
    }
    sCounters.verticesSubmitted += submittedVertices;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.batchFlushes++;
    if (reason < PSP_PROFILE_FLUSH_COUNT) {
        sFrameCounters.flushReasons[reason]++;
    }
    sFrameCounters.verticesSubmitted += submittedVertices;
#endif
}

void PspProfiler_CountBatchStateTransitions(int textureIdChanged, int textureEnvChanged, int wrapSChanged,
                                           int wrapTChanged, int alphaTestChanged, int blendChanged,
                                           int premultipliedChanged) {
    if (!sCaptureActive) {
        return;
    }
    if (textureIdChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_TEXTURE_ID]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_TEXTURE_ID]++;
#endif
    }
    if (textureEnvChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_TEXTURE_ENV]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_TEXTURE_ENV]++;
#endif
    }
    if (wrapSChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_WRAP_S]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_WRAP_S]++;
#endif
    }
    if (wrapTChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_WRAP_T]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_WRAP_T]++;
#endif
    }
    if (alphaTestChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_ALPHA_TEST]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_ALPHA_TEST]++;
#endif
    }
    if (blendChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_BLEND]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_BLEND]++;
#endif
    }
    if (premultipliedChanged) {
        sCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_PREMULTIPLIED]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.batchStateTransitions[PSP_PROFILE_BATCH_STATE_PREMULTIPLIED]++;
#endif
    }
}

void PspProfiler_CountTextureFlushSource(PspProfileTextureFlushSource source) {
    if (!sCaptureActive) {
        return;
    }
    if (source < PSP_PROFILE_TEXTURE_FLUSH_COUNT) {
        sCounters.textureFlushSources[source]++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.textureFlushSources[source]++;
#endif
    }
}

void PspProfiler_CountTrianglePath(u32 directFastpathTriangles, u32 generalPathTriangles,
                                   u32 perspectivePathTriangles, u32 clippedPathTriangles,
                                   u32 directVerticesWritten) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.directFastpathTriangles += directFastpathTriangles;
    sCounters.generalPathTriangles += generalPathTriangles;
    sCounters.perspectivePathTriangles += perspectivePathTriangles;
    sCounters.clippedPathTriangles += clippedPathTriangles;
    sCounters.directVerticesWritten += directVerticesWritten;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.directFastpathTriangles += directFastpathTriangles;
    sFrameCounters.generalPathTriangles += generalPathTriangles;
    sFrameCounters.perspectivePathTriangles += perspectivePathTriangles;
    sFrameCounters.clippedPathTriangles += clippedPathTriangles;
    sFrameCounters.directVerticesWritten += directVerticesWritten;
#endif
}

void PspProfiler_CountEffectiveState(u32 resolves, u32 reuses, u32 materialResolves, u32 depthResolves,
                                     u32 fogResolves) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.effectiveStateResolves += resolves;
    sCounters.effectiveStateReuses += reuses;
    sCounters.materialStateResolves += materialResolves;
    sCounters.depthStateResolves += depthResolves;
    sCounters.fogStateResolves += fogResolves;
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.effectiveStateResolves += resolves;
    sFrameCounters.effectiveStateReuses += reuses;
    sFrameCounters.materialStateResolves += materialResolves;
    sFrameCounters.depthStateResolves += depthResolves;
    sFrameCounters.fogStateResolves += fogResolves;
#endif
}

void PspProfiler_CountDrawCall(u32 vertices) {
    if (sCaptureActive) {
        sCounters.drawCalls++;
        sPhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT].items += vertices;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.drawCalls++;
        sFramePhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT].items += vertices;
#endif
    }
}

void PspProfiler_CountPspglSubmitSplit(u32 smallDraw, u32 largeDraw, u32 vertices) {
    if (!sCaptureActive) {
        return;
    }
    if (smallDraw) {
        sPhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT_SMALL].items += vertices;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFramePhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT_SMALL].items += vertices;
#endif
    } else if (largeDraw) {
        sPhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT_LARGE].items += vertices;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFramePhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT_LARGE].items += vertices;
#endif
    }
}

void PspProfiler_CountPspglVertexStreamUploadSplit(u32 smallDraw, u32 largeDraw, u32 uploadBytes) {
    if (!sCaptureActive) {
        return;
    }
    if (smallDraw) {
        sPhase[PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_SMALL].items += uploadBytes;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFramePhase[PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_SMALL].items += uploadBytes;
#endif
    } else if (largeDraw) {
        sPhase[PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_LARGE].items += uploadBytes;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFramePhase[PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_LARGE].items += uploadBytes;
#endif
    }
}

void PspProfiler_CountVertexStream(u32 vboDraw, u32 vertices, u32 upload, u32 uploadBytes, u32 fallbackDraw,
                                   u32 fallbackVertices, u32 pageSwitch, u32 capacityBytes, u32 highWaterBytes,
                                   u32 smallVboDraw, u32 largeVboDraw, u32 smallVboVertices,
                                   u32 largeVboVertices) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.vboDrawCalls += vboDraw;
    sCounters.vboVertices += vertices;
    sCounters.smallVboDrawCalls += smallVboDraw;
    sCounters.largeVboDrawCalls += largeVboDraw;
    sCounters.smallVboVertices += smallVboVertices;
    sCounters.largeVboVertices += largeVboVertices;
    sCounters.vertexStreamUploadCalls += upload;
    sCounters.vertexStreamUploadBytes += uploadBytes;
    sPhase[PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD].items += uploadBytes;
    sCounters.clientArrayFallbackDraws += fallbackDraw;
    sCounters.clientArrayFallbackVertices += fallbackVertices;
    sCounters.vertexStreamPageSwitches += pageSwitch;
    if (capacityBytes > sCounters.vertexStreamCapacityBytes) {
        sCounters.vertexStreamCapacityBytes = capacityBytes;
    }
    if (highWaterBytes > sCounters.vertexStreamHighWaterBytes) {
        sCounters.vertexStreamHighWaterBytes = highWaterBytes;
    }
#if SF64_PSP_PROFILE_FRAME_TRACE
    sFrameCounters.vboDrawCalls += vboDraw;
    sFrameCounters.vboVertices += vertices;
    sFrameCounters.smallVboDrawCalls += smallVboDraw;
    sFrameCounters.largeVboDrawCalls += largeVboDraw;
    sFrameCounters.smallVboVertices += smallVboVertices;
    sFrameCounters.largeVboVertices += largeVboVertices;
    sFrameCounters.vertexStreamUploadCalls += upload;
    sFrameCounters.vertexStreamUploadBytes += uploadBytes;
    sFramePhase[PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD].items += uploadBytes;
    sFrameCounters.clientArrayFallbackDraws += fallbackDraw;
    sFrameCounters.clientArrayFallbackVertices += fallbackVertices;
    sFrameCounters.vertexStreamPageSwitches += pageSwitch;
    if (capacityBytes > sFrameCounters.vertexStreamCapacityBytes) {
        sFrameCounters.vertexStreamCapacityBytes = capacityBytes;
    }
    if (highWaterBytes > sFrameCounters.vertexStreamHighWaterBytes) {
        sFrameCounters.vertexStreamHighWaterBytes = highWaterBytes;
    }
#endif
}

void PspProfiler_CountGlFlush(void) {
    if (sCaptureActive) {
        sCounters.glFlushCalls++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.glFlushCalls++;
#endif
    }
}

void PspProfiler_CountSync(void) {
    if (sCaptureActive) {
        sCounters.syncCalls++;
#if SF64_PSP_PROFILE_FRAME_TRACE
        sFrameCounters.syncCalls++;
#endif
    }
}
#endif
