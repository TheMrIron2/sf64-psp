#include "src/psp/hw_counter_profile.h"
#include "src/psp/texture_attribution.h"

#if PROFILE_HW_COUNTERS

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <pspthreadman.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SF64_GIT_SHA
#define SF64_GIT_SHA "unknown"
#endif
#ifndef SF64_GIT_DIRTY
#define SF64_GIT_DIRTY "unknown"
#endif
#ifndef N64PSP_GIT_SHA
#define N64PSP_GIT_SHA "unknown"
#endif
#ifndef N64PSP_GIT_DIRTY
#define N64PSP_GIT_DIRTY "unknown"
#endif
#ifndef PSPGL_GIT_SHA
#define PSPGL_GIT_SHA "unknown"
#endif
#ifndef PSPGL_GIT_DIRTY
#define PSPGL_GIT_DIRTY "unknown"
#endif
#ifndef PSPGL_SOURCE_MODE
#define PSPGL_SOURCE_MODE "unknown"
#endif
#ifndef PSP_GFX_BACKEND_NAME
#define PSP_GFX_BACKEND_NAME "unknown"
#endif
#ifndef BUILD_COMPILER
#define BUILD_COMPILER "unknown"
#endif
#ifndef BUILD_OPT_FLAGS
#define BUILD_OPT_FLAGS "unknown"
#endif

#ifndef PROFILE_GPROF
#define PROFILE_GPROF 0
#endif
#ifndef PROFILE_PHASES
#define PROFILE_PHASES 0
#endif
#ifndef PROFILE_COMPONENTS
#define PROFILE_COMPONENTS 0
#endif
#ifndef PSPGL_SWAP_INTERVAL
#define PSPGL_SWAP_INTERVAL 0
#endif
#ifndef PSP_AUDIO
#define PSP_AUDIO 0
#endif
#ifndef PSP_FPS_OVERLAY
#define PSP_FPS_OVERLAY 0
#endif
#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif
#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#define PSP_HW_PROFILE_DIR "ms0:/PSP/GAME/SF64PROFILE"
#define PSP_HW_PROFILE_DIR_EF0 "ef0:/PSP/GAME/SF64PROFILE"
#define PSP_HW_PROFILE_MAX_SLOT 999
#define PSP_HW_COUNTER_COUNT 19
#define PSP_HW_RATIO_SCALE 1000
#define PSP_HW_FLUSH_REASON_COUNT 8
#define PSP_HW_TEXTURE_BARRIER_SOURCE_COUNT 4
#define PSP_HW_VBLANK_US 16667
#define PSP_HW_VBLANK_BUCKET_COUNT 4
#define PSP_HW_DL_SOURCE_SLOTS 4096
#define PSP_HW_EFFECT_TYPE_COUNT 512

typedef enum {
    PSP_HW_STATUS_READY,
    PSP_HW_STATUS_WARMUP,
    PSP_HW_STATUS_RECORDING,
    PSP_HW_STATUS_SAVING,
    PSP_HW_STATUS_SAVED,
    PSP_HW_STATUS_ERROR
} PspHwCounterStatus;

/* Which dump step failed, reported on screen so a failure needs no guessing */
typedef enum {
    PSP_HW_STEP_NONE,
    PSP_HW_STEP_SLOT,
    PSP_HW_STEP_OPEN,
    PSP_HW_STEP_META,
    PSP_HW_STEP_WORK,
    PSP_HW_STEP_SCOPES,
    PSP_HW_STEP_COUNTERS,
    PSP_HW_STEP_DIAGNOSTICS,
    PSP_HW_STEP_PACING,
    PSP_HW_STEP_SOURCES,
    PSP_HW_STEP_FRAMES
} PspHwCounterStep;

typedef enum {
    PSP_HW_ROOT_MS0,
    PSP_HW_ROOT_EF0,
    PSP_HW_ROOT_HOST0,
    PSP_HW_ROOT_COUNT
} PspHwCounterRoot;

typedef enum {
    PSP_HW_SOURCE_NONE,
    PSP_HW_SOURCE_THREAD,
    PSP_HW_SOURCE_GLOBAL
} PspHwCounterSource;

typedef struct {
    u64 timeUs;
    u32 counter[PSP_HW_COUNTER_COUNT];
} PspHwCounterSample;

typedef struct {
    u64 counter[PSP_HW_COUNTER_COUNT];
    u64 elapsedUs;
    u32 samples;
} PspHwCounterScopeTotals;

typedef struct {
    u64 lookups;
    u64 hits;
    u64 misses;
    u64 uploads;
    u64 uploadBytes;
    u64 evictions;
} PspHwTextureCacheTotals;

typedef enum {
    PSP_HW_PACING_FRAME_INTERVAL,
    PSP_HW_PACING_TASK,
    PSP_HW_PACING_PRESENT,
    PSP_HW_PACING_TASK_AND_PRESENT,
    PSP_HW_PACING_COUNT
} PspHwPacingMetric;

typedef struct {
    u64 elapsedUs;
    u32 maxUs;
    u32 samples;
    u32 vblankBuckets[PSP_HW_VBLANK_BUCKET_COUNT];
} PspHwPacingTotals;

typedef struct {
    PspHwCounterScopeTotals scope[PSP_HW_SCOPE_COUNT];
    PspHwTextureCacheTotals textureCache[PSP_HW_TEXTURE_CACHE_COUNT];
    u64 batchFlushes[PSP_HW_FLUSH_REASON_COUNT];
    u64 batchFlushVertices[PSP_HW_FLUSH_REASON_COUNT];
    u64 textureBarriers[PSP_HW_TEXTURE_BARRIER_SOURCE_COUNT];
    u64 poolEvents[PSP_HW_POOL_EVENT_COUNT];
    PspHwPacingTotals pacing[PSP_HW_PACING_COUNT];
    u64 commands;
    u64 loadedVertices;
    u64 submittedVertices;
    u32 frames;
} PspHwCounterTotals;

typedef struct {
    u32 frameIntervalUs;
    u32 taskUs;
    u32 presentUs;
    u32 commands;
    u32 loadedVertices;
    u32 submittedVertices;
    u32 textureUploadBytes;
    u32 taskCounter[PSP_HW_COUNTER_COUNT];
    u32 commandSources[PSP_COMMAND_SOURCE_COUNT];
    u32 commandSourceUs[PSP_COMMAND_SOURCE_COUNT];
    u32 waterUs[PSP_WATER_TILE_COUNT];
    u32 waterInputTriangles[PSP_WATER_TILE_COUNT];
    u32 waterEmittedTriangles[PSP_WATER_TILE_COUNT];
    u32 topEffectId;
    u32 topEffectUs;
    u32 secondEffectId;
    u32 secondEffectUs;
    u32 effectTotalUs;
    u32 waterGroupsSkipped;
    u32 waterPairsSkipped;
    u32 waterCommandsSkipped;
#if PSP_GFX_BACKEND_GU
    u32 texture[PSP_HW_TEXTURE_FRAME_COUNT];
#endif
} PspHwFrameRecord;

static const char* sPspHwCounterNames[PSP_HW_COUNTER_COUNT] = {
    "systemck",   "cpuck",         "internal_stall", "memory_stall", "copz_stall",
    "vfpu_stall", "sleep",         "bus_access",     "uncached_load", "uncached_store",
    "cached_load", "cached_store", "i_miss",         "d_miss",        "d_writeback",
    "cop0_inst",  "fpu_inst",      "vfpu_inst",      "local_bus"
};

static const char* sPspHwCommandSourceNames[PSP_COMMAND_SOURCE_COUNT] = {
    "unattributed", "starfield", "backdrop", "ground", "player", "player_reflection",
    "scenery", "boss", "sprite", "actor", "item", "shots", "effects",
    "effects_reflection", "player_details", "hud"
};

static const char* sPspHwScopeNames[PSP_HW_SCOPE_COUNT] = {
    "task", "frontend", "flush", "present", "texture", "vertex", "submit", "triangle", "clipping", "batch"
};

static const char* sPspHwTextureCacheNames[PSP_HW_TEXTURE_CACHE_COUNT] = {
    "ci8", "rgba16", "rgba32", "converted"
};

static const char* sPspHwFlushReasonNames[PSP_HW_FLUSH_REASON_COUNT] = {
    "buffer_full", "texture_change", "render_state_change", "transform_change",
    "clipping_path", "end_of_task", "explicit_sync", "other"
};

static const char* sPspHwTextureBarrierNames[PSP_HW_TEXTURE_BARRIER_SOURCE_COUNT] = {
    "material_key", "texture_enable", "cache_miss_upload", "set_texture_image"
};

static const char* sPspHwPoolEventNames[PSP_HW_POOL_EVENT_COUNT] = {
    "hit", "open", "eviction", "capacity_flush", "drained", "unpooled", "reservation_fallback"
};

static const char* sPspHwPacingMetricNames[PSP_HW_PACING_COUNT] = {
    "frame_interval", "task", "present", "task_and_present"
};

/* Scene tags for the audit standard workloads plus a free slot */
static const char* sPspHwSceneNames[] = { "title", "corneria", "light", "other" };
#define PSP_HW_SCENE_COUNT ((u32) (sizeof(sPspHwSceneNames) / sizeof(sPspHwSceneNames[0])))

/* ms0 is absent on a PSP Go without an M2 card, ef0 is its internal flash
 * host0 is the PSPLINK host directory and catches both refusing writes */
static const char* sPspHwRoots[PSP_HW_ROOT_COUNT] = { PSP_HW_PROFILE_DIR, PSP_HW_PROFILE_DIR_EF0, "host0:" };
static const char sPspHwRootLetters[PSP_HW_ROOT_COUNT] = { 'M', 'E', 'H' };
static const char* sPspHwStepNames[] = { "OK", "SLOT", "OPEN", "META", "WORK", "SCOPE", "CTRS", "DIAG", "PACE", "SOURCE", "FRAMES" };

static PspHwCounterTotals sPspHwTotals;
static PspHwFrameRecord sPspHwFrames[PSP_HW_COUNTER_CAPTURE_FRAMES];
static PspCommandSourceStats sPspHwCurrentCommandSources[PSP_COMMAND_SOURCE_COUNT];
static u32 sPspHwCommandSourceMismatchFrames;
static struct {
    u64 commands;
    u64 vertexCommands;
    u64 triangleCommands;
    u64 displayListCalls;
    u64 frontendUs;
} sPspHwCommandSourceTotals[PSP_COMMAND_SOURCE_COUNT];
static struct {
    u32 source;
    u32 rawTarget;
    u32 resolvedTarget;
    u32 calls;
    u64 inclusiveCommands;
} sPspHwDlSources[PSP_HW_DL_SOURCE_SLOTS];
static u32 sPspHwDlSourceUsed;
static u32 sPspHwDlSourceOverflowCalls;
static u64 sPspHwDlSourceOverflowCommands;
static int sPspHwDlSourceFull;
static u64 sPspHwWaterGroupsSeen;
static u64 sPspHwWaterGroupsEvaluated;
static u64 sPspHwWaterGroupsSkipped;
static u64 sPspHwWaterPairsSkipped;
static u64 sPspHwWaterCommandsSkipped;
static u32 sPspHwCurrentWaterGroupsSkipped;
static u32 sPspHwCurrentWaterPairsSkipped;
static u32 sPspHwCurrentWaterCommandsSkipped;
static struct {
    u64 elapsedUs;
    u64 commands;
    u64 loadedVertices;
    u64 inputTriangles;
    u64 emittedTriangles;
    u32 calls;
    u32 zeroOutputCalls;
} sPspHwWaterTotals[PSP_WATER_TILE_COUNT];
static u32 sPspHwCurrentWaterUs[PSP_WATER_TILE_COUNT];
static u32 sPspHwCurrentWaterInput[PSP_WATER_TILE_COUNT];
static u32 sPspHwCurrentWaterEmitted[PSP_WATER_TILE_COUNT];
static struct {
    u64 elapsedUs;
    u64 commands;
    u32 calls;
    u32 maxFrameUs;
} sPspHwEffectTotals[PSP_HW_EFFECT_TYPE_COUNT];
static u32 sPspHwCurrentEffectUs[PSP_HW_EFFECT_TYPE_COUNT];
static PspHwCounterSample sPspHwScopeStart[PSP_HW_SCOPE_COUNT];
static const volatile PspDebugProfilerRegs* sPspHwRegs;
static PspHwCounterSource sPspHwSource;
static PspHwCounterStatus sPspHwStatus = PSP_HW_STATUS_READY;
static u32 sPspHwScene;
static u32 sPspHwSavedSlot;
static u32 sPspHwWarmupFrames;
static u32 sPspHwPreviousButtons;
static volatile int sPspHwCaptureActive;
static volatile int sPspHwStopRequested;
static int sPspHwFrameArmed;
static int sPspHwRenderThreadBound;
static volatile int sPspHwDumpPending;
static PspHwCounterStep sPspHwErrorStep;
static int sPspHwErrorCode;
static u32 sPspHwErrorRoot;
static u64 sPspHwLastFrameBeginUs;
static u32 sPspHwCurrentFrameIntervalUs;
static u32 sPspHwCurrentTaskUs;
static u32 sPspHwCurrentPresentUs;
static u32 sPspHwCurrentTaskCounters[PSP_HW_COUNTER_COUNT];
static u64 sPspHwLastTextureUploadBytes;
static int sPspHwFrameBeginValid;
static int sPspHwCurrentFrameIntervalValid;
static int sPspHwCurrentTaskValid;
static int sPspHwCurrentPresentValid;
static u32 sPspHwPacingSamples[PSP_HW_PACING_COUNT][PSP_HW_COUNTER_CAPTURE_FRAMES];

static u64 psp_hw_ratio(u64 value, u64 denominator) {
    if (denominator == 0) {
        return 0;
    }
    return (value * PSP_HW_RATIO_SCALE) / denominator;
}

static int psp_hw_compare_u32(const void* a, const void* b) {
    u32 lhs = *(const u32*) a;
    u32 rhs = *(const u32*) b;

    return (lhs > rhs) - (lhs < rhs);
}

static void psp_hw_record_pacing(PspHwPacingMetric metric, u32 elapsedUs) {
    PspHwPacingTotals* totals = &sPspHwTotals.pacing[metric];
    u32 vblanks;

    if (totals->samples >= PSP_HW_COUNTER_CAPTURE_FRAMES) {
        return;
    }
    sPspHwPacingSamples[metric][totals->samples++] = elapsedUs;
    totals->elapsedUs += elapsedUs;
    if (elapsedUs > totals->maxUs) {
        totals->maxUs = elapsedUs;
    }
    vblanks = (elapsedUs + (PSP_HW_VBLANK_US / 2U)) / PSP_HW_VBLANK_US;
    if (vblanks == 0) {
        vblanks = 1;
    }
    if (vblanks >= PSP_HW_VBLANK_BUCKET_COUNT) {
        vblanks = PSP_HW_VBLANK_BUCKET_COUNT;
    }
    totals->vblankBuckets[vblanks - 1]++;
}

static u32 psp_hw_pacing_percentile(PspHwPacingMetric metric, u32 percentile) {
    PspHwPacingTotals* totals = &sPspHwTotals.pacing[metric];
    u32 rank;

    if (totals->samples == 0) {
        return 0;
    }
    qsort(sPspHwPacingSamples[metric], totals->samples, sizeof(sPspHwPacingSamples[metric][0]),
          psp_hw_compare_u32);
    rank = ((totals->samples * percentile) + 99U) / 100U;
    return sPspHwPacingSamples[metric][rank - 1];
}

/* Firmware exposes these only when profiler mode was chosen before ThreadMan init
 * both return NULL otherwise, which is why they are safe to attempt */
static void psp_hw_bind_counters(void) {
    const volatile PspDebugProfilerRegs* regs = sceKernelReferThreadProfiler();

    if (regs != NULL) {
        sPspHwRegs = regs;
        sPspHwSource = PSP_HW_SOURCE_THREAD;
        return;
    }

    regs = sceKernelReferGlobalProfiler();
    if (regs != NULL) {
        sPspHwRegs = regs;
        sPspHwSource = PSP_HW_SOURCE_GLOBAL;
        return;
    }

    sPspHwRegs = NULL;
    sPspHwSource = PSP_HW_SOURCE_NONE;
}

static const char* psp_hw_source_name(void) {
    if (sPspHwSource == PSP_HW_SOURCE_THREAD) {
        return "thread_profiler";
    }
    if (sPspHwSource == PSP_HW_SOURCE_GLOBAL) {
        return "global_profiler";
    }
    return "none";
}

static void psp_hw_sample(PspHwCounterSample* sample) {
    const volatile PspDebugProfilerRegs* regs = sPspHwRegs;

    sample->timeUs = (u64) sceKernelGetSystemTimeWide();
    if (regs == NULL) {
        return;
    }

    sample->counter[0] = regs->systemck;
    sample->counter[1] = regs->cpuck;
    sample->counter[2] = regs->internal;
    sample->counter[3] = regs->memory;
    sample->counter[4] = regs->copz;
    sample->counter[5] = regs->vfpu;
    sample->counter[6] = regs->sleep;
    sample->counter[7] = regs->bus_access;
    sample->counter[8] = regs->uncached_load;
    sample->counter[9] = regs->uncached_store;
    sample->counter[10] = regs->cached_load;
    sample->counter[11] = regs->cached_store;
    sample->counter[12] = regs->i_miss;
    sample->counter[13] = regs->d_miss;
    sample->counter[14] = regs->d_writeback;
    sample->counter[15] = regs->cop0_inst;
    sample->counter[16] = regs->fpu_inst;
    sample->counter[17] = regs->vfpu_inst;
    sample->counter[18] = regs->local_bus;
}

static int psp_hw_write_all(SceUID fd, const char* text) {
    u32 length = (u32) strlen(text);
    int written = sceIoWrite(fd, text, length);

    if (written != (int) length) {
        sPspHwErrorCode = written;
        return 0;
    }
    return 1;
}

#if PSP_GFX_BACKEND_GU
#include "src/psp/texture_attribution.inc.c"
#endif

static int psp_hw_find_slot(u32 root, char* path, u32 pathSize, u32* slotOut) {
    SceIoStat stat;
    u32 slot;

    /* host0 is the PSPLINK host directory, do not create trees on the PC there */
    if (root == PSP_HW_ROOT_MS0) {
        sceIoMkdir("ms0:/PSP", 0777);
        sceIoMkdir("ms0:/PSP/GAME", 0777);
        sceIoMkdir(PSP_HW_PROFILE_DIR, 0777);
    } else if (root == PSP_HW_ROOT_EF0) {
        sceIoMkdir("ef0:/PSP", 0777);
        sceIoMkdir("ef0:/PSP/GAME", 0777);
        sceIoMkdir(PSP_HW_PROFILE_DIR_EF0, 0777);
    }

    for (slot = 0; slot <= PSP_HW_PROFILE_MAX_SLOT; slot++) {
        snprintf(path, pathSize, "%s/hw-%03lu-%s.csv", sPspHwRoots[root], (unsigned long) slot,
                 sPspHwSceneNames[sPspHwScene]);
        if (sceIoGetstat(path, &stat) < 0) {
            *slotOut = slot;
            return 1;
        }
    }
    sPspHwErrorCode = 0;
    return 0;
}

static int psp_hw_dump_metadata(SceUID fd) {
    char line[512];

    snprintf(line, sizeof(line),
             "key,value\n"
             "scene,%s\n"
             "counter_source,%s\n"
             "frames,%lu\n"
             "warmup_frames,%u\n",
             sPspHwSceneNames[sPspHwScene], psp_hw_source_name(),
             (unsigned long) sPspHwTotals.frames, PSP_HW_COUNTER_WARMUP_FRAMES);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }

    snprintf(line, sizeof(line),
             "sf64_commit,%s\n"
             "sf64_tree,%s\n"
             "n64psp_commit,%s\n"
             "n64psp_tree,%s\n"
             "renderer_backend,%s\n",
             SF64_GIT_SHA, SF64_GIT_DIRTY, N64PSP_GIT_SHA, N64PSP_GIT_DIRTY, PSP_GFX_BACKEND_NAME);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }

    snprintf(line, sizeof(line),
             "pspgl_commit,%s\n"
             "pspgl_tree,%s\n"
             "pspgl_source,%s\n",
             PSPGL_GIT_SHA, PSPGL_GIT_DIRTY, PSPGL_SOURCE_MODE);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }

    snprintf(line, sizeof(line),
             "compiler,%s\n"
             "opt_flags,%s\n",
             BUILD_COMPILER, BUILD_OPT_FLAGS);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }

    snprintf(line, sizeof(line),
             "build_flags,PROFILE_HW_COUNTERS=1 PROFILE_HW_COUNTER_SCOPES=%d PROFILE_PHASES=%d "
             "PROFILE_COMPONENTS=%d PROFILE_GPROF=%d PSP_FPS_OVERLAY=%d PSP_RENDERER_DIAGNOSTICS=%d "
             "PSP_LOG=%d PSP_AUDIO=%d PSPGL_SWAP_INTERVAL=%d\n",
             PROFILE_HW_COUNTER_SCOPES, PROFILE_PHASES, PROFILE_COMPONENTS, PROFILE_GPROF,
             PSP_FPS_OVERLAY, PSP_RENDERER_DIAGNOSTICS, PSP_LOG_ENABLED, PSP_AUDIO, PSPGL_SWAP_INTERVAL);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }

    snprintf(line, sizeof(line),
             "cpu_mhz,%d\n"
             "bus_mhz,%d\n"
             "ratio_scale,%u\n",
             scePowerGetCpuClockFrequencyInt(), scePowerGetBusClockFrequencyInt(), PSP_HW_RATIO_SCALE);
    return psp_hw_write_all(fd, line);
}

static int psp_hw_dump_work(SceUID fd) {
    char line[256];

    snprintf(line, sizeof(line),
             "\n[work]\n"
             "metric,total,per_frame_x1000\n"
             "commands,%llu,%llu\n"
             "loaded_vertices,%llu,%llu\n"
             "submitted_vertices,%llu,%llu\n",
             (unsigned long long) sPspHwTotals.commands,
             (unsigned long long) psp_hw_ratio(sPspHwTotals.commands, sPspHwTotals.frames),
             (unsigned long long) sPspHwTotals.loadedVertices,
             (unsigned long long) psp_hw_ratio(sPspHwTotals.loadedVertices, sPspHwTotals.frames),
             (unsigned long long) sPspHwTotals.submittedVertices,
             (unsigned long long) psp_hw_ratio(sPspHwTotals.submittedVertices, sPspHwTotals.frames));
    return psp_hw_write_all(fd, line);
}

static int psp_hw_dump_scopes(SceUID fd) {
    char line[256];
    u32 i;

    if (!psp_hw_write_all(fd, "\n[scopes]\nscope,samples,elapsed_us,us_per_frame_x1000,us_per_sample_x1000\n")) {
        return 0;
    }

    for (i = 0; i < PSP_HW_SCOPE_COUNT; i++) {
        const PspHwCounterScopeTotals* totals = &sPspHwTotals.scope[i];

        if (totals->samples == 0) {
            continue;
        }
        snprintf(line, sizeof(line), "%s,%lu,%llu,%llu,%llu\n", sPspHwScopeNames[i],
                 (unsigned long) totals->samples, (unsigned long long) totals->elapsedUs,
                 (unsigned long long) psp_hw_ratio(totals->elapsedUs, sPspHwTotals.frames),
                 (unsigned long long) psp_hw_ratio(totals->elapsedUs, totals->samples));
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_counters(SceUID fd) {
    char line[256];
    u32 scope;
    u32 i;

    if (sPspHwSource == PSP_HW_SOURCE_NONE) {
        return psp_hw_write_all(fd, "\n[counters]\n# unavailable: firmware profiler mode was not "
                                    "enabled before ThreadMan init; timings above are still valid\n");
    }

    if (!psp_hw_write_all(fd, "\n[counters]\nscope,counter,total,per_frame_x1000,per_sample_x1000,per_command_x1000,"
                              "per_loaded_vertex_x1000,per_submitted_vertex_x1000\n")) {
        return 0;
    }

    for (scope = 0; scope < PSP_HW_SCOPE_COUNT; scope++) {
        const PspHwCounterScopeTotals* totals = &sPspHwTotals.scope[scope];

        if (totals->samples == 0) {
            continue;
        }
        for (i = 0; i < PSP_HW_COUNTER_COUNT; i++) {
            u64 total = totals->counter[i];

            snprintf(line, sizeof(line), "%s,%s,%llu,%llu,%llu,%llu,%llu,%llu\n", sPspHwScopeNames[scope],
                     sPspHwCounterNames[i], (unsigned long long) total,
                     (unsigned long long) psp_hw_ratio(total, sPspHwTotals.frames),
                     (unsigned long long) psp_hw_ratio(total, totals->samples),
                     (unsigned long long) psp_hw_ratio(total, sPspHwTotals.commands),
                     (unsigned long long) psp_hw_ratio(total, sPspHwTotals.loadedVertices),
                     (unsigned long long) psp_hw_ratio(total, sPspHwTotals.submittedVertices));
            if (!psp_hw_write_all(fd, line)) {
                return 0;
            }
        }
    }
    return 1;
}

static int psp_hw_dump_diagnostics(SceUID fd) {
    char line[256];
    u32 i;

    if (!psp_hw_write_all(fd,
                          "\n[texture cache]\n"
                          "cache,lookups,hits,misses,uploads,bytes_uploaded,evictions,lookups_per_frame_x1000,"
                          "misses_per_frame_x1000\n")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_TEXTURE_CACHE_COUNT; i++) {
        const PspHwTextureCacheTotals* totals = &sPspHwTotals.textureCache[i];

        snprintf(line, sizeof(line), "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", sPspHwTextureCacheNames[i],
                 (unsigned long long) totals->lookups, (unsigned long long) totals->hits,
                 (unsigned long long) totals->misses, (unsigned long long) totals->uploads,
                 (unsigned long long) totals->uploadBytes, (unsigned long long) totals->evictions,
                 (unsigned long long) psp_hw_ratio(totals->lookups, sPspHwTotals.frames),
                 (unsigned long long) psp_hw_ratio(totals->misses, sPspHwTotals.frames));
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }

#if PSP_GFX_BACKEND_GU
    if (!psp_hw_texture_dump(fd)) {
        return 0;
    }
#endif

    if (!psp_hw_write_all(fd, "\n[batch flush reasons]\nreason,count,vertices,count_per_frame_x1000\n")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_FLUSH_REASON_COUNT; i++) {
        snprintf(line, sizeof(line), "%s,%llu,%llu,%llu\n", sPspHwFlushReasonNames[i],
                 (unsigned long long) sPspHwTotals.batchFlushes[i],
                 (unsigned long long) sPspHwTotals.batchFlushVertices[i],
                 (unsigned long long) psp_hw_ratio(sPspHwTotals.batchFlushes[i], sPspHwTotals.frames));
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }

    if (!psp_hw_write_all(fd, "\n[texture barriers]\nsource,count,count_per_frame_x1000\n")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_TEXTURE_BARRIER_SOURCE_COUNT; i++) {
        snprintf(line, sizeof(line), "%s,%llu,%llu\n", sPspHwTextureBarrierNames[i],
                 (unsigned long long) sPspHwTotals.textureBarriers[i],
                 (unsigned long long) psp_hw_ratio(sPspHwTotals.textureBarriers[i], sPspHwTotals.frames));
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }

    if (!psp_hw_write_all(fd, "\n[batch pool]\nmetric,total,per_frame_x1000\n")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_POOL_EVENT_COUNT; i++) {
        snprintf(line, sizeof(line), "%s,%llu,%llu\n", sPspHwPoolEventNames[i],
                 (unsigned long long) sPspHwTotals.poolEvents[i],
                 (unsigned long long) psp_hw_ratio(sPspHwTotals.poolEvents[i], sPspHwTotals.frames));
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_pacing(SceUID fd) {
    char line[256];
    u32 i;

    if (!psp_hw_write_all(fd,
                          "\n[frame pacing]\n"
                          "metric,samples,average_us,p95_us,p99_us,max_us,one_vblank,two_vblanks,"
                          "three_vblanks,four_or_more_vblanks\n")) {
        return 0;
    }
    for (i = 0; i < PSP_HW_PACING_COUNT; i++) {
        const PspHwPacingTotals* totals = &sPspHwTotals.pacing[i];

        snprintf(line, sizeof(line), "%s,%lu,%llu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", sPspHwPacingMetricNames[i],
                 (unsigned long) totals->samples,
                 (unsigned long long) (totals->samples ? (totals->elapsedUs / totals->samples) : 0),
                 (unsigned long) psp_hw_pacing_percentile((PspHwPacingMetric) i, 95),
                 (unsigned long) psp_hw_pacing_percentile((PspHwPacingMetric) i, 99),
                 (unsigned long) totals->maxUs, (unsigned long) totals->vblankBuckets[0],
                 (unsigned long) totals->vblankBuckets[1], (unsigned long) totals->vblankBuckets[2],
                 (unsigned long) totals->vblankBuckets[3]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_command_sources(SceUID fd) {
    char line[384];
    u32 frame;
    u32 source;

    snprintf(line, sizeof(line), "\n[command source diagnostics]\nmetric,value\n"
                                 "source_mismatch_frames,%lu\n\n[command sources]\n"
                                 "source,commands,per_frame_x1000,vertex_commands,triangle_commands,"
                                 "display_list_calls,frontend_us,frontend_us_per_frame_x1000\n",
             (unsigned long) sPspHwCommandSourceMismatchFrames);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }
    for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
        snprintf(line, sizeof(line), "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n", sPspHwCommandSourceNames[source],
                 (unsigned long long) sPspHwCommandSourceTotals[source].commands,
                 (unsigned long long) psp_hw_ratio(sPspHwCommandSourceTotals[source].commands, sPspHwTotals.frames),
                 (unsigned long long) sPspHwCommandSourceTotals[source].vertexCommands,
                 (unsigned long long) sPspHwCommandSourceTotals[source].triangleCommands,
                 (unsigned long long) sPspHwCommandSourceTotals[source].displayListCalls,
                 (unsigned long long) sPspHwCommandSourceTotals[source].frontendUs,
                 (unsigned long long) psp_hw_ratio(sPspHwCommandSourceTotals[source].frontendUs, sPspHwTotals.frames));
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[command source frames]\nframe")) {
        return 0;
    }
    for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
        snprintf(line, sizeof(line), ",%s", sPspHwCommandSourceNames[source]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }
    for (frame = 0; frame < sPspHwTotals.frames; frame++) {
        u32 length = (u32) snprintf(line, sizeof(line), "%lu", (unsigned long) frame);

        for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
            length += (u32) snprintf(line + length, sizeof(line) - length, ",%lu",
                                     (unsigned long) sPspHwFrames[frame].commandSources[source]);
        }
        line[length++] = '\n';
        line[length] = 0;
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[command source timing frames]\nframe")) {
        return 0;
    }
    for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
        snprintf(line, sizeof(line), ",%s", sPspHwCommandSourceNames[source]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }
    for (frame = 0; frame < sPspHwTotals.frames; frame++) {
        u32 length = (u32) snprintf(line, sizeof(line), "%lu", (unsigned long) frame);

        for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
            length += (u32) snprintf(line + length, sizeof(line) - length, ",%lu",
                                     (unsigned long) sPspHwFrames[frame].commandSourceUs[source]);
        }
        line[length++] = '\n';
        line[length] = 0;
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_water_and_effects(SceUID fd) {
    char line[384];
    u32 tile;
    u32 id;
    u32 frame;

    snprintf(line, sizeof(line), "\n[water group cull]\nmetric,value\ngroups_seen,%llu\n"
                                 "groups_evaluated,%llu\ngroups_skipped,%llu\n"
                                 "triangle_pairs_skipped,%llu\ncommands_skipped,%llu\n",
             (unsigned long long) sPspHwWaterGroupsSeen,
             (unsigned long long) sPspHwWaterGroupsEvaluated,
             (unsigned long long) sPspHwWaterGroupsSkipped,
             (unsigned long long) sPspHwWaterPairsSkipped,
             (unsigned long long) sPspHwWaterCommandsSkipped);
    if (!psp_hw_write_all(fd, line) ||
        !psp_hw_write_all(fd, "\n[water group cull frames]\nframe,groups_skipped,triangle_pairs_skipped,"
                              "commands_skipped\n")) {
        return 0;
    }
    for (frame = 0; frame < sPspHwTotals.frames; frame++) {
        snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu\n", (unsigned long) frame,
                 (unsigned long) sPspHwFrames[frame].waterGroupsSkipped,
                 (unsigned long) sPspHwFrames[frame].waterPairsSkipped,
                 (unsigned long) sPspHwFrames[frame].waterCommandsSkipped);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[water tiles]\ntile,x,z,calls,frontend_us,commands,loaded_vertices,"
                              "input_triangles,emitted_triangles,zero_output_calls\n")) {
        return 0;
    }
    for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
        snprintf(line, sizeof(line), "%lu,%ld,%ld,%lu,%llu,%llu,%llu,%llu,%llu,%lu\n",
                 (unsigned long) tile, (long) (tile % 3) - 1, -(long) (tile / 3),
                 (unsigned long) sPspHwWaterTotals[tile].calls,
                 (unsigned long long) sPspHwWaterTotals[tile].elapsedUs,
                 (unsigned long long) sPspHwWaterTotals[tile].commands,
                 (unsigned long long) sPspHwWaterTotals[tile].loadedVertices,
                 (unsigned long long) sPspHwWaterTotals[tile].inputTriangles,
                 (unsigned long long) sPspHwWaterTotals[tile].emittedTriangles,
                 (unsigned long) sPspHwWaterTotals[tile].zeroOutputCalls);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[water tile frames]\nframe")) {
        return 0;
    }
    for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
        snprintf(line, sizeof(line), ",tile%lu_us", (unsigned long) tile);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
        snprintf(line, sizeof(line), ",tile%lu_input", (unsigned long) tile);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
        snprintf(line, sizeof(line), ",tile%lu_emitted", (unsigned long) tile);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }
    for (frame = 0; frame < sPspHwTotals.frames; frame++) {
        u32 length = (u32) snprintf(line, sizeof(line), "%lu", (unsigned long) frame);

        for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
            length += (u32) snprintf(line + length, sizeof(line) - length, ",%lu",
                                     (unsigned long) sPspHwFrames[frame].waterUs[tile]);
        }
        for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
            length += (u32) snprintf(line + length, sizeof(line) - length, ",%lu",
                                     (unsigned long) sPspHwFrames[frame].waterInputTriangles[tile]);
        }
        for (tile = 0; tile < PSP_WATER_TILE_COUNT; tile++) {
            length += (u32) snprintf(line + length, sizeof(line) - length, ",%lu",
                                     (unsigned long) sPspHwFrames[frame].waterEmittedTriangles[tile]);
        }
        line[length++] = '\n';
        line[length] = 0;
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[effect types]\nid,calls,commands,frontend_us,max_frame_us\n")) {
        return 0;
    }
    for (id = 0; id < PSP_HW_EFFECT_TYPE_COUNT; id++) {
        if (sPspHwEffectTotals[id].calls == 0) {
            continue;
        }
        snprintf(line, sizeof(line), "%lu,%lu,%llu,%llu,%lu\n", (unsigned long) id,
                 (unsigned long) sPspHwEffectTotals[id].calls,
                 (unsigned long long) sPspHwEffectTotals[id].commands,
                 (unsigned long long) sPspHwEffectTotals[id].elapsedUs,
                 (unsigned long) sPspHwEffectTotals[id].maxFrameUs);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n[effect type frames]\nframe,top_id,top_us,second_id,second_us,total_us\n")) {
        return 0;
    }
    for (frame = 0; frame < sPspHwTotals.frames; frame++) {
        snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,%lu,%lu\n", (unsigned long) frame,
                 (unsigned long) sPspHwFrames[frame].topEffectId,
                 (unsigned long) sPspHwFrames[frame].topEffectUs,
                 (unsigned long) sPspHwFrames[frame].secondEffectId,
                 (unsigned long) sPspHwFrames[frame].secondEffectUs,
                 (unsigned long) sPspHwFrames[frame].effectTotalUs);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_display_list_sources(SceUID fd) {
    char line[192];
    u32 slot;

    snprintf(line, sizeof(line), "\n[display list source diagnostics]\nmetric,value\n"
                                 "table_slots,%u\ntable_used,%lu\noverflow_calls,%lu\n"
                                 "overflow_inclusive_commands,%llu\n",
             PSP_HW_DL_SOURCE_SLOTS, (unsigned long) sPspHwDlSourceUsed,
             (unsigned long) sPspHwDlSourceOverflowCalls,
             (unsigned long long) sPspHwDlSourceOverflowCommands);
    if (!psp_hw_write_all(fd, line) ||
        !psp_hw_write_all(fd, "\n[display list sources]\nsource,raw_target,resolved_target,calls,"
                              "inclusive_commands\n")) {
        return 0;
    }
    for (slot = 0; slot < PSP_HW_DL_SOURCE_SLOTS; slot++) {
        if (sPspHwDlSources[slot].calls == 0) {
            continue;
        }
        snprintf(line, sizeof(line), "%s,0x%08lx,0x%08lx,%lu,%llu\n",
                 sPspHwCommandSourceNames[sPspHwDlSources[slot].source],
                 (unsigned long) sPspHwDlSources[slot].rawTarget,
                 (unsigned long) sPspHwDlSources[slot].resolvedTarget,
                 (unsigned long) sPspHwDlSources[slot].calls,
                 (unsigned long long) sPspHwDlSources[slot].inclusiveCommands);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_frames(SceUID fd) {
    char line[384];
    u32 frame;
    u32 counter;

    if (!psp_hw_write_all(fd, "\n[frames]\nframe,frame_interval_us,task_us,present_us,commands,"
                              "loaded_vertices,submitted_vertices,texture_upload_bytes")) {
        return 0;
    }
    for (counter = 0; counter < PSP_HW_COUNTER_COUNT; counter++) {
        snprintf(line, sizeof(line), ",%s", sPspHwCounterNames[counter]);
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    if (!psp_hw_write_all(fd, "\n")) {
        return 0;
    }

    for (frame = 0; frame < sPspHwTotals.frames; frame++) {
        const PspHwFrameRecord* record = &sPspHwFrames[frame];
        u32 length;

        length = (u32) snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                                (unsigned long) frame, (unsigned long) record->frameIntervalUs,
                                (unsigned long) record->taskUs, (unsigned long) record->presentUs,
                                (unsigned long) record->commands, (unsigned long) record->loadedVertices,
                                (unsigned long) record->submittedVertices,
                                (unsigned long) record->textureUploadBytes);
        for (counter = 0; counter < PSP_HW_COUNTER_COUNT; counter++) {
            length += (u32) snprintf(line + length, sizeof(line) - length, ",%lu",
                                     (unsigned long) record->taskCounter[counter]);
        }
        line[length++] = '\n';
        line[length] = 0;
        if (!psp_hw_write_all(fd, line)) {
            return 0;
        }
    }
    return 1;
}

static int psp_hw_dump_to_root(u32 root) {
    char path[112];
    SceUID fd;
    u32 slot;

    sPspHwErrorRoot = root;
    sPspHwErrorCode = 0;

    if (!psp_hw_find_slot(root, path, sizeof(path), &slot)) {
        sPspHwErrorStep = PSP_HW_STEP_SLOT;
        return 0;
    }

    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0) {
        sPspHwErrorStep = PSP_HW_STEP_OPEN;
        sPspHwErrorCode = fd;
        return 0;
    }

    if (!psp_hw_dump_metadata(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_META;
    } else if (!psp_hw_dump_work(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_WORK;
    } else if (!psp_hw_dump_scopes(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_SCOPES;
    } else if (!psp_hw_dump_counters(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_COUNTERS;
    } else if (!psp_hw_dump_diagnostics(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_DIAGNOSTICS;
    } else if (!psp_hw_dump_pacing(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_PACING;
    } else if (!psp_hw_dump_command_sources(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_SOURCES;
    } else if (!psp_hw_dump_water_and_effects(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_SOURCES;
    } else if (!psp_hw_dump_display_list_sources(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_SOURCES;
    } else if (!psp_hw_dump_frames(fd)) {
        sPspHwErrorStep = PSP_HW_STEP_FRAMES;
    } else {
        sPspHwErrorStep = PSP_HW_STEP_NONE;
    }
    sceIoClose(fd);

    if (sPspHwErrorStep != PSP_HW_STEP_NONE) {
        return 0;
    }
    sPspHwSavedSlot = slot;
    return 1;
}

static void psp_hw_dump(void) {
    u32 root;

    if (sPspHwTotals.frames == 0) {
        sPspHwStatus = PSP_HW_STATUS_READY;
        return;
    }

    for (root = 0; root < PSP_HW_ROOT_COUNT; root++) {
        if (psp_hw_dump_to_root(root)) {
            sPspHwStatus = PSP_HW_STATUS_SAVED;
            return;
        }
    }
    sPspHwStatus = PSP_HW_STATUS_ERROR;
}

static void psp_hw_start(void) {
    if (sPspHwCaptureActive || sPspHwDumpPending) {
        return;
    }
    memset(&sPspHwTotals, 0, sizeof(sPspHwTotals));
    memset(sPspHwCommandSourceTotals, 0, sizeof(sPspHwCommandSourceTotals));
    sPspHwCommandSourceMismatchFrames = 0;
    memset(sPspHwDlSources, 0, sizeof(sPspHwDlSources));
    memset(sPspHwWaterTotals, 0, sizeof(sPspHwWaterTotals));
    memset(sPspHwEffectTotals, 0, sizeof(sPspHwEffectTotals));
    sPspHwDlSourceUsed = 0;
    sPspHwDlSourceOverflowCalls = 0;
    sPspHwDlSourceOverflowCommands = 0;
    sPspHwDlSourceFull = 0;
    sPspHwWaterGroupsSeen = 0;
    sPspHwWaterGroupsEvaluated = 0;
    sPspHwWaterGroupsSkipped = 0;
    sPspHwWaterPairsSkipped = 0;
    sPspHwWaterCommandsSkipped = 0;
#if PSP_GFX_BACKEND_GU
    psp_hw_texture_reset();
#endif
    sPspHwLastTextureUploadBytes = 0;
    sPspHwFrameBeginValid = 0;
    sPspHwWarmupFrames = PSP_HW_COUNTER_WARMUP_FRAMES;
    sPspHwStopRequested = 0;
    sPspHwFrameArmed = 0;
    sPspHwErrorStep = PSP_HW_STEP_NONE;
    sPspHwCaptureActive = 1;
    sPspHwStatus = (sPspHwWarmupFrames != 0) ? PSP_HW_STATUS_WARMUP : PSP_HW_STATUS_RECORDING;
}

/* The render thread only requests the dump, the input thread performs it
 * That matches the phase profiler, which is the write path known to work */
static void psp_hw_request_dump(void) {
    sPspHwCaptureActive = 0;
    sPspHwFrameArmed = 0;
    sPspHwStatus = PSP_HW_STATUS_SAVING;
    sPspHwDumpPending = 1;
}

static void psp_hw_service_dump(void) {
    if (!sPspHwDumpPending) {
        return;
    }
    sPspHwDumpPending = 0;
    psp_hw_dump();
}

void PspHwCounterProfile_Init(void) {
    memset(&sPspHwTotals, 0, sizeof(sPspHwTotals));
#if PSP_GFX_BACKEND_GU
    psp_hw_texture_reset();
#endif
    sPspHwCaptureActive = 0;
    sPspHwStopRequested = 0;
    sPspHwFrameArmed = 0;
    sPspHwScene = 0;
    sPspHwPreviousButtons = 0;
    sPspHwRenderThreadBound = 0;
    sPspHwDumpPending = 0;
    sPspHwErrorStep = PSP_HW_STEP_NONE;
    sPspHwErrorCode = 0;
    sPspHwStatus = PSP_HW_STATUS_READY;
    sPspHwFrameBeginValid = 0;
    psp_hw_bind_counters();
}

void PspHwCounterProfile_Shutdown(void) {
    if (sPspHwCaptureActive) {
        psp_hw_request_dump();
    }
    psp_hw_service_dump();
}

int PspHwCounterProfile_PollControls(u32 rawButtons) {
    const u32 previousSceneCombo = PSP_CTRL_SELECT | PSP_CTRL_LEFT;
    const u32 nextSceneCombo = PSP_CTRL_SELECT | PSP_CTRL_RIGHT;
    const u32 startCombo = PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER;
    const u32 stopCombo = PSP_CTRL_SELECT | PSP_CTRL_RTRIGGER;
    int consumed = 0;

    psp_hw_service_dump();

    if (!sPspHwCaptureActive && ((rawButtons & previousSceneCombo) == previousSceneCombo) &&
        ((sPspHwPreviousButtons & previousSceneCombo) != previousSceneCombo)) {
        sPspHwScene = (sPspHwScene + PSP_HW_SCENE_COUNT - 1) % PSP_HW_SCENE_COUNT;
        sPspHwStatus = PSP_HW_STATUS_READY;
    } else if (!sPspHwCaptureActive && ((rawButtons & nextSceneCombo) == nextSceneCombo) &&
               ((sPspHwPreviousButtons & nextSceneCombo) != nextSceneCombo)) {
        sPspHwScene = (sPspHwScene + 1) % PSP_HW_SCENE_COUNT;
        sPspHwStatus = PSP_HW_STATUS_READY;
    } else if (((rawButtons & startCombo) == startCombo) &&
               ((sPspHwPreviousButtons & startCombo) != startCombo)) {
        psp_hw_start();
    } else if (((rawButtons & stopCombo) == stopCombo) &&
               ((sPspHwPreviousButtons & stopCombo) != stopCombo)) {
        if (sPspHwCaptureActive) {
            sPspHwStopRequested = 1;
        }
    }

    if (((rawButtons & previousSceneCombo) == previousSceneCombo) ||
        ((rawButtons & nextSceneCombo) == nextSceneCombo) ||
        ((rawButtons & startCombo) == startCombo) || ((rawButtons & stopCombo) == stopCombo)) {
        consumed = 1;
    }

    sPspHwPreviousButtons = rawButtons;
    return consumed;
}

void PspHwCounterProfile_FrameBegin(void) {
    u64 now = (u64) sceKernelGetSystemTimeWide();

    /* Init binds on the main thread, rebind once here so the status reports this one */
    if (!sPspHwRenderThreadBound) {
        sPspHwRenderThreadBound = 1;
        psp_hw_bind_counters();
    }

    if (!sPspHwCaptureActive) {
        sPspHwFrameArmed = 0;
        return;
    }

    if (sPspHwWarmupFrames != 0) {
        sPspHwWarmupFrames--;
        sPspHwLastFrameBeginUs = now;
        sPspHwFrameBeginValid = 1;
        sPspHwFrameArmed = 0;
        if (sPspHwWarmupFrames == 0) {
            sPspHwStatus = PSP_HW_STATUS_RECORDING;
        }
        return;
    }

    /* Every scope runs on this thread so the pointer only needs refreshing per frame */
    psp_hw_bind_counters();
    sPspHwFrameArmed = 1;
#if PSP_GFX_BACKEND_GU
    memset(sPspHwTextureFrame, 0, sizeof(sPspHwTextureFrame));
#endif
    sPspHwCurrentFrameIntervalValid = sPspHwFrameBeginValid;
    if (sPspHwCurrentFrameIntervalValid) {
        sPspHwCurrentFrameIntervalUs = (u32) (now - sPspHwLastFrameBeginUs);
    }
    sPspHwLastFrameBeginUs = now;
    sPspHwFrameBeginValid = 1;
    sPspHwCurrentTaskValid = 0;
    sPspHwCurrentPresentValid = 0;
    memset(sPspHwCurrentTaskCounters, 0, sizeof(sPspHwCurrentTaskCounters));
    memset(sPspHwCurrentCommandSources, 0, sizeof(sPspHwCurrentCommandSources));
    memset(sPspHwCurrentWaterUs, 0, sizeof(sPspHwCurrentWaterUs));
    memset(sPspHwCurrentWaterInput, 0, sizeof(sPspHwCurrentWaterInput));
    memset(sPspHwCurrentWaterEmitted, 0, sizeof(sPspHwCurrentWaterEmitted));
    memset(sPspHwCurrentEffectUs, 0, sizeof(sPspHwCurrentEffectUs));
    sPspHwCurrentWaterGroupsSkipped = 0;
    sPspHwCurrentWaterPairsSkipped = 0;
    sPspHwCurrentWaterCommandsSkipped = 0;
}

void PspHwCounterProfile_FrameEnd(u32 commands, u32 loadedVertices, u32 submittedVertices) {
    PspHwFrameRecord* record;
    u64 textureUploadBytes = 0;
    u32 textureClass;
    u32 source;
    u32 sourceCommands = 0;

    if (!sPspHwFrameArmed) {
        if (sPspHwCaptureActive && sPspHwStopRequested && (sPspHwWarmupFrames != 0)) {
            /* Aborted during warmup, nothing recorded */
            sPspHwCaptureActive = 0;
            sPspHwStopRequested = 0;
            sPspHwStatus = PSP_HW_STATUS_READY;
        }
        return;
    }

    sPspHwFrameArmed = 0;
    if (sPspHwCurrentFrameIntervalValid) {
        psp_hw_record_pacing(PSP_HW_PACING_FRAME_INTERVAL, sPspHwCurrentFrameIntervalUs);
    }
    if (sPspHwCurrentTaskValid) {
        psp_hw_record_pacing(PSP_HW_PACING_TASK, sPspHwCurrentTaskUs);
    }
    if (sPspHwCurrentPresentValid) {
        psp_hw_record_pacing(PSP_HW_PACING_PRESENT, sPspHwCurrentPresentUs);
    }
    if (sPspHwCurrentTaskValid && sPspHwCurrentPresentValid) {
        psp_hw_record_pacing(PSP_HW_PACING_TASK_AND_PRESENT,
                             sPspHwCurrentTaskUs + sPspHwCurrentPresentUs);
    }
    record = &sPspHwFrames[sPspHwTotals.frames];
    record->frameIntervalUs = sPspHwCurrentFrameIntervalValid ? sPspHwCurrentFrameIntervalUs : 0;
    record->taskUs = sPspHwCurrentTaskValid ? sPspHwCurrentTaskUs : 0;
    record->presentUs = sPspHwCurrentPresentValid ? sPspHwCurrentPresentUs : 0;
    record->commands = commands;
#if PSP_GFX_BACKEND_GU
    memcpy(record->texture, sPspHwTextureFrame, sizeof(record->texture));
#endif
    record->loadedVertices = loadedVertices;
    record->submittedVertices = submittedVertices;
    memcpy(record->taskCounter, sPspHwCurrentTaskCounters, sizeof(record->taskCounter));
    for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
        record->commandSources[source] = sPspHwCurrentCommandSources[source].commands;
        record->commandSourceUs[source] = sPspHwCurrentCommandSources[source].frontendUs;
        sourceCommands += record->commandSources[source];
    }
    memcpy(record->waterUs, sPspHwCurrentWaterUs, sizeof(record->waterUs));
    memcpy(record->waterInputTriangles, sPspHwCurrentWaterInput, sizeof(record->waterInputTriangles));
    memcpy(record->waterEmittedTriangles, sPspHwCurrentWaterEmitted, sizeof(record->waterEmittedTriangles));
    record->topEffectId = PSP_EFFECT_TYPE_END;
    record->topEffectUs = 0;
    record->secondEffectId = PSP_EFFECT_TYPE_END;
    record->secondEffectUs = 0;
    record->effectTotalUs = 0;
    record->waterGroupsSkipped = sPspHwCurrentWaterGroupsSkipped;
    record->waterPairsSkipped = sPspHwCurrentWaterPairsSkipped;
    record->waterCommandsSkipped = sPspHwCurrentWaterCommandsSkipped;
    for (source = 0; source < PSP_HW_EFFECT_TYPE_COUNT; source++) {
        u32 effectUs = sPspHwCurrentEffectUs[source];

        record->effectTotalUs += effectUs;
        if (effectUs > record->topEffectUs) {
            record->secondEffectUs = record->topEffectUs;
            record->secondEffectId = record->topEffectId;
            record->topEffectUs = effectUs;
            record->topEffectId = source;
        } else if (effectUs > record->secondEffectUs) {
            record->secondEffectUs = effectUs;
            record->secondEffectId = source;
        }
        if (effectUs > sPspHwEffectTotals[source].maxFrameUs) {
            sPspHwEffectTotals[source].maxFrameUs = effectUs;
        }
    }
    if (sourceCommands != commands) {
        sPspHwCommandSourceMismatchFrames++;
    }
    for (textureClass = 0; textureClass < PSP_HW_TEXTURE_CACHE_COUNT; textureClass++) {
        textureUploadBytes += sPspHwTotals.textureCache[textureClass].uploadBytes;
    }
    record->textureUploadBytes = (u32) (textureUploadBytes - sPspHwLastTextureUploadBytes);
    sPspHwLastTextureUploadBytes = textureUploadBytes;

    sPspHwTotals.commands += commands;
    sPspHwTotals.loadedVertices += loadedVertices;
    sPspHwTotals.submittedVertices += submittedVertices;
    sPspHwTotals.frames++;

    if ((sPspHwTotals.frames >= PSP_HW_COUNTER_CAPTURE_FRAMES) || sPspHwStopRequested) {
        psp_hw_request_dump();
    }
}

void PspHwCounterProfile_ScopeBegin(PspHwCounterScope scope) {
    if (!sPspHwFrameArmed) {
        return;
    }
    psp_hw_sample(&sPspHwScopeStart[scope]);
}

void PspHwCounterProfile_ScopeEnd(PspHwCounterScope scope) {
    PspHwCounterSample end;
    PspHwCounterScopeTotals* totals;
    const PspHwCounterSample* start;
    u32 i;

    if (!sPspHwFrameArmed) {
        return;
    }

    psp_hw_sample(&end);
    start = &sPspHwScopeStart[scope];
    totals = &sPspHwTotals.scope[scope];
    end.timeUs -= start->timeUs;
    totals->elapsedUs += end.timeUs;
    totals->samples++;

    if (scope == PSP_HW_SCOPE_TASK) {
        sPspHwCurrentTaskUs = (u32) end.timeUs;
        sPspHwCurrentTaskValid = 1;
    } else if (scope == PSP_HW_SCOPE_PRESENT) {
        sPspHwCurrentPresentUs = (u32) end.timeUs;
        sPspHwCurrentPresentValid = 1;
    }

    if (sPspHwRegs == NULL) {
        return;
    }
    for (i = 0; i < PSP_HW_COUNTER_COUNT; i++) {
        u32 delta = (u32) (end.counter[i] - start->counter[i]);

        totals->counter[i] += delta;
        if (scope == PSP_HW_SCOPE_TASK) {
            sPspHwCurrentTaskCounters[i] = delta;
        }
    }
}

void PspHwCounterProfile_CountTextureCacheLookup(PspHwTextureCacheClass cache, int hit) {
    if (!sPspHwFrameArmed || (cache >= PSP_HW_TEXTURE_CACHE_COUNT)) {
        return;
    }
    sPspHwTotals.textureCache[cache].lookups++;
    if (hit) {
        sPspHwTotals.textureCache[cache].hits++;
    } else {
        sPspHwTotals.textureCache[cache].misses++;
    }
}

void PspHwCounterProfile_CountTextureUpload(PspHwTextureCacheClass cache, u32 bytes) {
    if (!sPspHwFrameArmed || (cache >= PSP_HW_TEXTURE_CACHE_COUNT)) {
        return;
    }
    sPspHwTotals.textureCache[cache].uploads++;
    sPspHwTotals.textureCache[cache].uploadBytes += bytes;
}

void PspHwCounterProfile_CountTextureCacheEviction(PspHwTextureCacheClass cache) {
    if (!sPspHwFrameArmed || (cache >= PSP_HW_TEXTURE_CACHE_COUNT)) {
        return;
    }
    sPspHwTotals.textureCache[cache].evictions++;
}

void PspHwCounterProfile_CountBatchFlush(u32 reason, u32 vertices) {
    if (!sPspHwFrameArmed || (reason >= PSP_HW_FLUSH_REASON_COUNT)) {
        return;
    }
    sPspHwTotals.batchFlushes[reason]++;
    sPspHwTotals.batchFlushVertices[reason] += vertices;
}

void PspHwCounterProfile_CountTextureBarrier(u32 source) {
    if (!sPspHwFrameArmed || (source >= PSP_HW_TEXTURE_BARRIER_SOURCE_COUNT)) {
        return;
    }
    sPspHwTotals.textureBarriers[source]++;
}

void PspHwCounterProfile_CountPoolEvent(PspHwPoolEvent event) {
    if (!sPspHwFrameArmed || (event >= PSP_HW_POOL_EVENT_COUNT)) {
        return;
    }
    sPspHwTotals.poolEvents[event]++;
}

void PspHwCounterProfile_RecordCommandSources(const PspCommandSourceStats* stats) {
    u32 source;

    if (!sPspHwFrameArmed) {
        return;
    }
    for (source = 0; source < PSP_COMMAND_SOURCE_COUNT; source++) {
        sPspHwCurrentCommandSources[source] = stats[source];
        sPspHwCommandSourceTotals[source].commands += stats[source].commands;
        sPspHwCommandSourceTotals[source].vertexCommands += stats[source].vertexCommands;
        sPspHwCommandSourceTotals[source].triangleCommands += stats[source].triangleCommands;
        sPspHwCommandSourceTotals[source].displayListCalls += stats[source].displayListCalls;
        sPspHwCommandSourceTotals[source].frontendUs += stats[source].frontendUs;
    }
}

void PspHwCounterProfile_RecordWaterTile(u32 tile, u32 elapsedUs, u32 commands, u32 loadedVertices,
                                         u32 inputTriangles, u32 emittedTriangles) {
    if (!sPspHwFrameArmed || tile >= PSP_WATER_TILE_COUNT) {
        return;
    }
    sPspHwWaterTotals[tile].elapsedUs += elapsedUs;
    sPspHwWaterTotals[tile].commands += commands;
    sPspHwWaterTotals[tile].loadedVertices += loadedVertices;
    sPspHwWaterTotals[tile].inputTriangles += inputTriangles;
    sPspHwWaterTotals[tile].emittedTriangles += emittedTriangles;
    sPspHwWaterTotals[tile].calls++;
    sPspHwWaterTotals[tile].zeroOutputCalls += emittedTriangles == 0;
    sPspHwCurrentWaterUs[tile] += elapsedUs;
    sPspHwCurrentWaterInput[tile] += inputTriangles;
    sPspHwCurrentWaterEmitted[tile] += emittedTriangles;
}

void PspHwCounterProfile_RecordEffectType(u32 id, u32 elapsedUs, u32 commands) {
    if (!sPspHwFrameArmed || id >= PSP_HW_EFFECT_TYPE_COUNT) {
        return;
    }
    sPspHwEffectTotals[id].elapsedUs += elapsedUs;
    sPspHwEffectTotals[id].commands += commands;
    sPspHwEffectTotals[id].calls++;
    sPspHwCurrentEffectUs[id] += elapsedUs;
}

void PspHwCounterProfile_CountWaterGroupSeen(void) {
    if (sPspHwFrameArmed) {
        sPspHwWaterGroupsSeen++;
    }
}

void PspHwCounterProfile_CountWaterGroupEvaluated(void) {
    if (sPspHwFrameArmed) {
        sPspHwWaterGroupsEvaluated++;
    }
}

void PspHwCounterProfile_CountWaterGroupSkipped(u32 pairs, u32 commands) {
    if (!sPspHwFrameArmed) {
        return;
    }
    sPspHwWaterGroupsSkipped++;
    sPspHwWaterPairsSkipped += pairs;
    sPspHwWaterCommandsSkipped += commands;
    sPspHwCurrentWaterGroupsSkipped++;
    sPspHwCurrentWaterPairsSkipped += pairs;
    sPspHwCurrentWaterCommandsSkipped += commands;
}

int PspHwCounterProfile_IsCapturingTask(void) {
    return sPspHwFrameArmed;
}

void PspHwCounterProfile_RecordDisplayList(u32 source, u32 rawTarget, const void* resolvedTarget, u32 commands) {
    u32 resolved = (u32) (unsigned long) resolvedTarget;
    u32 slot;
    u32 probe;

    if (!sPspHwFrameArmed || (source >= PSP_COMMAND_SOURCE_COUNT)) {
        return;
    }
    if (sPspHwDlSourceFull) {
        sPspHwDlSourceOverflowCalls++;
        sPspHwDlSourceOverflowCommands += commands;
        return;
    }
    slot = (rawTarget * 2654435761U ^ resolved * 2246822519U ^ source * 3266489917U) &
           (PSP_HW_DL_SOURCE_SLOTS - 1U);
    for (probe = 0; probe < PSP_HW_DL_SOURCE_SLOTS; probe++) {
        if (sPspHwDlSources[slot].calls == 0) {
            sPspHwDlSources[slot].source = source;
            sPspHwDlSources[slot].rawTarget = rawTarget;
            sPspHwDlSources[slot].resolvedTarget = resolved;
            sPspHwDlSourceUsed++;
        } else if ((sPspHwDlSources[slot].source != source) ||
                   (sPspHwDlSources[slot].rawTarget != rawTarget) ||
                   (sPspHwDlSources[slot].resolvedTarget != resolved)) {
            slot = (slot + 1U) & (PSP_HW_DL_SOURCE_SLOTS - 1U);
            continue;
        }
        sPspHwDlSources[slot].calls++;
        sPspHwDlSources[slot].inclusiveCommands += commands;
        return;
    }
    sPspHwDlSourceFull = 1;
    sPspHwDlSourceOverflowCalls++;
    sPspHwDlSourceOverflowCommands += commands;
}

void PspHwCounterProfile_DrawStatus(void) {
    const char* scene = sPspHwSceneNames[sPspHwScene];
    const char* source = (sPspHwSource == PSP_HW_SOURCE_NONE) ? "TIME" : "CTRS";

    pspDebugScreenSetXY(0, 2);
    if (sPspHwStatus == PSP_HW_STATUS_WARMUP) {
        pspDebugScreenPrintf("HW %-8s %s WARM %03lu    ", scene, source,
                             (unsigned long) sPspHwWarmupFrames);
    } else if (sPspHwStatus == PSP_HW_STATUS_RECORDING) {
        pspDebugScreenPrintf("HW %-8s %s REC %03lu/%03u", scene, source,
                             (unsigned long) sPspHwTotals.frames, PSP_HW_COUNTER_CAPTURE_FRAMES);
    } else if (sPspHwStatus == PSP_HW_STATUS_SAVING) {
        pspDebugScreenPrintf("HW %-8s %s SAVING     ", scene, source);
    } else if (sPspHwStatus == PSP_HW_STATUS_SAVED) {
        pspDebugScreenPrintf("HW %-8s %s SAVED %03lu %c ", scene, source,
                             (unsigned long) sPspHwSavedSlot, sPspHwRootLetters[sPspHwErrorRoot]);
    } else if (sPspHwStatus == PSP_HW_STATUS_ERROR) {
        pspDebugScreenPrintf("HW %-8s %s ERR %s %c %08X", scene, source, sPspHwStepNames[sPspHwErrorStep],
                             sPspHwRootLetters[sPspHwErrorRoot], (unsigned) sPspHwErrorCode);
    } else {
        pspDebugScreenPrintf("HW %-8s %s READY      ", scene, source);
    }
}

#else

typedef int PspHwCounterProfileTranslationUnitNotEmpty;

#endif
