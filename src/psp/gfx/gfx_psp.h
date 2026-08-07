#ifndef PSP_GFX_PSP_H
#define PSP_GFX_PSP_H

int PspGfx_Init(void);
int PspGfx_IsReady(void);
void PspGfx_BeginFrame(void);
void PspGfx_EndFrame(void);
void* PspGfx_GetPresentedFrameBuffer(int* stride, int* pixelFormat);
int PspGfx_GetWidth(void);
int PspGfx_GetHeight(void);

#endif
