#ifndef PSP_GFX_DEVICE_H
#define PSP_GFX_DEVICE_H

int PspGfxDevice_Init(void);
int PspGfxDevice_IsReady(void);
int PspGfxDevice_BeginFrame(void);
int PspGfxDevice_Submit(void);
int PspGfxDevice_Present(void);
void PspGfxDevice_Shutdown(void);
void* PspGfxDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat);

#endif
