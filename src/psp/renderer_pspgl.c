#include "PR/ultratypes.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_psp_dl.h"
#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"
#include "src/psp/renderer.h"

#define PSPGL_STARFIELD_CAP 800
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
        float r = (float) (star->color & 0xFF) / 255.0f;
        float g = (float) ((star->color >> 8) & 0xFF) / 255.0f;
        float b = (float) ((star->color >> 16) & 0xFF) / 255.0f;
        float a = (float) (star->color >> 24) / 255.0f;
        u32 corner;

        for (corner = 0; corner < PSPGL_STARFIELD_VERTICES_PER_STAR; corner++) {
            PspGfxPspglColorVertex* vertex = &sStarfieldVertices[out++];

            vertex->x = sStarfieldCorners[corner][0] ? x1 : x0;
            vertex->y = sStarfieldCorners[corner][1] ? y1 : y0;
            vertex->z = 0.0f;
            vertex->r = r;
            vertex->g = g;
            vertex->b = b;
            vertex->a = a;
            vertex->u = 0.0f;
            vertex->v = 0.0f;
        }
    }

    PspGfxPspgl_DrawColoredTriangles(sStarfieldVertices, out, 0, PSP_GFX_PSPGL_TEX_REPLACE, 0, 0, 0, 0);
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

    if (!PspGfx_IsReady()) {
        PspRenderer_Init();
    }
    if (!PspGfx_IsReady()) {
        return;
    }

    PspGfx_BeginFrame();
    PspGfxPspgl_BeginFrame();
    psp_renderer_draw_starfield();

    if ((task != NULL) && (task->task.t.data_ptr != NULL)) {
        dl = (const Gfx*) task->task.t.data_ptr;
        PspGfxDl_Run(dl, taskIndex, NULL);
    }

    PspGfx_EndFrame();
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
