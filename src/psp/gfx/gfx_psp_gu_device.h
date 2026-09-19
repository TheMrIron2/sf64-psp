#ifndef PSP_GFX_GU_DEVICE_H
#define PSP_GFX_GU_DEVICE_H

#include "PR/ultratypes.h"

int PspGfxGuDevice_Init(void);
int PspGfxGuDevice_IsReady(void);
int PspGfxGuDevice_BeginFrame(void);
int PspGfxGuDevice_Submit(void);
int PspGfxGuDevice_Present(void);
void PspGfxGuDevice_Shutdown(void);
void* PspGfxGuDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat);
int PspGfxGuDevice_GetTextureEdramArena(void** base, u32* bytes);

#endif
