#include "PR/ultratypes.h"
#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/gfx/gfx_psp_color.h"
#include "src/psp/platform.h"
#include "src/psp/renderer_starfield.h"

#include <stdio.h>

#define PSP_STARFIELD_CAP 1000
#define PSP_STARFIELD_VERTICES_PER_STAR 2
#define PSP_STARFIELD_CHUNK_STARS 512

typedef struct {
    s16 x;
    s16 y;
    u32 color;
} PspStar;

static PspStar sStarfieldStars[PSP_STARFIELD_CAP];
static PspGfxVertex sStarfieldVertices[PSP_STARFIELD_CHUNK_STARS * PSP_STARFIELD_VERTICES_PER_STAR]
    __attribute__((aligned(16)));
static u32 sStarfieldCount;
static int sStarfieldReady;

#if PSP_RENDERER_DIAGNOSTICS
static u32 sStarfieldDiagRequested;
static u32 sStarfieldDiagTraversed;
static u32 sStarfieldDroppedStars;
static u32 sStarfieldDiagLast[6] = { 0xFFFFFFFFu, 0, 0, 0, 0, 0 };
#endif

static const PspGfxDrawState sStarfieldDrawState = {
    { { 0, 0, 0, 0 } },
    PSP_GFX_TEX_REPLACE,
    0,
    PSP_GFX_WRAP_CLAMP,
    PSP_GFX_WRAP_CLAMP,
    0,
    0,
    0,
    0,
    0,
    0,
    NULL,
    0.0f,
    0.0f,
    NULL,
    0,
    1,
    0,
    PSP_GFX_VIEWPORT_FULL,
};

#if PSP_RENDERER_DIAGNOSTICS
static void psp_starfield_log_diag(u32 chunks) {
    u32 values[6];
    char line[128];
    u32 i;
    int changed = 0;

    values[0] = sStarfieldDiagRequested;
    values[1] = sStarfieldDiagTraversed;
    values[2] = sStarfieldCount + sStarfieldDroppedStars;
    values[3] = sStarfieldCount;
    values[4] = chunks;
    values[5] = sStarfieldDroppedStars;

    for (i = 0; i < 6; i++) {
        if (values[i] != sStarfieldDiagLast[i]) {
            changed = 1;
        }
        sStarfieldDiagLast[i] = values[i];
    }
    if (!changed) {
        return;
    }

    snprintf(line, sizeof(line), "[starfield] req=%lu trav=%lu vis=%lu sub=%lu chunks=%lu drop=%lu",
             (unsigned long) values[0], (unsigned long) values[1], (unsigned long) values[2],
             (unsigned long) values[3], (unsigned long) values[4], (unsigned long) values[5]);
    PspPlatform_LogLine(line);
}
#endif

void PspStarfield_Begin(void) {
    sStarfieldCount = 0;
    sStarfieldReady = 0;
#if PSP_RENDERER_DIAGNOSTICS
    sStarfieldDiagRequested = 0;
    sStarfieldDiagTraversed = 0;
    sStarfieldDroppedStars = 0;
#endif
}

void PspStarfield_Add(s16 x, s16 y, u32 n64FillColor) {
    PspStar* star;

    if (sStarfieldCount >= PSP_STARFIELD_CAP) {
#if PSP_RENDERER_DIAGNOSTICS
        sStarfieldDroppedStars++;
#endif
        return;
    }

    star = &sStarfieldStars[sStarfieldCount++];
    star->x = x;
    star->y = y;
    star->color = psp_gfx_rgba5551_to_abgr8888((u16) (n64FillColor & 0xFFFFu));
}

void PspStarfield_End(void) {
    sStarfieldReady = 1;
}

#if PSP_RENDERER_DIAGNOSTICS
void PspStarfield_DiagCounts(u32 requested, u32 traversed) {
    sStarfieldDiagRequested = requested;
    sStarfieldDiagTraversed = traversed;
}
#endif

void PspStarfield_DrawPending(void) {
    u32 first;
#if PSP_RENDERER_DIAGNOSTICS
    u32 chunks = 0;
#endif

    if (!sStarfieldReady) {
        return;
    }
    sStarfieldReady = 0;

    if (sStarfieldCount == 0) {
        return;
    }

    PspGfxBackend_DrawSolidRect(0.0f, 0.0f, 320.0f, 240.0f, 0xFF000000u, 0, PSP_GFX_VIEWPORT_FULL);

    for (first = 0; first < sStarfieldCount; first += PSP_STARFIELD_CHUNK_STARS) {
        u32 chunkCount = sStarfieldCount - first;
        u32 out = 0;
        u32 i;

        if (chunkCount > PSP_STARFIELD_CHUNK_STARS) {
            chunkCount = PSP_STARFIELD_CHUNK_STARS;
        }

        for (i = 0; i < chunkCount; i++) {
            const PspStar* star = &sStarfieldStars[first + i];
            PspGfxVertex* vertex = &sStarfieldVertices[out];

            vertex[0].u = 0.0f;
            vertex[0].v = 0.0f;
            vertex[0].color = star->color;
            vertex[0].x = ((float) star->x / 160.0f) - 1.0f;
            vertex[0].y = 1.0f - ((float) star->y / 120.0f);
            vertex[0].z = 0.0f;

            vertex[1].u = 0.0f;
            vertex[1].v = 0.0f;
            vertex[1].color = star->color;
            vertex[1].x = ((float) (star->x + 1) / 160.0f) - 1.0f;
            vertex[1].y = 1.0f - ((float) (star->y + 1) / 120.0f);
            vertex[1].z = 0.0f;
            out += PSP_STARFIELD_VERTICES_PER_STAR;
        }

        PspGfxBackend_DrawSprites(sStarfieldVertices, out, &sStarfieldDrawState);
#if PSP_RENDERER_DIAGNOSTICS
        chunks++;
#endif
    }

#if PSP_RENDERER_DIAGNOSTICS
    psp_starfield_log_diag(chunks);
#endif
}
