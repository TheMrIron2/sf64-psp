#include "PR/ultratypes.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/gfx/gfx_psp_dl.h"
#include "src/psp/gfx/gfx_psp_device.h"
#include "src/psp/display.h"
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
static u64 sPerfRenderAccumUs;

static u32 sPerfFpsTenths;
static u32 sPerfGfxMsTenths;
static int sPerfReady;

#endif

#if PSP_FPS_OVERLAY

static void psp_renderer_draw_perf_overlay(void) {
    void* framebuffer;
    int bufferWidth;
    int pixelFormat;
    uintptr_t uncachedAddress;

    if (!sPerfReady) {
        return;
    }

    framebuffer = NULL;
    bufferWidth = 0;
    pixelFormat = 0;

    framebuffer = PspGfxDevice_GetPresentedFrameBuffer(&bufferWidth, &pixelFormat);

    if ((framebuffer == NULL) || (bufferWidth != 512)) {
        return;
    }

    uncachedAddress = ((uintptr_t) framebuffer) | (uintptr_t) 0x40000000U;

    pspDebugScreenSetBase((u32*) uncachedAddress);
    pspDebugScreenSetColorMode(pixelFormat);

    pspDebugScreenSetBackColor(0x00000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenEnableBackColor(1);
    pspDebugScreenSetXY(0, 0);

    pspDebugScreenPrintf(
        "FPS %lu.%lu  GFX %lu.%lums   ",
        (unsigned long) (sPerfFpsTenths / 10),
        (unsigned long) (sPerfFpsTenths % 10),
        (unsigned long) (sPerfGfxMsTenths / 10),
        (unsigned long) (sPerfGfxMsTenths % 10)
    );
}

static void psp_renderer_perf_frame_complete(u64 renderUs) {
    SceInt64 now;
    u64 elapsed;
    u64 fpsNumerator;
    u64 renderDenominator;

    now = sceKernelGetSystemTimeWide();

    if (sPerfWindowStart == 0) {
        sPerfWindowStart = now;
        return;
    }

    sPerfWindowFrames++;
    sPerfRenderAccumUs += renderUs;

    elapsed = (u64) (now - sPerfWindowStart);

    if (elapsed < 1000000ULL) {
        return;
    }

    fpsNumerator = (u64) sPerfWindowFrames * 10000000ULL;

    sPerfFpsTenths =
        (u32) ((fpsNumerator + (elapsed / 2ULL)) / elapsed);

    renderDenominator = (u64) sPerfWindowFrames * 100ULL;

    sPerfGfxMsTenths =
        (u32) ((sPerfRenderAccumUs + (renderDenominator / 2ULL)) /
               renderDenominator);

    sPerfWindowStart = now;
    sPerfWindowFrames = 0;
    sPerfRenderAccumUs = 0;
    sPerfReady = 1;
}

#endif

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

void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    const Gfx* dl;

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
            return;
        }

    #if PSP_FPS_OVERLAY
        renderStart = sceKernelGetSystemTimeWide();
    #endif

        PspHwCounterProfile_FrameBegin();
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_TASK);
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_FRONTEND);
        PspProfiler_ComponentTaskBegin();
        PspGfxDevice_BeginFrame();

        if ((task != NULL) && (task->task.t.data_ptr != NULL)) {
            dl = (const Gfx*) task->task.t.data_ptr;
            PspGfxDl_Run(dl, taskIndex, NULL);
    #if PROFILE_HW_COUNTERS
            /* Only after a run, the context still holds the previous task otherwise */
            PspGfxDl_GetLastWork(&hwCommands, &hwLoadedVertices, &hwSubmittedVertices);
    #endif
        }

        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_FRONTEND);
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_FLUSH);
        PspGfxDevice_Submit();
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_FLUSH);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_TASK);

    #if PSP_FPS_OVERLAY
        renderEnd = sceKernelGetSystemTimeWide();
    #endif

        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_PRESENT);
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PRESENT_SWAP);
        PspGfxDevice_Present();
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PRESENT_SWAP);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_PRESENT);
        PspProfiler_ComponentTaskEnd();

    #if PROFILE_HW_COUNTERS
        PspHwCounterProfile_FrameEnd(hwCommands, hwLoadedVertices, hwSubmittedVertices);
    #endif

    #if PSP_FPS_OVERLAY
        psp_renderer_perf_frame_complete(
            (u64) (renderEnd - renderStart)
        );

        psp_renderer_draw_perf_overlay();
    #endif

        PspProfiler_DrawStatus();
        PspHwCounterProfile_DrawStatus();
}

int PspRenderer_HistoryHudCacheReady(void) {
    return PspGfxBackend_ReplayCacheReady();
}

void PspRenderer_HistoryHudCacheInvalidate(void) {
    PspGfxBackend_ReplayCacheInvalidate();
}
