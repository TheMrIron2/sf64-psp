#include "src/psp/hw_counter_profile.h"

#if PROFILE_HW_COUNTERS

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <psppower.h>
#include <pspthreadman.h>

#include <stdio.h>
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
#ifndef BATCH_STATE_CACHE
#define BATCH_STATE_CACHE 1
#endif
#ifndef VTX_FUSED_TNL
#define VTX_FUSED_TNL 1
#endif
#ifndef PSP_FLOAT_MTX
#define PSP_FLOAT_MTX 1
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
    PSP_HW_STEP_COUNTERS
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
    PspHwCounterScopeTotals scope[PSP_HW_SCOPE_COUNT];
    u64 commands;
    u64 loadedVertices;
    u64 submittedVertices;
    u32 frames;
} PspHwCounterTotals;

static const char* sPspHwCounterNames[PSP_HW_COUNTER_COUNT] = {
    "systemck",   "cpuck",         "internal_stall", "memory_stall", "copz_stall",
    "vfpu_stall", "sleep",         "bus_access",     "uncached_load", "uncached_store",
    "cached_load", "cached_store", "i_miss",         "d_miss",        "d_writeback",
    "cop0_inst",  "fpu_inst",      "vfpu_inst",      "local_bus"
};

static const char* sPspHwScopeNames[PSP_HW_SCOPE_COUNT] = {
    "task", "frontend", "flush", "present", "texture", "vertex", "submit"
};

/* Scene tags for the audit standard workloads plus a free slot */
static const char* sPspHwSceneNames[] = { "title", "corneria", "light", "other" };
#define PSP_HW_SCENE_COUNT ((u32) (sizeof(sPspHwSceneNames) / sizeof(sPspHwSceneNames[0])))

/* ms0 is absent on a PSP Go without an M2 card, ef0 is its internal flash
 * host0 is the PSPLINK host directory and catches both refusing writes */
static const char* sPspHwRoots[PSP_HW_ROOT_COUNT] = { PSP_HW_PROFILE_DIR, PSP_HW_PROFILE_DIR_EF0, "host0:" };
static const char sPspHwRootLetters[PSP_HW_ROOT_COUNT] = { 'M', 'E', 'H' };
static const char* sPspHwStepNames[] = { "OK", "SLOT", "OPEN", "META", "WORK", "SCOPE", "CTRS" };

static PspHwCounterTotals sPspHwTotals;
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

static u64 psp_hw_ratio(u64 value, u64 denominator) {
    if (denominator == 0) {
        return 0;
    }
    return (value * PSP_HW_RATIO_SCALE) / denominator;
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
    char line[320];

    snprintf(line, sizeof(line),
             "key,value\n"
             "scene,%s\n"
             "counter_source,%s\n"
             "frames,%lu\n"
             "warmup_frames,%u\n",
             sPspHwSceneNames[sPspHwScene], psp_hw_source_name(),
             (unsigned long) sPspHwTotals.frames, PROFILE_HW_COUNTER_WARMUP_FRAMES);
    if (!psp_hw_write_all(fd, line)) {
        return 0;
    }

    snprintf(line, sizeof(line),
             "sf64_commit,%s\n"
             "sf64_tree,%s\n"
             "n64psp_commit,%s\n"
             "n64psp_tree,%s\n",
             SF64_GIT_SHA, SF64_GIT_DIRTY, N64PSP_GIT_SHA, N64PSP_GIT_DIRTY);
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
             "PSP_LOG=%d PSP_AUDIO=%d VTX_FUSED_TNL=%d PSP_FLOAT_MTX=%d BATCH_STATE_CACHE=%d\n",
             PROFILE_HW_COUNTER_SCOPES, PROFILE_PHASES, PROFILE_COMPONENTS, PROFILE_GPROF,
             PSP_FPS_OVERLAY, PSP_RENDERER_DIAGNOSTICS, PSP_LOG_ENABLED, PSP_AUDIO, VTX_FUSED_TNL,
             PSP_FLOAT_MTX, BATCH_STATE_CACHE);
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

    if (!psp_hw_write_all(fd, "\n[scopes]\nscope,samples,elapsed_us,us_per_frame_x1000\n")) {
        return 0;
    }

    for (i = 0; i < PSP_HW_SCOPE_COUNT; i++) {
        const PspHwCounterScopeTotals* totals = &sPspHwTotals.scope[i];

        if (totals->samples == 0) {
            continue;
        }
        snprintf(line, sizeof(line), "%s,%lu,%llu,%llu\n", sPspHwScopeNames[i],
                 (unsigned long) totals->samples, (unsigned long long) totals->elapsedUs,
                 (unsigned long long) psp_hw_ratio(totals->elapsedUs, sPspHwTotals.frames));
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

    if (!psp_hw_write_all(fd, "\n[counters]\nscope,counter,total,per_frame_x1000,per_command_x1000,"
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

            snprintf(line, sizeof(line), "%s,%s,%llu,%llu,%llu,%llu,%llu\n", sPspHwScopeNames[scope],
                     sPspHwCounterNames[i], (unsigned long long) total,
                     (unsigned long long) psp_hw_ratio(total, sPspHwTotals.frames),
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
    sPspHwWarmupFrames = PROFILE_HW_COUNTER_WARMUP_FRAMES;
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
        sPspHwFrameArmed = 0;
        if (sPspHwWarmupFrames == 0) {
            sPspHwStatus = PSP_HW_STATUS_RECORDING;
        }
        return;
    }

    /* Every scope runs on this thread so the pointer only needs refreshing per frame */
    psp_hw_bind_counters();
    sPspHwFrameArmed = 1;
}

void PspHwCounterProfile_FrameEnd(u32 commands, u32 loadedVertices, u32 submittedVertices) {
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
    sPspHwTotals.commands += commands;
    sPspHwTotals.loadedVertices += loadedVertices;
    sPspHwTotals.submittedVertices += submittedVertices;
    sPspHwTotals.frames++;

    if ((sPspHwTotals.frames >= PROFILE_HW_COUNTER_FRAMES) || sPspHwStopRequested) {
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
    totals->elapsedUs += end.timeUs - start->timeUs;
    totals->samples++;

    if (sPspHwRegs == NULL) {
        return;
    }
    for (i = 0; i < PSP_HW_COUNTER_COUNT; i++) {
        totals->counter[i] += (u32) (end.counter[i] - start->counter[i]);
    }
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
                             (unsigned long) sPspHwTotals.frames, PROFILE_HW_COUNTER_FRAMES);
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
