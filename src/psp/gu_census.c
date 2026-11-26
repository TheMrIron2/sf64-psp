#include "src/psp/gu_census.h"
#include "src/psp/menu.h"
#include "src/psp/audio_mode.h"
#include "src/psp/display.h"
#include "src/psp/gfx/gfx_psp_backend.h"
#include "sf64thread.h"
#include <n64psp/display.h>
#include <pspkernel.h>
#include <psppower.h>
#include <pspiofilemgr.h>
#include <stdio.h>

#define CENSUS_ROWS 8192

typedef struct {
    u32 time, kind, serial, menu, value[8];
} CensusRow;

#if PROFILE_CENSUS_DISPATCH
static u32 gPspCensusDispatch[20];
static int sTaskThread, sCounting;

void PspCensus_Dispatch(unsigned index) {
    if (sCounting && sceKernelGetThreadId() == sTaskThread) gPspCensusDispatch[index]++;
}
#endif

static CensusRow sRows[CENSUS_ROWS];
static u32 sCount, sSerial, sStart, sTaskStart, sFrontendEnd, sSubmitEnd, sSimulationStart;
static u32 sPrepareStart, sPrepareUs, sRenderOnly;
static u32 sGfxWaitStart;
static int sActive;
static const char* sStatus = "idle";

void PspCensus_Event(u32 kind, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h) {
    int lock;
    CensusRow* row;
    if (!sActive) return;
    lock = sceKernelCpuSuspendIntr();
    if (sCount < CENSUS_ROWS) {
        row = &sRows[sCount++];
        row->time = sceKernelGetSystemTimeLow() - sStart;
        row->kind = kind;
        row->serial = sSerial;
        row->menu = PspMenu_IsOpen();
        row->value[0] = a; row->value[1] = b; row->value[2] = c; row->value[3] = d;
        row->value[4] = e; row->value[5] = f; row->value[6] = g; row->value[7] = h;
    } else {
        sActive = 0;
        sStatus = "full - save";
    }
    sceKernelCpuResumeIntr(lock);
}

void PspCensus_Start(void) {
    sCount = sSerial = 0;
    sStart = sceKernelGetSystemTimeLow();
    sActive = 1;
    sStatus = "recording";
    PspCensus_Event(0, PspGfxBackend_GetActive(), PspAudioMode_GetRequested(),
                    PspDisplay_GetConfig()->mode, scePowerGetCpuClockFrequencyInt(),
                    scePowerGetBusClockFrequencyInt(), PROFILE_CENSUS, GFX_TASK_POOL_COUNT,
                    sizeof(gGfxPools));
}

const char* PspCensus_Status(void) { return sStatus; }

void PspCensus_Begin(void) {
    if (!sActive) return;
#if PROFILE_CENSUS_DISPATCH
    {
        unsigned i;
        sTaskThread = sceKernelGetThreadId();
        sCounting = 1;
        for (i = 0; i < 20; i++) gPspCensusDispatch[i] = 0;
    }
#endif
    sSerial++;
    sTaskStart = sceKernelGetSystemTimeLow();
    sFrontendEnd = sSubmitEnd = sTaskStart;
}

void PspCensus_Mark(int stage) {
    if (!sActive) return;
    if (stage == 0) sFrontendEnd = sceKernelGetSystemTimeLow();
    else sSubmitEnd = sceKernelGetSystemTimeLow();
}

void PspCensus_End(int presented) {
    u32 now;
    if (!sActive) return;
    now = sceKernelGetSystemTimeLow();
#if PROFILE_CENSUS_DISPATCH
    sCounting = 0;
    PspCensus_Event(5, gPspCensusDispatch[0], gPspCensusDispatch[1], gPspCensusDispatch[2],
                    gPspCensusDispatch[3], gPspCensusDispatch[4], gPspCensusDispatch[5],
                    gPspCensusDispatch[6], gPspCensusDispatch[7]);
    PspCensus_Event(6, gPspCensusDispatch[8], gPspCensusDispatch[9], gPspCensusDispatch[10],
                    gPspCensusDispatch[11], gPspCensusDispatch[12], gPspCensusDispatch[13],
                    gPspCensusDispatch[14], gPspCensusDispatch[15]);
    PspCensus_Event(7, gPspCensusDispatch[16], gPspCensusDispatch[17], gPspCensusDispatch[18],
                    gPspCensusDispatch[19], 0, 0, 0, 0);
#endif
    PspCensus_Event(1, sTaskStart - sStart, sFrontendEnd - sTaskStart,
                    sSubmitEnd - sFrontendEnd, now - sSubmitEnd, presented,
                    sSubmitEnd - sTaskStart, now - sStart, sRenderOnly);
    PspCensus_Event(8, sPrepareUs, sRenderOnly, 0, 0, 0, 0, 0, 0);
}

void PspCensus_Prepare(int end, int renderOnly) {
    if (!sActive) return;
    if (!end) sPrepareStart = sceKernelGetSystemTimeLow();
    else {
        sPrepareUs = sceKernelGetSystemTimeLow() - sPrepareStart;
        sRenderOnly = renderOnly;
    }
}

void PspCensus_Simulation(int end, int taskInFlight, s32 pool, u32 deadlineVi) {
    if (!sActive) return;
    if (!end) sSimulationStart = sceKernelGetSystemTimeLow();
    else PspCensus_Event(4, sceKernelGetSystemTimeLow() - sSimulationStart, taskInFlight, pool, deadlineVi, 0, 0, 0, 0);
}

void PspCensus_GfxWait(int end, u32 reasons) {
    if (!sActive) return;
    if (!end) sGfxWaitStart = sceKernelGetSystemTimeLow();
    else PspCensus_Event(9, sceKernelGetSystemTimeLow() - sGfxWaitStart, reasons, 0, 0, 0, 0, 0, 0);
}

void PspCensus_Save(void) {
    char path[64], line[256];
    u32 slot, i;
    int fd, length, failed = 0;
    sActive = 0;
#if PROFILE_CENSUS_DISPATCH
    sCounting = 0;
#endif
    sceIoMkdir("ms0:/SF64PROFILE", 0777);
    for (slot = 0; slot < 1000; slot++) {
        snprintf(path, sizeof(path), "ms0:/SF64PROFILE/census-%03lu.csv", (unsigned long) slot);
        fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_EXCL, 0666);
        if (fd >= 0) break;
    }
    if (slot == 1000) { sStatus = "save failed"; return; }
    length = snprintf(line, sizeof(line), "time_us,kind,serial,menu,a,b,c,d,e,f,g,h\n");
    failed |= sceIoWrite(fd, line, length) != length;
    for (i = 0; i < sCount; i++) {
        CensusRow* r = &sRows[i];
        length = snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
                          r->time, r->kind, r->serial, r->menu, r->value[0], r->value[1],
                          r->value[2], r->value[3], r->value[4], r->value[5], r->value[6], r->value[7]);
        if (sceIoWrite(fd, line, length) != length) { failed = 1; break; }
    }
    failed |= sceIoClose(fd) < 0;
    sStatus = failed ? "save failed" : "saved";
}
