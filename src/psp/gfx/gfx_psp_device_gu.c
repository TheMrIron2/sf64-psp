#include "src/psp/gfx/gfx_psp_device.h"

#include "src/psp/gfx/gfx_psp_gu_device.h"
#include "src/psp/gfx/gfx_psp_gu_texture.h"

int PspGfxDevice_Init(void) {
    if (!PspGfxGuDevice_Init()) {
        return 0;
    }
    if (!PspGfxGuTexture_Init()) {
        PspGfxGuDevice_Shutdown();
        return 0;
    }
    return 1;
}

int PspGfxDevice_IsReady(void) {
    return PspGfxGuDevice_IsReady();
}

int PspGfxDevice_BeginFrame(void) {
    if (!PspGfxGuDevice_BeginFrame()) {
        return 0;
    }
    PspGfxGuTexture_BeginFrame();
    return 1;
}

int PspGfxDevice_Submit(void) {
    return PspGfxGuDevice_Submit();
}

int PspGfxDevice_Present(void) {
    return PspGfxGuDevice_Present();
}

void PspGfxDevice_Shutdown(void) {
    PspGfxGuDevice_Shutdown();
    PspGfxGuTexture_Shutdown();
}

void* PspGfxDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    return PspGfxGuDevice_GetPresentedFrameBuffer(stride, pixelFormat);
}
