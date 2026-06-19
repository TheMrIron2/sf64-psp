#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "PR/ultratypes.h"

typedef struct {
    // native PSP GE vertex order
    float u;
    float v;
    u32 color;
    float x;
    float y;
    float z;
} PspGfxPspglColorVertex;

typedef char PspGfxPspglColorVertexSizeCheck[
    (sizeof(PspGfxPspglColorVertex) == 24) ? 1 : -1
];

typedef enum {
    PSP_GFX_PSPGL_TEX_REPLACE,
    PSP_GFX_PSPGL_TEX_MODULATE,
} PspGfxPspglTextureEnv;

typedef enum {
    PSP_GFX_PSPGL_WRAP_CLAMP,
    PSP_GFX_PSPGL_WRAP_REPEAT,
    PSP_GFX_PSPGL_WRAP_MIRROR,
} PspGfxPspglTextureWrap;

void PspGfxPspgl_Init(void);
void PspGfxPspgl_BeginFrame(void);
u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight);
u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight);
u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight);
u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight);
void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                      PspGfxPspglTextureEnv textureEnv, PspGfxPspglTextureWrap wrapS,
                                      PspGfxPspglTextureWrap wrapT, int alphaTest, int blend, int premultiplied,
                                      int depthTest, int depthWrite, int fog, const float* fogColor, float fogStart,
                                      float fogEnd, const float* projectionMatrix, int pretransformed);

#endif
