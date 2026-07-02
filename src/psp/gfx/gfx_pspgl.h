#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "PR/ultratypes.h"

#include <stddef.h>

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
typedef char PspGfxPspglColorVertexUOffsetCheck[(offsetof(PspGfxPspglColorVertex, u) == 0) ? 1 : -1];
typedef char PspGfxPspglColorVertexVOffsetCheck[(offsetof(PspGfxPspglColorVertex, v) == 4) ? 1 : -1];
typedef char PspGfxPspglColorVertexColorOffsetCheck[(offsetof(PspGfxPspglColorVertex, color) == 8) ? 1 : -1];
typedef char PspGfxPspglColorVertexXOffsetCheck[(offsetof(PspGfxPspglColorVertex, x) == 12) ? 1 : -1];
typedef char PspGfxPspglColorVertexYOffsetCheck[(offsetof(PspGfxPspglColorVertex, y) == 16) ? 1 : -1];
typedef char PspGfxPspglColorVertexZOffsetCheck[(offsetof(PspGfxPspglColorVertex, z) == 20) ? 1 : -1];

typedef enum {
    PSP_GFX_PSPGL_TEX_REPLACE,
    PSP_GFX_PSPGL_TEX_MODULATE,
    PSP_GFX_PSPGL_TEX_BLEND,
} PspGfxPspglTextureEnv;

typedef enum {
    PSP_GFX_PSPGL_WRAP_CLAMP,
    PSP_GFX_PSPGL_WRAP_REPEAT,
    PSP_GFX_PSPGL_WRAP_MIRROR,
} PspGfxPspglTextureWrap;

typedef struct PspGfxPspglTextureParameterState PspGfxPspglTextureParameterState;

typedef struct {
    PspGfxPspglTextureParameterState* state;
    u32 texture;
    u32 generation;
} PspGfxPspglTextureRef;

void PspGfxPspgl_Init(void);
void PspGfxPspgl_BeginFrame(void);
void PspGfxPspgl_Flush(void);
int PspGfxPspgl_TextureExceedsPixelBudget(u32 width, u32 height);
int PspGfxPspgl_FindCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_CreateCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_CreateCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* textureId,
                                  PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_CreateRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                    u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindIa8Texture(const u8* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                               u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_CreateIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                                 PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                              PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindIa16Texture(const u16* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                                u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_CreateIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                                  PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                               PspGfxPspglTextureRef* textureRef);
void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                      PspGfxPspglTextureRef textureRef, PspGfxPspglTextureEnv textureEnv,
                                      u32 textureEnvColor, PspGfxPspglTextureWrap wrapS,
                                      PspGfxPspglTextureWrap wrapT, int alphaTest, int blend, int premultiplied,
                                      int depthTest, int depthWrite, int fog, const float* fogColor, float fogStart,
                                      float fogEnd,
                                      const float* projectionMatrix, int pretransformed, int textureExpected);
void PspGfxPspgl_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend);

#endif
