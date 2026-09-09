#include "src/psp/gfx/gfx_psp_device.h"

int PspGfxDevice_Init(void) {
    return 0;
}

int PspGfxDevice_IsReady(void) {
    return 0;
}

int PspGfxDevice_BeginFrame(void) {
    return 0;
}

int PspGfxDevice_Submit(void) {
    return 0;
}

int PspGfxDevice_Present(void) {
    return 0;
}

void PspGfxDevice_Shutdown(void) {
}

void* PspGfxDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    (void) stride;
    (void) pixelFormat;
    return (void*) 0;
}
