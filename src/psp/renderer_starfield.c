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

typedef struct {
    PspStar stars[PSP_STARFIELD_CAP];
    /* Source-indexed storage keeps a star's identity stable when visibility
     * changes reorder the submitted subset from one frame to the next. */
    u8 visible[PSP_STARFIELD_CAP];
    u16 sourceCount;
    u16 visibleCount;
    u8 valid;
#if PSP_RENDERER_DIAGNOSTICS
    u32 diagRequested;
    u32 diagTraversed;
    u32 droppedStars;
#endif
} PspStarfieldFrame;

static PspStarfieldFrame sStarfieldFrames[2];
static PspGfxVertex sStarfieldVertices[PSP_STARFIELD_CHUNK_STARS * PSP_STARFIELD_VERTICES_PER_STAR]
    __attribute__((aligned(16)));
static s32 sStarfieldRecordingPool = -1;
static s32 sStarfieldPresentationPool = -1;
static s32 sStarfieldPreviousPool = -1;
static f32 sStarfieldPresentationAlpha = 1.0f;

#if PSP_RENDERER_DIAGNOSTICS
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
static void psp_starfield_log_diag(const PspStarfieldFrame* frame, u32 chunks) {
    u32 values[6];
    char line[128];
    u32 i;
    int changed = 0;

    values[0] = frame->diagRequested;
    values[1] = frame->diagTraversed;
    values[2] = frame->visibleCount + frame->droppedStars;
    values[3] = frame->visibleCount;
    values[4] = chunks;
    values[5] = frame->droppedStars;

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

static s32 psp_starfield_pool_for_task(const SPTask* task) {
    if (task == &gGfxPools[0].task) {
        return 0;
    }
    if (task == &gGfxPools[1].task) {
        return 1;
    }
    return -1;
}

void PspStarfield_Reset(void) {
    sStarfieldFrames[0].valid = false;
    sStarfieldFrames[1].valid = false;
    sStarfieldRecordingPool = -1;
    sStarfieldPresentationPool = -1;
    sStarfieldPreviousPool = -1;
    sStarfieldPresentationAlpha = 1.0f;
}

void PspStarfield_BeginSimulationFrame(SPTask* task) {
    s32 pool = psp_starfield_pool_for_task(task);

    sStarfieldRecordingPool = pool;
    if (pool >= 0) {
        sStarfieldFrames[pool].valid = false;
    }
}

void PspStarfield_Begin(void) {
    PspStarfieldFrame* frame;
    u32 i;

    if (sStarfieldRecordingPool < 0) {
        return;
    }
    frame = &sStarfieldFrames[sStarfieldRecordingPool];
    for (i = 0; i < PSP_STARFIELD_CAP; i++) {
        frame->visible[i] = false;
    }
    frame->sourceCount = 0;
    frame->visibleCount = 0;
    frame->valid = false;
#if PSP_RENDERER_DIAGNOSTICS
    frame->diagRequested = 0;
    frame->diagTraversed = 0;
    frame->droppedStars = 0;
#endif
}

void PspStarfield_Add(u16 sourceIndex, s16 x, s16 y, u32 n64FillColor) {
    PspStarfieldFrame* frame;
    PspStar* star;

    if (sStarfieldRecordingPool < 0) {
        return;
    }
    frame = &sStarfieldFrames[sStarfieldRecordingPool];
    if (sourceIndex >= PSP_STARFIELD_CAP) {
#if PSP_RENDERER_DIAGNOSTICS
        frame->droppedStars++;
#endif
        return;
    }

    star = &frame->stars[sourceIndex];
    star->x = x;
    star->y = y;
    star->color = psp_gfx_rgba5551_to_abgr8888((u16) (n64FillColor & 0xFFFFu));
    if (!frame->visible[sourceIndex]) {
        frame->visible[sourceIndex] = true;
        frame->visibleCount++;
    }
    if (frame->sourceCount <= sourceIndex) {
        frame->sourceCount = sourceIndex + 1;
    }
}

void PspStarfield_End(void) {
    if (sStarfieldRecordingPool >= 0) {
        sStarfieldFrames[sStarfieldRecordingPool].valid = true;
    }
}

#if PSP_RENDERER_DIAGNOSTICS
void PspStarfield_DiagCounts(u32 requested, u32 traversed) {
    PspStarfieldFrame* frame;

    if (sStarfieldRecordingPool < 0) {
        return;
    }
    frame = &sStarfieldFrames[sStarfieldRecordingPool];
    frame->diagRequested = requested;
    frame->diagTraversed = traversed;
}
#endif

void PspStarfield_PreparePresentation(SPTask* task, SPTask* previousTask, f32 alpha) {
    sStarfieldPresentationPool = psp_starfield_pool_for_task(task);
    sStarfieldPreviousPool = psp_starfield_pool_for_task(previousTask);
    sStarfieldPresentationAlpha = alpha;
}

void PspStarfield_DrawPending(void) {
    const PspStarfieldFrame* current;
    const PspStarfieldFrame* previous = NULL;
    u32 sourceCount;
    u32 out = 0;
    u32 i;
#if PSP_RENDERER_DIAGNOSTICS
    u32 chunks = 0;
#endif

    if (sStarfieldPresentationPool < 0) {
        return;
    }
    current = &sStarfieldFrames[sStarfieldPresentationPool];
    if (!current->valid) {
        return;
    }
    sourceCount = current->sourceCount;
    if (sStarfieldPreviousPool >= 0) {
        previous = &sStarfieldFrames[sStarfieldPreviousPool];
        if (!previous->valid) {
            previous = NULL;
        } else if (sourceCount < previous->sourceCount) {
            sourceCount = previous->sourceCount;
        }
    }

    PspGfxBackend_DrawSolidRect(0.0f, 0.0f, 320.0f, 240.0f, 0xFF000000u, 0, PSP_GFX_VIEWPORT_FULL);

    for (i = 0; i < sourceCount; i++) {
        const PspStar* star;
        PspGfxVertex* vertex;
        u32 color;
        f32 x;
        f32 y;

        if (current->visible[i]) {
            star = &current->stars[i];
            x = star->x;
            y = star->y;
            color = star->color;
            if ((previous != NULL) && previous->visible[i]) {
                /* Screen-space endpoints already include the starfield's
                 * wrapped offset and camera roll. gStarFillColors itself is
                 * static; the 30 Hz star "flicker" is raster aliasing from
                 * integer positions, which smooth motion necessarily reduces. */
                x = previous->stars[i].x + ((x - previous->stars[i].x) * sStarfieldPresentationAlpha);
                y = previous->stars[i].y + ((y - previous->stars[i].y) * sStarfieldPresentationAlpha);
            }
        } else if ((previous != NULL) && previous->visible[i]) {
            star = &previous->stars[i];
            x = star->x;
            y = star->y;
            color = star->color;
        } else {
            continue;
        }

        vertex = &sStarfieldVertices[out];
        vertex[0].u = 0.0f;
        vertex[0].v = 0.0f;
        vertex[0].color = color;
        vertex[0].x = (x / 160.0f) - 1.0f;
        vertex[0].y = 1.0f - (y / 120.0f);
        vertex[0].z = 0.0f;

        vertex[1].u = 0.0f;
        vertex[1].v = 0.0f;
        vertex[1].color = vertex[0].color;
        vertex[1].x = ((x + 1.0f) / 160.0f) - 1.0f;
        vertex[1].y = 1.0f - ((y + 1.0f) / 120.0f);
        vertex[1].z = 0.0f;
        out += PSP_STARFIELD_VERTICES_PER_STAR;

        if (out == PSP_STARFIELD_CHUNK_STARS * PSP_STARFIELD_VERTICES_PER_STAR) {
            PspGfxBackend_DrawSprites(sStarfieldVertices, out, &sStarfieldDrawState);
            out = 0;
#if PSP_RENDERER_DIAGNOSTICS
            chunks++;
#endif
        }
    }

    if (out != 0) {
        PspGfxBackend_DrawSprites(sStarfieldVertices, out, &sStarfieldDrawState);
#if PSP_RENDERER_DIAGNOSTICS
        chunks++;
#endif
    }

#if PSP_RENDERER_DIAGNOSTICS
    psp_starfield_log_diag(current, chunks);
#endif
}
