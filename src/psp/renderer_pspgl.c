#include "PR/ultratypes.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_psp_dl.h"
#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <stdint.h>

#define PSPGL_STARFIELD_CAP 512
#define PSPGL_STARFIELD_VERTICES_PER_STAR 6

typedef struct {
    s16 x;
    s16 y;
    u32 color;
} PspGlStar;

static PspGlStar sStarfieldStars[PSPGL_STARFIELD_CAP];
static PspGfxPspglColorVertex sStarfieldVertices[PSPGL_STARFIELD_CAP * PSPGL_STARFIELD_VERTICES_PER_STAR];
static const u8 sStarfieldCorners[PSPGL_STARFIELD_VERTICES_PER_STAR][2] = {
    { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 },
};
static u32 sStarfieldCount;
static int sStarfieldReady;

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

    if (sceDisplayGetFrameBuf(
            &framebuffer,
            &bufferWidth,
            &pixelFormat,
            PSP_DISPLAY_SETBUF_IMMEDIATE
        ) != 0) {
        return;
    }

    if ((framebuffer == NULL) || (bufferWidth != 512)) {
        return;
    }

    /*
     * Use the uncached alias so the displayed framebuffer immediately
     * sees the CPU writes.
     */
    uncachedAddress =
        ((uintptr_t) framebuffer) | (uintptr_t) 0x40000000U;

    pspDebugScreenSetBase((u32*) uncachedAddress);
    pspDebugScreenSetColorMode(pixelFormat);

    pspDebugScreenSetBackColor(0x00000000);
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenEnableBackColor(1);

    pspDebugScreenSetXY(0, 0);

    /*
     * No floating-point formatting: both values are stored in tenths.
     * Trailing spaces erase remnants when the number loses a digit.
     */
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

static void psp_renderer_draw_starfield(void) {
    u32 i;
    u32 out = 0;

    if (!sStarfieldReady || (sStarfieldCount == 0)) {
        return;
    }

    for (i = 0; i < sStarfieldCount; i++) {
        const PspGlStar* star = &sStarfieldStars[i];
        float x0 = ((float) star->x / 160.0f) - 1.0f;
        float y0 = 1.0f - ((float) star->y / 120.0f);
        float x1 = ((float) (star->x + 1) / 160.0f) - 1.0f;
        float y1 = 1.0f - ((float) (star->y + 1) / 120.0f);
        u32 corner;

        for (corner = 0; corner < PSPGL_STARFIELD_VERTICES_PER_STAR; corner++) {
            PspGfxPspglColorVertex* vertex = &sStarfieldVertices[out++];

            vertex->x = sStarfieldCorners[corner][0] ? x1 : x0;
            vertex->y = sStarfieldCorners[corner][1] ? y1 : y0;
            vertex->z = 0.0f;
            vertex->color = star->color;
            vertex->u = 0.0f;
            vertex->v = 0.0f;
        }
    }

    PspGfxPspgl_DrawColoredTriangles(sStarfieldVertices, out, 0, PSP_GFX_PSPGL_TEX_REPLACE,
                                     PSP_GFX_PSPGL_WRAP_CLAMP, PSP_GFX_PSPGL_WRAP_CLAMP, 0, 0, 0, 0, 0, 0, NULL,
                                     0.0f, 0.0f, NULL, 1);
    sStarfieldReady = 0;
}

void PspRenderer_Init(void) {
    if (PspGfx_IsReady()) {
        return;
    }

    if (!PspGfx_Init()) {
        return;
    }

    PspGfxPspgl_Init();
    PspPlatform_LogLine("[pspgl] renderer init");
    PspGfx_BeginFrame();
    PspGfxPspgl_BeginFrame();
    PspGfx_EndFrame();
}

void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    const Gfx* dl;

    #if PSP_FPS_OVERLAY
        SceInt64 renderStart;
        SceInt64 renderEnd;
    #endif

        if (!PspGfx_IsReady()) {
            PspRenderer_Init();
        }

        if (!PspGfx_IsReady()) {
            return;
        }

    #if PSP_FPS_OVERLAY
        renderStart = sceKernelGetSystemTimeWide();
    #endif

        PspGfx_BeginFrame();
        PspGfxPspgl_BeginFrame();
        psp_renderer_draw_starfield();

        if ((task != NULL) && (task->task.t.data_ptr != NULL)) {
            dl = (const Gfx*) task->task.t.data_ptr;
            PspGfxDl_Run(dl, taskIndex, NULL);
        }

    #if PSP_FPS_OVERLAY
        renderEnd = sceKernelGetSystemTimeWide();
    #endif

        PspGfx_EndFrame();

    #if PSP_FPS_OVERLAY
        psp_renderer_perf_frame_complete(
            (u64) (renderEnd - renderStart)
        );

        psp_renderer_draw_perf_overlay();
    #endif

        PspProfiler_DrawStatus();
}

void PspRenderer_BeginStarfield(void) {
    sStarfieldCount = 0;
    sStarfieldReady = 0;
}

void PspRenderer_AddStar(s16 x, s16 y, u32 n64FillColor) {
    PspGlStar* star;

    if (sStarfieldCount >= PSPGL_STARFIELD_CAP) {
        return;
    }

    star = &sStarfieldStars[sStarfieldCount++];
    star->x = x;
    star->y = y;
    star->color = n64FillColor;
}

void PspRenderer_EndStarfield(void) {
    sStarfieldReady = 1;
}
