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
#include "src/psp/gu_census.h"
#include "src/psp/menu.h"
#include "src/psp/net_stats.h"
#include "src/psp/settings.h"
#include "src/psp/overlay.h"

#include <n64psp/trig.h>
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

static int sRendererInitAttempted;

extern float __sinf(float);
extern float PspMath_AccurateVisualSine(float);

static float psp_renderer_fast_visual_sine(float radians) {
    int turns;
    float sine, cosine;

    if (!(radians >= -32768.0f && radians <= 32768.0f)) {
        return __sinf(radians);
    }
    // The high part of two pi has an exact product across this range
    turns = (int) (radians * 0.15915494309189535f);
    radians = (radians - (float) turns * 6.28125f) - (float) turns * 0.0019353071795864769f;
    n64psp_sincosf(radians, &sine, &cosine);
    return sine;
}

PspVisualSine PspRenderer_GetVisualSine(void) {
    if (!PspSettings_UseVisualSine()) {
        return __sinf;
    }
    return PspSettings_UseFastGraphics() ? psp_renderer_fast_visual_sine : PspMath_AccurateVisualSine;
}

unsigned PspRenderer_RepeatTextureBlock(const Gfx* list, unsigned prefixCommands) {
    enum { IMAGE = 1, RENDER_TILE = 2, LOAD_TILE = 4, TILE_SIZE = 8, LOAD = 16 };
    unsigned i, seen = 0;

    if (!PspSettings_ReuseTextureBlock() || prefixCommands < 5 || prefixCommands > 8) {
        return 0;
    }
    for (i = 0; i < prefixCommands; i++) {
        unsigned op = list[i].words.w0 >> 24;
        unsigned tile = (list[i].words.w1 >> 24) & 7;
        unsigned command;

        switch (op) {
            case G_RDPTILESYNC:
            case G_RDPLOADSYNC:
            case G_RDPPIPESYNC:
                continue;
            case G_SETTIMG:
                command = IMAGE;
                break;
            case G_SETTILE:
                if (tile != G_TX_RENDERTILE && tile != G_TX_LOADTILE) {
                    return 0;
                }
                command = tile == G_TX_RENDERTILE ? RENDER_TILE : LOAD_TILE;
                break;
            case G_SETTILESIZE:
                if (tile != G_TX_RENDERTILE) {
                    return 0;
                }
                command = TILE_SIZE;
                break;
            case G_LOADBLOCK:
                if (tile != G_TX_LOADTILE || !(seen & IMAGE) || !(seen & LOAD_TILE)) {
                    return 0;
                }
                command = LOAD;
                break;
            default:
                return 0;
        }
        if (seen & command) {
            return 0;
        }
        seen |= command;
    }
    if (seen != (IMAGE | RENDER_TILE | LOAD_TILE | TILE_SIZE | LOAD) ||
        list[i].words.w0 >> 24 != (u8) G_VTX ||
        (list[i + 1].words.w0 >> 24 != (u8) G_TRI1 && list[i + 1].words.w0 >> 24 != (u8) G_TRI2) ||
        list[i + 2].words.w0 >> 24 != (u8) G_ENDDL) {
        return 0;
    }
    return i;
}

static int psp_renderer_draw_overlays(int menuFrame) {
    char text[96];
    int menu = PspMenu_Visible();
    int perf = PspSettings_PerformanceVisible();
    (void) text;
#if PROFILE_GPROF || PROFILE_PHASES
    if (!menu && (PspProfiler_IsCapturing() || PspProfiler_NeedsService())) return 0;
#endif
#if PROFILE_HW_COUNTERS
    if (!menu && PspHwCounterProfile_IsCapturingTask()) return 0;
#endif
    if (!menu && !perf && !PROFILE_GPROF && !PROFILE_PHASES && !PROFILE_HW_COUNTERS &&
        !(PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU)) return 0;
    if (!(menuFrame ? PspOverlay_BeginMenuFrame() : PspOverlay_Begin())) return 0;
    if (menu) {
        PspMenu_Draw();
        PspOverlay_MenuEnd();
    }
#if PSP_FPS_OVERLAY
    if (perf && sPerfReady) {
        int width = snprintf(text, sizeof(text), "FPS %lu.%lu GFX %lu.%lu/%lu.%lums",
                 (unsigned long)(sPerfFpsTenths / 10), (unsigned long)(sPerfFpsTenths % 10),
                 (unsigned long)(sPerfGfxMsTenths / 10), (unsigned long)(sPerfGfxMsTenths % 10),
                 (unsigned long)(sPerfGfxPeakTenths / 10), (unsigned long)(sPerfGfxPeakTenths % 10));
        PspOverlay_Rect(0, 0, width * 6, 8, 0xFF000000);
        PspOverlay_Text(0, 0, 0xFFFFFFFF, text);
    }
#endif
#if PROFILE_GPROF || PROFILE_PHASES
    if (menu) PspOverlay_Rect(0, 9, 480, 8, 0xD8000000);
    snprintf(text, sizeof(text), "PROF %s", PspProfiler_GetStatus());
    PspOverlay_Text(0, 9, 0xFFFFFFFF, text);
#endif
#if PROFILE_HW_COUNTERS
    if (menu) PspOverlay_Rect(0, 18, 480, 8, 0xD8000000);
    snprintf(text, sizeof(text), "HW %s %s", PspHwCounterProfile_GetScene(), PspHwCounterProfile_GetStatus());
    PspOverlay_Text(0, 18, 0xFFFFFFFF, text);
#endif
#if PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU
    {
        PspGfxDlNativeCoverageStats stats;
        PspGfxDl_GetNativeCoverageStats(&stats);
        snprintf(text, sizeof(text), "AC%lu F%lu %s%lu", (unsigned long)stats.mode,
                 (unsigned long)stats.calls, stats.mode == 1 ? "MC" : "BY", (unsigned long)stats.commands);
        PspOverlay_Text(0, 27, 0xFFFFFFFF, text);
    }
#endif
    PspOverlay_End();
    return 1;
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
    char message[96];

    if (PspGfxDevice_IsReady()) {
        return;
    }
    if (sRendererInitAttempted) return;
    sRendererInitAttempted = 1;

    if (!PspDisplay_Init() || !PspGfxDevice_Init()) {
        snprintf(message, sizeof(message), "[psp] %s graphics initialization failed", PspGfxBackend_GetName());
        PspPlatform_LogLine(message);
        pspDebugScreenSetXY(0, 6);
        pspDebugScreenPrintf("Graphics backend %s failed to initialize\n", PspGfxBackend_GetName());
        pspDebugScreenPrintf("Use HOME to exit, then select another backend\n");
        return;
    }

    snprintf(message, sizeof(message), "[psp] %s renderer init", PspGfxBackend_GetName());
    PspPlatform_LogLine(message);
    PspGfxDevice_BeginFrame();
    PspGfxDevice_Submit();
    PspGfxDevice_Present();
}

static volatile int sTaskInputsLocked;

void PspRenderer_LockTaskInputs(void) {
    sTaskInputsLocked = 1;
}

int PspRenderer_TaskInputsLocked(void) {
    return sTaskInputsLocked;
}

int PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    const Gfx* dl;
    int presented;
    int measure = PspNetStats_Active();
    u32 measureStarted = 0;
#if PSP_GFX_BACKEND_GU
    int menuDrawn = 0;
#endif

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
            sTaskInputsLocked = 0;
            return 0;
        }

        PspCensus_Begin();
        if (!PspFrameInterpolation_ShouldPresent(task)) {
#if PSP_FPS_OVERLAY
            sPerfWindowSkipped++;
#endif
            PspCensus_Interpolation(task);
            PspFrameInterpolation_FinishPresentation(task);
            sTaskInputsLocked = 0;
            PspCensus_End(0);
            return 0;
        }

    #if PSP_FPS_OVERLAY
        renderStart = sceKernelGetSystemTimeWide();
    #endif

        if (measure) measureStarted = PspNetStats_NowUs();
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
        PspCensus_Interpolation(task);
        PspFrameInterpolation_FinishPresentation(task);
#if PSP_GFX_BACKEND_GU
        if (PspGfxBackend_IsGu() && PspMenu_Visible()) menuDrawn = psp_renderer_draw_overlays(1);
        // GU uses converted vertices and textures once traversal ends
        if (PspGfxBackend_IsGu()) sTaskInputsLocked = 0;
#endif

        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_FRONTEND);
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_FLUSH);
        PspCensus_Mark(0);
        PspGfxDevice_Submit();
        PspCensus_Mark(1);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_FLUSH);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_TASK);

    #if PSP_FPS_OVERLAY
        renderEnd = sceKernelGetSystemTimeWide();
    #endif
        if (measure) PspNetStats_Time(PSP_NET_RENDER, measureStarted);

#if PSP_GFX_BACKEND_GU
        if (!PspGfxBackend_IsGu() || !menuDrawn) psp_renderer_draw_overlays(0);
#else
        psp_renderer_draw_overlays(0);
#endif
        PspHwCounterProfile_ScopeBegin(PSP_HW_SCOPE_PRESENT);
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PRESENT_SWAP);
        if (measure) measureStarted = PspNetStats_NowUs();
        presented = PspGfxDevice_Present();
        sTaskInputsLocked = 0;
        if (measure) {
            PspNetStats_Time(PSP_NET_PRESENT, measureStarted);
            if (presented) PspNetStats_Presented(task, PspNetStats_NowUs());
        }
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PRESENT_SWAP);
        PspHwCounterProfile_ScopeEnd(PSP_HW_SCOPE_PRESENT);
        PspCensus_End(presented);
        PspProfiler_ComponentTaskEnd();

    #if PROFILE_HW_COUNTERS
        PspHwCounterProfile_FrameEnd(hwCommands, hwLoadedVertices, hwSubmittedVertices);
    #endif

    #if PSP_FPS_OVERLAY
        if (presented) {
            psp_renderer_perf_frame_complete((u64) (renderEnd - renderStart));
        }
    #endif

        return presented;
}

int PspRenderer_HistoryHudCacheReady(void) {
    return PspGfxBackend_ReplayCacheReady();
}

void PspRenderer_HistoryHudCacheInvalidate(void) {
    PspGfxBackend_ReplayCacheInvalidate();
}
