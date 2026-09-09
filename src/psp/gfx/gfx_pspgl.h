#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/gfx/gfx_psp_color.h"

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
int PspGfxPspgl_TextureSupported(const PspGfxTextureRequest* request);
int PspGfxPspgl_FindTexture(const PspGfxTextureRequest* request, u32* textureId, PspGfxPspglTextureRef* textureRef,
                            u32* uploadWidth, u32* uploadHeight, u32* uploadX, u32* uploadY);
u32 PspGfxPspgl_CreateTexture(const PspGfxTextureRequest* request, PspGfxPspglTextureRef* textureRef, u32* uploadWidth,
                              u32* uploadHeight, u32* uploadX, u32* uploadY);
void PspGfxPspgl_InvalidateRgba16Texture(const u16* pixels);
void PspGfxPspgl_SetScissor(float ulx, float uly, float lrx, float lry);
void PspGfxPspgl_ClearScissor(void);
void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                      PspGfxPspglTextureRef textureRef, PspGfxPspglTextureEnv textureEnv,
                                      u32 textureEnvColor, PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT,
                                      int alphaTest, int blend, int premultiplied, int depthTest, int depthWrite,
                                      int fog, const float* fogColor, float fogStart, float fogEnd,
                                      const float* projectionMatrix, u32 projectionSerial, int pretransformed,
                                      int pointFilter, int uiViewport);
int PspGfxPspgl_ReserveColoredVertices(u32 vertexCapacity, PspGfxPspglVertexReservation* reservation);
void PspGfxPspgl_DrawReservedColoredTriangles(const PspGfxPspglVertexReservation* reservation, u32 vertexCount,
                                              u32 textureId, PspGfxPspglTextureRef textureRef,
                                              PspGfxPspglTextureEnv textureEnv, u32 textureEnvColor,
                                              PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT, int alphaTest,
                                              int blend, int premultiplied, int depthTest, int depthWrite, int fog,
                                              const float* fogColor, float fogStart, float fogEnd,
                                              const float* projectionMatrix, u32 projectionSerial, int pretransformed,
                                              int pointFilter, int uiViewport);
void PspGfxPspgl_DrawFogTriangles(const PspGfxPspglFogVertex* vertices, u32 vertexCount, const float* projectionMatrix,
                                  u32 projectionSerial, int pretransformed, int depthTest, int uiViewport);
void PspGfxPspgl_DrawColoredSprites(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId,
                                    PspGfxPspglTextureRef textureRef, PspGfxPspglTextureEnv textureEnv,
                                    u32 textureEnvColor, PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT,
                                    int alphaTest, int blend, int premultiplied, int depthTest, int depthWrite, int fog,
                                    const float* fogColor, float fogStart, float fogEnd, const float* projectionMatrix,
                                    u32 projectionSerial, int pretransformed, int pointFilter, int uiViewport);
void PspGfxPspgl_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend, int uiViewport);
void PspGfxPspgl_SetHudAnchor(s16 x, s16 y);
void PspGfxPspgl_BeginReplayCache(void);
void PspGfxPspgl_EndReplayCache(void);
void PspGfxPspgl_ReplayCache(void);
int PspGfxPspgl_ReplayCacheReady(void);
void PspGfxPspgl_ReplayCacheInvalidate(void);

#endif
