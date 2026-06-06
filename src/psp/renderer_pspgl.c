#include "PR/ultratypes.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_psp_dl.h"
#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"
#include "src/psp/renderer.h"

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
    PspGfxPspgl_DrawTestTriangle();
    PspGfx_EndFrame();
}

void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    PspGfxDlStats stats;
    const Gfx* dl;

    if (!PspGfx_IsReady()) {
        PspRenderer_Init();
    }
    if (!PspGfx_IsReady()) {
        return;
    }

    PspGfx_BeginFrame();
    PspGfxPspgl_BeginFrame();

    if ((task == NULL) || (task->task.t.data_ptr == NULL)) {
        PspGfxPspgl_DrawTestTriangle();
    } else {
        dl = (const Gfx*) task->task.t.data_ptr;
        if (!PspGfxDl_Run(dl, taskIndex, &stats) || (stats.drawVertexCount == 0)) {
            PspPlatform_LogLine("[pspgl] no drawable display-list triangles, drawing fallback triangle");
            PspGfxPspgl_DrawTestTriangle();
        }
    }

    PspGfx_EndFrame();
}

void PspRenderer_BeginStarfield(void) {
}

void PspRenderer_AddStar(s16 x, s16 y, u32 n64FillColor) {
    (void) x;
    (void) y;
    (void) n64FillColor;
}

void PspRenderer_EndStarfield(void) {
}
