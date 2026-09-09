#ifndef PSP_GFX_GU_TEXTURE_H
#define PSP_GFX_GU_TEXTURE_H

#include "src/psp/gfx/gfx_psp_backend.h"

int PspGfxGuTexture_Init(void);
void PspGfxGuTexture_BeginFrame(void);
void PspGfxGuTexture_Shutdown(void);
int PspGfxGuTexture_Supported(const PspGfxTextureRequest* request);
int PspGfxGuTexture_Find(const PspGfxTextureRequest* request, PspGfxTextureResult* result);
int PspGfxGuTexture_Create(const PspGfxTextureRequest* request, PspGfxTextureResult* result);
void PspGfxGuTexture_InvalidateRgba16(const u16* pixels);
int PspGfxGuTexture_Resolve(PspGfxTextureHandle handle, const void** pixels, u32* width, u32* height);

#endif
