#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/gfx/gfx_psp_color.h"

#define PSP_GFX_PSPGL_VIEWPORT_AUTO PSP_GFX_VIEWPORT_AUTO
#define PSP_GFX_PSPGL_VIEWPORT_FULL PSP_GFX_VIEWPORT_FULL
#define PSP_GFX_PSPGL_VIEWPORT_CENTERED_UI PSP_GFX_VIEWPORT_CENTERED_UI
#define PSP_GFX_PSPGL_VIEWPORT_WIDE_UI PSP_GFX_VIEWPORT_WIDE_UI
#define PSP_GFX_PSPGL_VIEWPORT_NATIVE_HUD PSP_GFX_VIEWPORT_NATIVE_HUD
#define PSP_GFX_PSPGL_VIEWPORT_HUD_TOP_LEFT PSP_GFX_VIEWPORT_HUD_TOP_LEFT
#define PSP_GFX_PSPGL_VIEWPORT_HUD_TOP_RIGHT PSP_GFX_VIEWPORT_HUD_TOP_RIGHT
#define PSP_GFX_PSPGL_VIEWPORT_HUD_BOTTOM_LEFT PSP_GFX_VIEWPORT_HUD_BOTTOM_LEFT
#define PSP_GFX_PSPGL_VIEWPORT_HUD_BOTTOM_RIGHT PSP_GFX_VIEWPORT_HUD_BOTTOM_RIGHT
#define PSP_GFX_PSPGL_VIEWPORT_HUD_TOP_CENTER PSP_GFX_VIEWPORT_HUD_TOP_CENTER
#define PSP_GFX_PSPGL_VIEWPORT_HUD_SCALED_TOP_LEFT PSP_GFX_VIEWPORT_HUD_SCALED_TOP_LEFT
#define PSP_GFX_PSPGL_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT PSP_GFX_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT

typedef PspGfxVertex PspGfxPspglColorVertex;
typedef PspGfxFogVertex PspGfxPspglFogVertex;

typedef struct {
    PspGfxPspglColorVertex* vertices;
    u32 firstVertex;
    u32 capacity;
} PspGfxPspglVertexReservation;

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
void PspGfxPspgl_SetMirrorEncoding(int mirrorS, int mirrorT);
int PspGfxPspgl_CanMirrorEncode(u32 width, u32 height, int mirrorS, int mirrorT);
int PspGfxPspgl_MirrorEncodingFailed(void);
void PspGfxPspgl_InvalidateRgba16Texture(const u16* pixels);
void PspGfxPspgl_SetScissor(float ulx, float uly, float lrx, float lry);
void PspGfxPspgl_ClearScissor(void);
int PspGfxPspgl_FindCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight);
u32 PspGfxPspgl_CreateCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
int PspGfxPspgl_FindCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight,
                               u32* uploadX, u32* uploadY);
u32 PspGfxPspgl_CreateCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight, u32* uploadX, u32* uploadY,
                                 PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight, u32* uploadX, u32* uploadY,
                              PspGfxPspglTextureRef* textureRef);
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
int PspGfxPspgl_FindIa8SoftCoverageTexture(const u8* pixels, u32 width, u32 height, u32* textureId,
                                           u32* uploadWidth, u32* uploadHeight,
                                           PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_CreateIa8SoftCoverageTexture(const u8* pixels, u32 width, u32 height, u32* uploadWidth,
                                             u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
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
int PspGfxPspgl_FindIa16SoftCoverageTexture(const u16* pixels, u32 width, u32 height, u32* textureId,
                                            u32* uploadWidth, u32* uploadHeight,
                                            PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_CreateIa16SoftCoverageTexture(const u16* pixels, u32 width, u32 height, u32* uploadWidth,
                                              u32* uploadHeight, PspGfxPspglTextureRef* textureRef);
u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                               PspGfxPspglTextureRef* textureRef);
void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                      PspGfxPspglTextureRef textureRef, PspGfxPspglTextureEnv textureEnv,
                                      u32 textureEnvColor, PspGfxPspglTextureWrap wrapS,
                                      PspGfxPspglTextureWrap wrapT, int alphaTest, int blend, int premultiplied,
                                      int depthTest, int depthWrite, int fog, const float* fogColor, float fogStart,
                                      float fogEnd,
                                      const float* projectionMatrix, u32 projectionSerial, int pretransformed,
                                      int pointFilter);
int PspGfxPspgl_ReserveColoredVertices(u32 vertexCapacity, PspGfxPspglVertexReservation* reservation);
void PspGfxPspgl_DrawReservedColoredTriangles(const PspGfxPspglVertexReservation* reservation, u32 vertexCount,
                                              u32 textureId, PspGfxPspglTextureRef textureRef,
                                              PspGfxPspglTextureEnv textureEnv, u32 textureEnvColor,
                                              PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT,
                                              int alphaTest, int blend, int premultiplied, int depthTest,
                                              int depthWrite, int fog, const float* fogColor, float fogStart,
                                              float fogEnd, const float* projectionMatrix, u32 projectionSerial,
                                              int pretransformed, int pointFilter);
void PspGfxPspgl_DrawFogTriangles(const PspGfxPspglFogVertex* vertices, u32 vertexCount,
                                  const float* projectionMatrix, u32 projectionSerial,
                                  int pretransformed, int restoreDepthTest, int restoreDepthWrite,
                                  u32 restoreTextureId,
                                  const PspGfxPspglColorVertex* restoreVertices);
void PspGfxPspgl_DrawColoredSprites(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                    PspGfxPspglTextureRef textureRef, PspGfxPspglTextureEnv textureEnv,
                                    u32 textureEnvColor, PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT,
                                    int alphaTest, int blend, int premultiplied, int depthTest, int depthWrite, int fog,
                                    const float* fogColor, float fogStart, float fogEnd,
                                    const float* projectionMatrix, u32 projectionSerial, int pretransformed,
                                    int pointFilter, int uiViewport);
void PspGfxPspgl_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend,
                               int fullViewport);
void PspGfxPspgl_SetViewportPolicy(int uiViewport);
void PspGfxPspgl_SetHudAnchor(s16 x, s16 y);
void PspGfxPspgl_BeginReplayCache(void);
void PspGfxPspgl_EndReplayCache(void);
void PspGfxPspgl_ReplayCache(void);
int PspGfxPspgl_ReplayCacheReady(void);
void PspGfxPspgl_ReplayCacheInvalidate(void);

#endif
