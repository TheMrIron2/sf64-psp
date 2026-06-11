#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "PR/ultratypes.h"

typedef struct {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
    float u;
    float v;
} PspGfxPspglColorVertex;

typedef enum {
    PSP_GFX_PSPGL_TEX_REPLACE,
    PSP_GFX_PSPGL_TEX_MODULATE,
} PspGfxPspglTextureEnv;

void PspGfxPspgl_Init(void);
void PspGfxPspgl_BeginFrame(void);
u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight);
u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight);
u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight);
void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                      PspGfxPspglTextureEnv textureEnv, int alphaTest, int blend, int depthTest,
                                      int depthWrite);

#endif
