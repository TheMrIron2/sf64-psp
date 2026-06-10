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

    if ((task != NULL) && (task->task.t.data_ptr != NULL)) {
        dl = (const Gfx*) task->task.t.data_ptr;
        PspGfxDl_Run(dl, taskIndex, NULL);
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
