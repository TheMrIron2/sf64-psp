#include "src/psp/gfx/gfx_psp_device.h"

#include "src/psp/gfx/gfx_psp_gu_device.h"

int PspGfxDevice_Init(void) {
    return PspGfxGuDevice_Init();
}

int PspGfxDevice_IsReady(void) {
    return PspGfxGuDevice_IsReady();
}

int PspGfxDevice_BeginFrame(void) {
    return PspGfxGuDevice_BeginFrame();
}

int PspGfxDevice_Submit(void) {
    return PspGfxGuDevice_Submit();
}

int PspGfxDevice_Present(void) {
    return PspGfxGuDevice_Present();
}

void PspGfxDevice_Shutdown(void) {
    PspGfxGuDevice_Shutdown();
}

void* PspGfxDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    return PspGfxGuDevice_GetPresentedFrameBuffer(stride, pixelFormat);
}
