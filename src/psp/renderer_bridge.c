#if PSP_GFX_BACKEND_GU
#include "gfx/gfx_psp_mesh_diagnostic.h"
#endif
#include "PR/ultratypes.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/gfx/gfx_psp_dl.h"
#include "src/psp/gfx/gfx_psp_device.h"
#include "src/psp/display.h"
#include "src/psp/frame_interpolation.h"
#include "src/psp/frame_scheduler.h"
#include "src/psp/hw_counter_profile.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#include <pspkernel.h>
#include <pspdebug.h>
#include <stdint.h>
// <string.h> drags in <strings.h>, which conflicts with PR/os_libc.h via sf64thread.h

#include <stdio.h>

#ifndef PSP_FPS_OVERLAY
#define PSP_FPS_OVERLAY 0
#endif

#if PSP_FPS_OVERLAY

static SceInt64 sPerfWindowStart;
static u32 sPerfWindowFrames;
static volatile u32 sPerfSimulationTicks;
static u32 sPerfWindowSimulationTicks;
static u64 sPerfRenderAccumUs;
static u32 sPerfRenderPeakUs;
static u32 sPerfWindowSkipped;

static u32 sPerfFpsTenths;
static u32 sPerfSimulationTenths;
static u32 sPerfGfxMsTenths;
static u32 sPerfGfxPeakTenths;
static u32 sPerfSkipped;
static int sPerfReady;

#endif

static void psp_renderer_draw_overlays(void) {
#if PSP_FPS_OVERLAY || PROFILE_GPROF || PROFILE_PHASES || PROFILE_HW_COUNTERS || PSP_NATIVE_COVERAGE_AB
    void* framebuffer;
    int bufferWidth;
    int pixelFormat;
    uintptr_t uncachedAddress;

    framebuffer = NULL;
    bufferWidth = 0;
    pixelFormat = 0;

    framebuffer = PspGfxDevice_GetOverlayFrameBuffer(&bufferWidth, &pixelFormat);

    if ((framebuffer == NULL) || (bufferWidth != 512)) {
        return;
    }

    uncachedAddress = ((uintptr_t) framebuffer) | (uintptr_t) 0x40000000U;

    pspDebugScreenSetBase((u32*) uncachedAddress);
    pspDebugScreenSetColorMode(pixelFormat);

    pspDebugScreenSetBackColor(0x00000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenEnableBackColor(1);
#if PSP_FPS_OVERLAY
    if (sPerfReady) {
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenPrintf(
            "FPS %lu.%lu  SIM %lu.%lu  GFX %lu.%lu/%lu.%lums  CAP %u SKIP %lu   ",
            (unsigned long) (sPerfFpsTenths / 10),
            (unsigned long) (sPerfFpsTenths % 10),
            (unsigned long) (sPerfSimulationTenths / 10),
            (unsigned long) (sPerfSimulationTenths % 10),
            (unsigned long) (sPerfGfxMsTenths / 10),
            (unsigned long) (sPerfGfxMsTenths % 10),
            (unsigned long) (sPerfGfxPeakTenths / 10),
            (unsigned long) (sPerfGfxPeakTenths % 10),
            (unsigned int) (60 / PspFrameScheduler_GetPresentationVIs()),
            (unsigned long) sPerfSkipped
        );
    }
#endif
#if PSP_GFX_BACKEND_GU
    {
        PspGfxDlRetainedStats stats;
        uint32_t enabled, remaining, status;
        PspGfxDl_GetRetainedStats(&stats);
        PspMeshDiagnostic_Status(&enabled, &remaining, &status);
        pspDebugScreenSetXY(0, 3);
        pspDebugScreenPrintf("RC%lu H%lu F%lu D%lu V%lu R%lu S%lu       ", (unsigned long) stats.mode,
            (unsigned long) stats.hits, (unsigned long) stats.fallbacks,
            (unsigned long) stats.spans, (unsigned long) stats.vertices,
            (unsigned long) stats.restored, (unsigned long) stats.skipped);
        pspDebugScreenSetXY(0, 4);
        pspDebugScreenPrintf("RD%lu CAP%lu STATUS%lu          ", (unsigned long) enabled,
            (unsigned long) remaining, (unsigned long) status);
    }
#elif PSP_NATIVE_COVERAGE_AB
    {
        PspGfxDlNativeCoverageStats stats;

        PspGfxDl_GetNativeCoverageStats(&stats);
        pspDebugScreenSetXY(0, 3);
        pspDebugScreenPrintf("AC%lu F%lu %s%lu       ", (unsigned long) stats.mode,
                             (unsigned long) stats.calls, stats.mode == 1 ? "MC" : "BY",
                             (unsigned long) stats.commands);
    }
#endif
    PspProfiler_DrawStatus();
    PspHwCounterProfile_DrawStatus();
#endif
}

#if PSP_FPS_OVERLAY

static void psp_renderer_perf_frame_complete(u64 renderUs) {
    SceInt64 now;
    u64 elapsed;
    u64 fpsNumerator;
    u32 simulationTicks;
    u64 renderDenominator;

    now = sceKernelGetSystemTimeWide();

    if (sPerfWindowStart == 0) {
        sPerfWindowStart = now;
        sPerfWindowSimulationTicks = sPerfSimulationTicks;
        sPerfWindowSkipped = 0;
        return;
    }

    sPerfWindowFrames++;
    sPerfRenderAccumUs += renderUs;
    if (renderUs > sPerfRenderPeakUs) {
        sPerfRenderPeakUs = (u32) renderUs;
    }

    elapsed = (u64) (now - sPerfWindowStart);

    if (elapsed < 1000000ULL) {
        return;
    }

    fpsNumerator = (u64) sPerfWindowFrames * 10000000ULL;
    simulationTicks = sPerfSimulationTicks;

    sPerfFpsTenths =
        (u32) ((fpsNumerator + (elapsed / 2ULL)) / elapsed);
    sPerfSimulationTenths =
        (u32) ((((u64) (simulationTicks - sPerfWindowSimulationTicks) * 10000000ULL) + (elapsed / 2ULL)) /
               elapsed);

    renderDenominator = (u64) sPerfWindowFrames * 100ULL;

    sPerfGfxMsTenths =
        (u32) ((sPerfRenderAccumUs + (renderDenominator / 2ULL)) /
               renderDenominator);
    sPerfGfxPeakTenths = (sPerfRenderPeakUs + 50) / 100;
    sPerfSkipped = sPerfWindowSkipped;

    sPerfWindowStart = now;
    sPerfWindowFrames = 0;
    sPerfWindowSimulationTicks = simulationTicks;
    sPerfRenderAccumUs = 0;
    sPerfRenderPeakUs = 0;
    sPerfWindowSkipped = 0;
    sPerfReady = 1;
}

#endif

void PspRenderer_RecordSimulationTicks(u32 ticks) {
#if PSP_FPS_OVERLAY
    sPerfSimulationTicks += ticks;
#else
    (void) ticks;
#endif
}

void PspRenderer_Init(void) {
    if (PspGfxDevice_IsReady()) {
        return;
    }

    if (!PspDisplay_Init() || !PspGfxDevice_Init()) {
        return;
    }

#if PSP_GFX_BACKEND_PSPGL
    PspPlatform_LogLine("[pspgl] renderer init");
#else
    PspPlatform_LogLine("[gu] renderer init");
#endif
    PspGfxDevice_BeginFrame();
    PspGfxDevice_Submit();
    PspGfxDevice_Present();
}

int PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    const Gfx* dl;
    int presented;

    #if PROFILE_HW_COUNTERS
        u32 hwCommands = 0;
        u32 hwLoadedVertices = 0;
        u32 hwSubmittedVertices = 0;
    #endif

    #if PSP_FPS_OVERLAY
        SceInt64 renderStart;
        SceInt64 renderEnd;
    #endif

        if (!PspGfxDevice_IsReady()) {
            PspRenderer_Init();
        }

        if (!PspGfxDevice_IsReady()) {
            PspFrameInterpolation_FinishPresentation(task);
            return 0;
        }

        if (!PspFrameInterpolation_ShouldPresent(task)) {
#if PSP_FPS_OVERLAY
            sPerfWindowSkipped++;
#endif
            PspFrameInterpolation_FinishPresentation(task);
            return 0;
        }

    #if PSP_FPS_OVERLAY
        renderStart = sceKernelGetSystemTimeWide();
    #endif

        PspHwCounterProfile_FrameBegin();
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_TASK);
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_FRONTEND);
        PspProfiler_ComponentTaskBegin();
#if PSP_GFX_BACKEND_GU
        PspMeshDiagnostic_BeginTask();
#endif
        PspGfxDevice_BeginFrame();

        if ((task != NULL) && (task->task.t.data_ptr != NULL)) {
            dl = (const Gfx*) task->task.t.data_ptr;
            PspGfxDl_Run(dl, taskIndex, NULL);
    #if PROFILE_HW_COUNTERS
            /* Only after a run, the context still holds the previous task otherwise */
            PspGfxDl_GetLastWork(&hwCommands, &hwLoadedVertices, &hwSubmittedVertices);
    #endif
        }
        PspFrameInterpolation_FinishPresentation(task);

        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_FRONTEND);
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_FLUSH);
#if PSP_GFX_BACKEND_GU
        PspMeshDiagnostic_SubmitStart();
#endif
        PspGfxDevice_Submit();
#if PSP_GFX_BACKEND_GU
        PspMeshDiagnostic_SubmitEnd();
#endif
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_FLUSH);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_TASK);

    #if PSP_FPS_OVERLAY
        renderEnd = sceKernelGetSystemTimeWide();
    #endif

#if PSP_GFX_BACKEND_GU
        psp_renderer_draw_overlays();
#endif
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_PRESENT);
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PRESENT_SWAP);
        presented = PspGfxDevice_Present();
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PRESENT_SWAP);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_PRESENT);
        PspProfiler_ComponentTaskEnd();

    #if PROFILE_HW_COUNTERS
        PspHwCounterProfile_FrameEnd(hwCommands, hwLoadedVertices, hwSubmittedVertices);
    #endif

    #if PSP_FPS_OVERLAY
        if (presented) {
            psp_renderer_perf_frame_complete((u64) (renderEnd - renderStart));
        }
    #endif

#if PSP_GFX_BACKEND_PSPGL
        psp_renderer_draw_overlays();
#endif
#if PSP_GFX_BACKEND_GU
        PspMeshDiagnostic_EndTask(presented);
#endif
        return presented;
}

int PspRenderer_HistoryHudCacheReady(void) {
    return PspGfxBackend_ReplayCacheReady();
}

void PspRenderer_HistoryHudCacheInvalidate(void) {
    PspGfxBackend_ReplayCacheInvalidate();
}
