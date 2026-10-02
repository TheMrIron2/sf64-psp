#ifndef PSP_GFX_DEVICE_H
#define PSP_GFX_DEVICE_H

int PspGfxDevice_Init(void);
int PspGfxDevice_IsReady(void);
int PspGfxDevice_BeginFrame(void);
int PspGfxDevice_Submit(void);
int PspGfxDevice_Present(void);
void PspGfxDevice_Shutdown(void);
// Query after submission on GU and after presentation on PSPGL
void* PspGfxDevice_GetOverlayFrameBuffer(int* stride, int* pixelFormat);

#endif
