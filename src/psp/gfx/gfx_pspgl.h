#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "PR/ultratypes.h"

#include <stddef.h>

/*
 * Renderer colour-transfer policy, matching sf64-dc (gfx_retro_dc.c): a
 * square-root transfer table256[i] = 255 * sqrt(i / 255) is applied exactly
 * once to every RGB input the fixed-function combine consumes -- vertex
 * shade colours, texture RGB at upload, primitive/environment/fog/fill
 * colours. Alpha is never transformed.
 *
 *   SF64_PSP_COLOR_TRANSFER=0  raw behaviour (transfer applied only to
 *                              calculated lighting, as before)
 *   SF64_PSP_COLOR_TRANSFER=1  sf64-dc square-root policy
 *
 * The mode is compile-time only: texture caches hold transformed texels, so
 * toggling requires a rebuild rather than runtime cache invalidation.
 */
#ifndef SF64_PSP_COLOR_TRANSFER
#define SF64_PSP_COLOR_TRANSFER 1
#endif

/* 255 * sqrt(i / 255) table; always built (calculated lighting uses it even
 * when SF64_PSP_COLOR_TRANSFER is 0). */
extern u8 gPspGfxColorTransferLut[256];

void PspGfxPspgl_InitColorTransfer(void);

/* Policy-gated RGB transfer for everything other than calculated lighting. */
static inline u8 psp_gfx_color_transfer_u8(u8 value) {
#if SF64_PSP_COLOR_TRANSFER
    return gPspGfxColorTransferLut[value];
#else
    return value;
#endif
}

/* N64 RGBA5551 fill colour to the renderer vertex layout 0xAABBGGRR: 5-bit
 * channels expand to 8 bits, RGB carries the transfer policy exactly once,
 * the alpha bit expands to 0/255 untransformed. */
static inline u32 psp_gfx_rgba5551_to_abgr8888(u16 color) {
    u32 r5 = (color >> 11) & 0x1F;
    u32 g5 = (color >> 6) & 0x1F;
    u32 b5 = (color >> 1) & 0x1F;
    u32 r = psp_gfx_color_transfer_u8((u8) ((r5 << 3) | (r5 >> 2)));
    u32 g = psp_gfx_color_transfer_u8((u8) ((g5 << 3) | (g5 >> 2)));
    u32 b = psp_gfx_color_transfer_u8((u8) ((b5 << 3) | (b5 >> 2)));
    u32 a = (color & 1U) ? 255U : 0U;

    return r | (g << 8) | (b << 16) | (a << 24);
}

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
int PspGfxPspgl_FindRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply, u32* textureId,
                                  PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_CreateRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                    u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindRgba32EnvBlendTexture(const void* pixels, u32 width, u32 height, u32 primitiveColor,
                                          u32 environmentColor, u32* textureId,
                                          PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_CreateRgba32EnvBlendTexture(const void* pixels, u32 width, u32 height, u32 primitiveColor,
                                            u32 environmentColor, u32* uploadWidth, u32* uploadHeight,
                                            PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindIa8Texture(const u8* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                               u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_CreateIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                                 PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                              PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindIa8EnvBlendTexture(const u8* pixels, u32 width, u32 height, u32 primitiveColor,
                                       u32 environmentColor, u32* textureId, u32* uploadWidth, u32* uploadHeight,
                                       PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_CreateIa8EnvBlendTexture(const u8* pixels, u32 width, u32 height, u32 primitiveColor,
                                         u32 environmentColor, u32* uploadWidth, u32* uploadHeight,
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
                                      const float* projectionMatrix, int pretransformed);
void PspGfxPspgl_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend);

#endif
