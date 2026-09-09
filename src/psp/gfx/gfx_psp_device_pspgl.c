#include "src/psp/gfx/gfx_psp_device.h"

#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/gfx/gfx_pspgl_device.h"

int PspGfxDevice_Init(void) {
    if (!PspGfxPspglDevice_Init()) {
        return 0;
    }

    PspGfxPspgl_Init();
    return 1;
}

int PspGfxDevice_IsReady(void) {
    return PspGfxPspglDevice_IsReady();
}

int PspGfxDevice_BeginFrame(void) {
    if (!PspGfxDevice_IsReady()) {
        return 0;
    }

    PspGfxPspglDevice_BeginFrame();
    PspGfxPspgl_BeginFrame();
    return 1;
}

int PspGfxDevice_Submit(void) {
    if (!PspGfxDevice_IsReady()) {
        return 0;
    }

    PspGfxPspgl_Flush();
    return 1;
}

int PspGfxDevice_Present(void) {
    if (!PspGfxDevice_IsReady()) {
        return 0;
    }

    return PspGfxPspglDevice_EndFrame();
}

void PspGfxDevice_Shutdown(void) {
    PspGfxPspglDevice_Shutdown();
}

void* PspGfxDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    return PspGfxPspglDevice_GetPresentedFrameBuffer(stride, pixelFormat);
}
