#include "src/psp/hw_counter_profile.h"

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspthreadman.h>

#include <stdio.h>
#include <string.h>

#ifndef SF64_GIT_SHA
#define SF64_GIT_SHA "unknown"
#endif

#ifndef N64PSP_GIT_SHA
#define N64PSP_GIT_SHA "unknown"
#endif

#define PSP_HW_PROFILE_DIR "ms0:/PSP/GAME/SF64PROFILE"
#define PSP_HW_PROFILE_FRAMES 300
#define PSP_HW_PROFILE_MAX_SLOT 999
#define PSP_HW_COUNTER_COUNT 19
#define PSP_HW_RATIO_SCALE 1000

typedef enum {
    PSP_HW_STATUS_READY,
    PSP_HW_STATUS_RECORDING,
    PSP_HW_STATUS_SAVED,
    PSP_HW_STATUS_UNAVAILABLE,
    PSP_HW_STATUS_ERROR
} PspHwCounterStatus;

typedef struct {
    PspDebugProfilerRegs regs;
} PspHwCounterSnapshot;

typedef struct {
    u64 counter[PSP_HW_COUNTER_COUNT];
    u64 commands;
    u64 loadedVertices;
    u64 submittedVertices;
    u32 frames;
} PspHwCounterTotals;

static const char* sPspHwCounterNames[PSP_HW_COUNTER_COUNT] = {
    "systemck",
    "cpuck",
    "internal_stall",
    "memory_stall",
    "copz_stall",
    "vfpu_stall",
    "sleep",
    "bus_access",
    "uncached_load",
    "uncached_store",
    "cached_load",
    "cached_store",
    "i_miss",
    "d_miss",
    "d_writeback",
    "cop0_inst",
    "fpu_inst",
    "vfpu_inst",
    "local_bus"
};

static PspHwCounterTotals sPspHwTotals;
static volatile int sPspHwCaptureActive;
static volatile int sPspHwStopRequested;
static int sPspHwUseProjectedOutput = 1;
static int sPspHwCaptureProjectedOutput = 1;
static u32 sPspHwPreviousButtons;
static u32 sPspHwSavedSlot;
static PspHwCounterStatus sPspHwStatus = PSP_HW_STATUS_READY;
static PspHwCounterSnapshot sPspHwFrameStart;

static u64 psp_hw_ratio(u64 value, u64 denominator) {
    if (denominator == 0) {
        return 0;
    }
    return (value * PSP_HW_RATIO_SCALE) / denominator;
}

static int psp_hw_write_all(SceUID fd, const char* text) {
    u32 length = (u32) strlen(text);

    return sceIoWrite(fd, text, length) == (int) length;
}

static int psp_hw_snapshot(PspHwCounterSnapshot* snapshot) {
    const volatile PspDebugProfilerRegs* regs = sceKernelReferThreadProfiler();

    if (regs == NULL) {
        return 0;
    }

    snapshot->regs.systemck = regs->systemck;
    snapshot->regs.cpuck = regs->cpuck;
    snapshot->regs.internal = regs->internal;
    snapshot->regs.memory = regs->memory;
    snapshot->regs.copz = regs->copz;
    snapshot->regs.vfpu = regs->vfpu;
    snapshot->regs.sleep = regs->sleep;
    snapshot->regs.bus_access = regs->bus_access;
    snapshot->regs.uncached_load = regs->uncached_load;
    snapshot->regs.uncached_store = regs->uncached_store;
    snapshot->regs.cached_load = regs->cached_load;
    snapshot->regs.cached_store = regs->cached_store;
    snapshot->regs.i_miss = regs->i_miss;
    snapshot->regs.d_miss = regs->d_miss;
    snapshot->regs.d_writeback = regs->d_writeback;
    snapshot->regs.cop0_inst = regs->cop0_inst;
    snapshot->regs.fpu_inst = regs->fpu_inst;
    snapshot->regs.vfpu_inst = regs->vfpu_inst;
    snapshot->regs.local_bus = regs->local_bus;
    return 1;
}

static void psp_hw_accumulate_delta(const PspDebugProfilerRegs* start,
                                    const PspDebugProfilerRegs* end) {
    sPspHwTotals.counter[0] += (u32) (end->systemck - start->systemck);
    sPspHwTotals.counter[1] += (u32) (end->cpuck - start->cpuck);
    sPspHwTotals.counter[2] += (u32) (end->internal - start->internal);
    sPspHwTotals.counter[3] += (u32) (end->memory - start->memory);
    sPspHwTotals.counter[4] += (u32) (end->copz - start->copz);
    sPspHwTotals.counter[5] += (u32) (end->vfpu - start->vfpu);
    sPspHwTotals.counter[6] += (u32) (end->sleep - start->sleep);
    sPspHwTotals.counter[7] += (u32) (end->bus_access - start->bus_access);
    sPspHwTotals.counter[8] += (u32) (end->uncached_load - start->uncached_load);
    sPspHwTotals.counter[9] += (u32) (end->uncached_store - start->uncached_store);
    sPspHwTotals.counter[10] += (u32) (end->cached_load - start->cached_load);
    sPspHwTotals.counter[11] += (u32) (end->cached_store - start->cached_store);
    sPspHwTotals.counter[12] += (u32) (end->i_miss - start->i_miss);
    sPspHwTotals.counter[13] += (u32) (end->d_miss - start->d_miss);
    sPspHwTotals.counter[14] += (u32) (end->d_writeback - start->d_writeback);
    sPspHwTotals.counter[15] += (u32) (end->cop0_inst - start->cop0_inst);
    sPspHwTotals.counter[16] += (u32) (end->fpu_inst - start->fpu_inst);
    sPspHwTotals.counter[17] += (u32) (end->vfpu_inst - start->vfpu_inst);
    sPspHwTotals.counter[18] += (u32) (end->local_bus - start->local_bus);
}

static int psp_hw_find_slot(char* path, u32 pathSize, u32* slotOut) {
    SceIoStat stat;
    const char* mode = sPspHwCaptureProjectedOutput ? "projected" : "no-projected";
    u32 slot;

    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/GAME", 0777);
    sceIoMkdir(PSP_HW_PROFILE_DIR, 0777);

    for (slot = 0; slot <= PSP_HW_PROFILE_MAX_SLOT; slot++) {
        snprintf(path, pathSize, "%s/hw-%03lu-%s.csv", PSP_HW_PROFILE_DIR, (unsigned long) slot, mode);
        if (sceIoGetstat(path, &stat) < 0) {
            *slotOut = slot;
            return 1;
        }
    }
    return 0;
}

static void psp_hw_dump(void) {
    char path[112];
    char line[256];
    SceUID fd;
    u32 slot;
    u32 i;

    if (sPspHwTotals.frames == 0) {
        sPspHwStatus = PSP_HW_STATUS_READY;
        return;
    }
    if (!psp_hw_find_slot(path, sizeof(path), &slot)) {
        sPspHwStatus = PSP_HW_STATUS_ERROR;
        return;
    }

    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0) {
        sPspHwStatus = PSP_HW_STATUS_ERROR;
        return;
    }

    snprintf(line, sizeof(line),
             "sf64_git_sha,%s\nn64psp_git_sha,%s\nmode,%s\nframes,%lu\ncommands,%llu\n"
             "loaded_vertices,%llu\nsubmitted_vertices,%llu\nratio_scale,%u\n",
             SF64_GIT_SHA, N64PSP_GIT_SHA,
             sPspHwCaptureProjectedOutput ? "projected" : "no-projected",
             (unsigned long) sPspHwTotals.frames,
             (unsigned long long) sPspHwTotals.commands,
             (unsigned long long) sPspHwTotals.loadedVertices,
             (unsigned long long) sPspHwTotals.submittedVertices,
             PSP_HW_RATIO_SCALE);
    if (!psp_hw_write_all(fd, line) ||
        !psp_hw_write_all(fd,
                          "counter_source,thread_profiler_delta\n"
                          "counter,total,per_frame_x1000,per_command_x1000,"
                          "per_loaded_vertex_x1000,per_submitted_vertex_x1000\n")) {
        sceIoClose(fd);
        sPspHwStatus = PSP_HW_STATUS_ERROR;
        return;
    }

    for (i = 0; i < PSP_HW_COUNTER_COUNT; i++) {
        u64 total = sPspHwTotals.counter[i];

        snprintf(line, sizeof(line), "%s,%llu,%llu,%llu,%llu,%llu\n",
                 sPspHwCounterNames[i],
                 (unsigned long long) total,
                 (unsigned long long) psp_hw_ratio(total, sPspHwTotals.frames),
                 (unsigned long long) psp_hw_ratio(total, sPspHwTotals.commands),
                 (unsigned long long) psp_hw_ratio(total, sPspHwTotals.loadedVertices),
                 (unsigned long long) psp_hw_ratio(total, sPspHwTotals.submittedVertices));
        if (!psp_hw_write_all(fd, line)) {
            sceIoClose(fd);
            sPspHwStatus = PSP_HW_STATUS_ERROR;
            return;
        }
    }

    sceIoClose(fd);
    sPspHwSavedSlot = slot;
    sPspHwStatus = PSP_HW_STATUS_SAVED;
}

static void psp_hw_start(void) {
    if (sPspHwCaptureActive) {
        return;
    }
    memset(&sPspHwTotals, 0, sizeof(sPspHwTotals));
    sPspHwCaptureProjectedOutput = sPspHwUseProjectedOutput;
    sPspHwStopRequested = 0;
    sPspHwCaptureActive = 1;
    sPspHwStatus = PSP_HW_STATUS_RECORDING;
}

void PspHwCounterProfile_Init(void) {
    memset(&sPspHwTotals, 0, sizeof(sPspHwTotals));
    sPspHwCaptureActive = 0;
    sPspHwStopRequested = 0;
    sPspHwUseProjectedOutput = 1;
    sPspHwPreviousButtons = 0;
    sPspHwStatus = PSP_HW_STATUS_READY;
}

void PspHwCounterProfile_Shutdown(void) {
    if (sPspHwCaptureActive) {
        sPspHwCaptureActive = 0;
        psp_hw_dump();
    }
}

int PspHwCounterProfile_PollControls(u32 rawButtons) {
    const u32 baselineCombo = PSP_CTRL_SELECT | PSP_CTRL_LEFT;
    const u32 candidateCombo = PSP_CTRL_SELECT | PSP_CTRL_RIGHT;
    const u32 startCombo = PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER;
    const u32 stopCombo = PSP_CTRL_SELECT | PSP_CTRL_RTRIGGER;
    int consumed = 0;

    if (!sPspHwCaptureActive &&
        ((rawButtons & baselineCombo) == baselineCombo) &&
        ((sPspHwPreviousButtons & baselineCombo) != baselineCombo)) {
        sPspHwUseProjectedOutput = 1;
        sPspHwStatus = PSP_HW_STATUS_READY;
        consumed = 1;
    } else if (!sPspHwCaptureActive &&
               ((rawButtons & candidateCombo) == candidateCombo) &&
               ((sPspHwPreviousButtons & candidateCombo) != candidateCombo)) {
        sPspHwUseProjectedOutput = 0;
        sPspHwStatus = PSP_HW_STATUS_READY;
        consumed = 1;
    } else if (((rawButtons & startCombo) == startCombo) &&
               ((sPspHwPreviousButtons & startCombo) != startCombo)) {
        psp_hw_start();
        consumed = 1;
    } else if (((rawButtons & stopCombo) == stopCombo) &&
               ((sPspHwPreviousButtons & stopCombo) != stopCombo)) {
        if (sPspHwCaptureActive) {
            sPspHwStopRequested = 1;
        }
        consumed = 1;
    }

    if (((rawButtons & baselineCombo) == baselineCombo) ||
        ((rawButtons & candidateCombo) == candidateCombo) ||
        ((rawButtons & startCombo) == startCombo) ||
        ((rawButtons & stopCombo) == stopCombo)) {
        consumed = 1;
    }

    sPspHwPreviousButtons = rawButtons;
    return consumed;
}

void PspHwCounterProfile_BeginFrame(void) {
    if (!sPspHwCaptureActive) {
        return;
    }
    if (!psp_hw_snapshot(&sPspHwFrameStart)) {
        sPspHwCaptureActive = 0;
        sPspHwStatus = PSP_HW_STATUS_UNAVAILABLE;
    }
}

void PspHwCounterProfile_EndFrame(u32 commands, u32 loadedVertices, u32 submittedVertices) {
    PspHwCounterSnapshot end;

    if (!sPspHwCaptureActive) {
        return;
    }

    if (!psp_hw_snapshot(&end)) {
        sPspHwCaptureActive = 0;
        sPspHwStatus = PSP_HW_STATUS_UNAVAILABLE;
        return;
    }
    psp_hw_accumulate_delta(&sPspHwFrameStart.regs, &end.regs);
    sPspHwTotals.commands += commands;
    sPspHwTotals.loadedVertices += loadedVertices;
    sPspHwTotals.submittedVertices += submittedVertices;
    sPspHwTotals.frames++;

    if ((sPspHwTotals.frames >= PSP_HW_PROFILE_FRAMES) || sPspHwStopRequested) {
        sPspHwCaptureActive = 0;
        psp_hw_dump();
    }
}

int PspHwCounterProfile_UseProjectedOutput(void) {
    return sPspHwUseProjectedOutput;
}

void PspHwCounterProfile_DrawStatus(void) {
    const char* mode = sPspHwUseProjectedOutput ? "A PROJECTED" : "B NO-PROJ";

    pspDebugScreenSetXY(0, 2);
    if (sPspHwStatus == PSP_HW_STATUS_RECORDING) {
        pspDebugScreenPrintf("HW %s REC %03lu/%03u   ", mode,
                             (unsigned long) sPspHwTotals.frames, PSP_HW_PROFILE_FRAMES);
    } else if (sPspHwStatus == PSP_HW_STATUS_SAVED) {
        pspDebugScreenPrintf("HW %s SAVED %03lu   ", mode, (unsigned long) sPspHwSavedSlot);
    } else if (sPspHwStatus == PSP_HW_STATUS_UNAVAILABLE) {
        pspDebugScreenPrintf("HW %s NEED PROFMODE T", mode);
    } else if (sPspHwStatus == PSP_HW_STATUS_ERROR) {
        pspDebugScreenPrintf("HW %s ERROR       ", mode);
    } else {
        pspDebugScreenPrintf("HW %s READY       ", mode);
    }
}
