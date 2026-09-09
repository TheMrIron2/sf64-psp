#ifndef PSP_GFX_BACKEND_H
#define PSP_GFX_BACKEND_H

#include "PR/ultratypes.h"

#include <stddef.h>

// Native GE vertex order
typedef struct {
    float u;
    float v;
    u32 color;
    float x;
    float y;
    float z;
} PspGfxVertex;

typedef char PspGfxVertexSizeCheck[(sizeof(PspGfxVertex) == 24) ? 1 : -1];
typedef char PspGfxVertexUOffsetCheck[(offsetof(PspGfxVertex, u) == 0) ? 1 : -1];
typedef char PspGfxVertexVOffsetCheck[(offsetof(PspGfxVertex, v) == 4) ? 1 : -1];
typedef char PspGfxVertexColorOffsetCheck[(offsetof(PspGfxVertex, color) == 8) ? 1 : -1];
typedef char PspGfxVertexXOffsetCheck[(offsetof(PspGfxVertex, x) == 12) ? 1 : -1];
typedef char PspGfxVertexYOffsetCheck[(offsetof(PspGfxVertex, y) == 16) ? 1 : -1];
typedef char PspGfxVertexZOffsetCheck[(offsetof(PspGfxVertex, z) == 20) ? 1 : -1];

typedef struct {
    u32 color;
    float x;
    float y;
    float z;
} PspGfxFogVertex;

typedef char PspGfxFogVertexSizeCheck[(sizeof(PspGfxFogVertex) == 16) ? 1 : -1];

// Texture handles may expire when later requests replace cache storage
typedef struct {
    u32 opaque[4];
} PspGfxTextureHandle;

typedef char PspGfxTextureHandleSizeCheck[(sizeof(PspGfxTextureHandle) == 16) ? 1 : -1];

static inline PspGfxTextureHandle PspGfxTextureHandle_Null(void) {
    PspGfxTextureHandle handle = { { 0, 0, 0, 0 } };

    return handle;
}

static inline int PspGfxTextureHandle_IsValid(PspGfxTextureHandle handle) {
    return (handle.opaque[0] | handle.opaque[1] | handle.opaque[2] | handle.opaque[3]) != 0;
}

static inline int PspGfxTextureHandle_Equals(PspGfxTextureHandle a, PspGfxTextureHandle b) {
    return (a.opaque[0] == b.opaque[0]) && (a.opaque[1] == b.opaque[1]) && (a.opaque[2] == b.opaque[2]) &&
           (a.opaque[3] == b.opaque[3]);
}

u32 PspGfxBackend_TextureDebugId(PspGfxTextureHandle handle);
u32 PspGfxBackend_TextureDebugGeneration(PspGfxTextureHandle handle);

typedef struct {
    u32 opaque;
} PspGfxReservationToken;

typedef struct {
    PspGfxVertex* vertices;
    PspGfxReservationToken token;
    u32 capacity;
} PspGfxVertexReservation;

typedef enum {
    PSP_GFX_TEX_REPLACE,
    PSP_GFX_TEX_MODULATE,
    PSP_GFX_TEX_BLEND,
} PspGfxTextureEnv;

typedef enum {
    PSP_GFX_WRAP_CLAMP,
    PSP_GFX_WRAP_REPEAT,
    PSP_GFX_WRAP_MIRROR,
} PspGfxTextureWrap;

typedef enum {
    PSP_GFX_TEXTURE_CI8,
    PSP_GFX_TEXTURE_CI4,
    PSP_GFX_TEXTURE_RGBA16,
    PSP_GFX_TEXTURE_RGBA32,
    PSP_GFX_TEXTURE_IA8,
    PSP_GFX_TEXTURE_IA16,
} PspGfxTextureFormat;

typedef struct {
    PspGfxTextureFormat format;
    const void* pixels;
    const u16* palette;
    u32 width;
    u32 height;
    u8 premultiply;
    u8 softCoverage;
    u8 envBlend;
    u8 mirrorS;
    u8 mirrorT;
    u32 primitiveColor;
    u32 environmentColor;
} PspGfxTextureRequest;

typedef enum {
    PSP_GFX_TEXTURE_CACHE_MISS,
    PSP_GFX_TEXTURE_CACHE_HIT,
    PSP_GFX_TEXTURE_CACHE_CREATED,
    PSP_GFX_TEXTURE_CACHE_FAILED,
} PspGfxTextureCacheResult;

typedef struct {
    PspGfxTextureHandle handle;
    u32 uploadWidth;
    u32 uploadHeight;
    u32 uploadX;
    u32 uploadY;
    PspGfxTextureCacheResult cacheResult;
} PspGfxTextureResult;

typedef enum {
    PSP_GFX_VIEWPORT_AUTO = -1,
    PSP_GFX_VIEWPORT_FULL,
    PSP_GFX_VIEWPORT_CENTERED_UI,
    PSP_GFX_VIEWPORT_WIDE_UI,
    PSP_GFX_VIEWPORT_NATIVE_HUD,
    PSP_GFX_VIEWPORT_HUD_TOP_LEFT,
    PSP_GFX_VIEWPORT_HUD_TOP_RIGHT,
    PSP_GFX_VIEWPORT_HUD_BOTTOM_LEFT,
    PSP_GFX_VIEWPORT_HUD_BOTTOM_RIGHT,
    PSP_GFX_VIEWPORT_HUD_TOP_CENTER,
    PSP_GFX_VIEWPORT_HUD_SCALED_TOP_LEFT,
    PSP_GFX_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT,
} PspGfxViewportPolicy;

typedef struct {
    PspGfxTextureHandle texture;
    PspGfxTextureEnv textureEnv;
    u32 textureEnvColor;
    PspGfxTextureWrap wrapS;
    PspGfxTextureWrap wrapT;
    int alphaTest;
    int blend;
    int premultiplied;
    int depthTest;
    int depthWrite;
    int fog;
    const float* fogColor;
    float fogStart;
    float fogEnd;
    const float* projectionMatrix;
    u32 projectionSerial;
    int pretransformed;
    int pointFilter;
    PspGfxViewportPolicy viewport;
} PspGfxDrawState;

typedef struct {
    const float* projectionMatrix;
    u32 projectionSerial;
    int pretransformed;
    int depthTest;
    PspGfxViewportPolicy viewport;
} PspGfxFogDrawState;

int PspGfxBackend_TextureSupported(const PspGfxTextureRequest* request);
int PspGfxBackend_FindTexture(const PspGfxTextureRequest* request, PspGfxTextureResult* result);
int PspGfxBackend_CreateTexture(const PspGfxTextureRequest* request, PspGfxTextureResult* result);
void PspGfxBackend_InvalidateRgba16Texture(const u16* pixels);
void PspGfxBackend_SetScissor(float ulx, float uly, float lrx, float lry);
void PspGfxBackend_ClearScissor(void);

void PspGfxBackend_DrawTriangles(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state);
int PspGfxBackend_ReserveVertices(u32 vertexCapacity, PspGfxVertexReservation* reservation);
void PspGfxBackend_DrawReservedTriangles(const PspGfxVertexReservation* reservation, u32 vertexCount,
                                         const PspGfxDrawState* state);
void PspGfxBackend_DrawFogTriangles(const PspGfxFogVertex* vertices, u32 vertexCount, const PspGfxFogDrawState* state);
void PspGfxBackend_DrawSprites(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state);
void PspGfxBackend_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend,
                                 PspGfxViewportPolicy viewport);
void PspGfxBackend_SetHudAnchor(s16 x, s16 y);
// Backends must invalidate replay when captured resources expire
void PspGfxBackend_BeginReplayCache(void);
void PspGfxBackend_EndReplayCache(void);
void PspGfxBackend_ReplayCache(void);

#endif
