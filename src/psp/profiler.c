#include "src/psp/profiler.h"

#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <pspdebug.h>
#include <psppower.h>
#include <stdio.h>

#if SF64_PSP_GPROF
#include <pspprof.h>
#endif

#ifndef SF64_PSP_GPROF
#define SF64_PSP_GPROF 0
#endif
#ifndef SF64_PSP_PROFILE_PHASES
#define SF64_PSP_PROFILE_PHASES 0
#endif
#ifndef SF64_GIT_SHA
#define SF64_GIT_SHA "unknown"
#endif
#ifndef N64PSP_GIT_SHA
#define N64PSP_GIT_SHA "unknown"
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

#define PSP_PROFILE_DIR "ms0:/PSP/GAME/SF64PROFILE"
#define PSP_PROFILE_MAX_SLOT 999
#define PSP_PROFILE_MAX_THREADS 8
#define PSP_PROFILE_TIMER_SAMPLES 128

#define PSP_PROFILE_ATTR __attribute__((no_instrument_function, no_profile_instrument_function))

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
    u64 batchFlushes;
    u64 flushReasons[PSP_PROFILE_FLUSH_COUNT];
    u64 verticesSubmitted;
    u64 drawCalls;
    u64 glFlushCalls;
    u64 syncCalls;
    u64 textureCacheHits;
    u64 textureCacheMisses;
    u64 textureDecodes;
    u64 textureUploads;
    u64 textureBytesUploaded;
} PspProfileCounters;
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
#endif

#if SF64_PSP_PROFILE_PHASES
PSP_PROFILE_ATTR static u32 psp_profiler_strlen(const char* text) {
    u32 len = 0;

    while ((text != NULL) && (text[len] != '\0')) {
        len++;
    }
    return len;
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
    u8* bytes;
    u32 i;

    bytes = (u8*) sPhase;
    for (i = 0; i < sizeof(sPhase); i++) {
        bytes[i] = 0;
    }
    bytes = (u8*) sThreadPhase;
    for (i = 0; i < sizeof(sThreadPhase); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < PSP_PROFILE_MAX_THREADS; i++) {
        sThreadPhase[i].threadId = -1;
    }
    bytes = (u8*) &sCounters;
    for (i = 0; i < sizeof(sCounters); i++) {
        bytes[i] = 0;
    }
    sCaptureFrames = 0;
    sForcedActivePhaseEnds = 0;
    sTimerReadPairOverheadUs = psp_profiler_measure_timer_overhead();
    sCaptureStartUs = psp_profiler_now_us();
    sCaptureEndUs = sCaptureStartUs;
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
        "PSPGL draw submission",
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

static void psp_profiler_write_phase_files(u32 slot) {
    char path[96];
    char line[256];
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
             "SF64 git SHA: %s\nn64psp submodule SHA: %s\nPerfect Dark reference SHA: %s\ncompiler: %s\noptimisation flags: %s\nPROFILE_PSP: %d\nSF64_PSP_PROFILE_PHASES: %d\nCPU clock: %lu\nbus clock: %lu\ncapture slot: %lu\nrequested frame count: %d\nactual frame count: %lu\ntimer overhead us: %llu\n\n",
             SF64_GIT_SHA, N64PSP_GIT_SHA, PERFECT_DARK_PSP_SHA, SF64_PSP_COMPILER, SF64_PSP_OPT_FLAGS,
             SF64_PSP_GPROF, SF64_PSP_PROFILE_PHASES, (unsigned long) scePowerGetCpuClockFrequency(),
             (unsigned long) scePowerGetBusClockFrequency(), (unsigned long) slot, SF64_PSP_PROFILE_CAPTURE_FRAMES,
             (unsigned long) sCaptureFrames, sTimerReadPairOverheadUs);
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
    snprintf(line, sizeof(line),
             "\n[texture statistics]\ncache_hits,%llu\ncache_misses,%llu\ndecodes_or_conversions,%llu\nuploads,%llu\nbytes_uploaded,%llu\n",
             sCounters.textureCacheHits, sCounters.textureCacheMisses, sCounters.textureDecodes,
             sCounters.textureUploads, sCounters.textureBytesUploaded);
    psp_profiler_write_all(fd, line);
    snprintf(line, sizeof(line),
             "\n[clipping statistics]\ninput_triangles,%llu\ntrivially_accepted,%llu\ntrivially_rejected,%llu\npartially_clipped,%llu\ngenerated_vertices,%llu\noutput_triangles,%llu\n",
             sCounters.inputTriangles, sCounters.triviallyAcceptedTriangles, sCounters.triviallyRejectedTriangles,
             sCounters.partiallyClippedTriangles, sCounters.generatedClippingVertices, sCounters.outputTriangles);
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
    sceIoClose(fd);
    psp_profiler_set_status(PSP_PROF_STATUS_SAVED, slot);
}
#endif

void PspProfiler_Init(void) {
    if (sInitialized) {
        return;
    }
    sInitialized = 1;
    sStatus = PSP_PROF_STATUS_OFF;
    sStatusSlot = 0;
    sNextSlot = 0;
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

    if (sCaptureActive) {
        PspProfiler_StopCapture();
        PspProfiler_DumpCapture();
    }
    sCaptureStarted = 0;
    sCaptureDumped = 0;
#if SF64_PSP_GPROF
    sCapturePath[0] = '\0';
    if (!psp_profiler_open_unique("gmon", "out", sCapturePath, sizeof(sCapturePath), &slot)) {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, sStatusSlot);
        return;
    }
    gprof_stop(NULL, 0);
    gprof_start();
#endif
#if SF64_PSP_PROFILE_PHASES
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
#endif
#if !SF64_PSP_GPROF && !SF64_PSP_PROFILE_PHASES
    (void) slot;
#endif
    sCaptureStarted = 1;
    sCaptureActive = 1;
    psp_profiler_set_status(PSP_PROF_STATUS_REC, slot);
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
#if SF64_PSP_GPROF
    gprof_stop(NULL, 0);
#endif
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
#endif
    sCaptureActive = 0;
}

void PspProfiler_DumpCapture(void) {
    if (!sCaptureStarted || sCaptureDumped) {
        return;
    }
#if SF64_PSP_GPROF
    if (sCapturePath[0] == '\0') {
        psp_profiler_set_status(PSP_PROF_STATUS_ERROR, sStatusSlot);
        return;
    }
    gprof_stop(sCapturePath, 1);
    psp_profiler_set_status(PSP_PROF_STATUS_SAVED, sStatusSlot);
#endif
#if SF64_PSP_PROFILE_PHASES
    psp_profiler_mkdir();
    psp_profiler_write_phase_files(sStatusSlot);
#endif
    sCaptureDumped = 1;
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
    psp_profiler_unlock(lockState);
}

void PspProfiler_OnGfxTaskComplete(void) {
    int lockState;
    u32 frames;

    if (!sCaptureActive) {
        return;
    }
    lockState = psp_profiler_lock();
    sCaptureFrames++;
    frames = sCaptureFrames;
    psp_profiler_unlock(lockState);
    if (frames >= SF64_PSP_PROFILE_CAPTURE_FRAMES) {
        PspProfiler_StopCapture();
        PspProfiler_DumpCapture();
    }
}

void PspProfiler_CountDisplayListTask(void) {
    if (sCaptureActive) {
        sCounters.displayListTasks++;
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
}

void PspProfiler_CountTriangleCommand(u32 triCount, u32 tri1, u32 tri2) {
    if (!sCaptureActive) {
        return;
    }
    sCounters.inputTriangles += triCount;
    sCounters.tri1Commands += tri1;
    sCounters.tri2Commands += tri2;
    sPhase[PSP_PROFILE_PHASE_TRIANGLE].items += triCount;
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
}

void PspProfiler_CountDrawCall(u32 vertices) {
    if (sCaptureActive) {
        sCounters.drawCalls++;
        sPhase[PSP_PROFILE_PHASE_PSPGL_SUBMIT].items += vertices;
    }
}

void PspProfiler_CountGlFlush(void) {
    if (sCaptureActive) {
        sCounters.glFlushCalls++;
    }
}

void PspProfiler_CountSync(void) {
    if (sCaptureActive) {
        sCounters.syncCalls++;
    }
}
#endif
