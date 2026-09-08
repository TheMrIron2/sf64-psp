#ifndef PSP_GFX_PSPGL_DEVICE_H
#define PSP_GFX_PSPGL_DEVICE_H

int PspGfxPspglDevice_Init(void);
int PspGfxPspglDevice_IsReady(void);
void PspGfxPspglDevice_BeginFrame(void);
void PspGfxPspglDevice_EndFrame(void);
void* PspGfxPspglDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat);

#endif
