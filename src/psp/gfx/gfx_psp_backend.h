#ifndef PSP_GFX_BACKEND_H
#define PSP_GFX_BACKEND_H

#include "PR/ultratypes.h"

#include <stddef.h>

#if PSP_GFX_BACKEND_GU && PSP_GFX_BACKEND_PSPGL
#define PSP_GFX_BACKEND_SELECTABLE 1
#else
#define PSP_GFX_BACKEND_SELECTABLE 0
#endif

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

typedef enum {
    PSP_GFX_KIND_GU,
    PSP_GFX_KIND_PSPGL,
} PspGfxBackendKind;

typedef struct {
    u32 (*TextureDebugId)(PspGfxTextureHandle handle);
    u32 (*TextureDebugGeneration)(PspGfxTextureHandle handle);
    int (*TextureSupported)(const PspGfxTextureRequest* request);
    int (*FindTexture)(const PspGfxTextureRequest* request, PspGfxTextureResult* result);
    int (*CreateTexture)(const PspGfxTextureRequest* request, PspGfxTextureResult* result);
    void (*InvalidateRgba16Texture)(const u16* pixels);
    void (*SetScissor)(float ulx, float uly, float lrx, float lry);
    void (*ClearScissor)(void);
    void (*DrawTriangles)(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state);
    int (*ReserveVertices)(u32 vertexCapacity, PspGfxVertexReservation* reservation);
    void (*DrawReservedTriangles)(const PspGfxVertexReservation* reservation, u32 vertexCount,
                                 const PspGfxDrawState* state);
    void (*DrawFogTriangles)(const PspGfxFogVertex* vertices, u32 vertexCount, const PspGfxFogDrawState* state);
    void (*DrawSprites)(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state);
    void (*DrawSolidRect)(float ulx, float uly, float lrx, float lry, u32 color, int blend,
                          PspGfxViewportPolicy viewport);
    void (*SetHudAnchor)(s16 x, s16 y);
    void (*BeginReplayCache)(void);
    void (*EndReplayCache)(void);
    int (*ReplayCacheReady)(void);
    void (*ReplayCacheInvalidate)(void);
    void (*ReplayCache)(void);
} PspGfxBackendOps;

extern const PspGfxBackendOps* gPspGfxBackendOps;
extern PspGfxBackendKind gPspGfxBackendKind;

int PspGfxBackend_Select(PspGfxBackendKind backend);
void PspGfxBackend_LockSelection(void);
PspGfxBackendKind PspGfxBackend_GetActive(void);
PspGfxBackendKind PspGfxBackend_GetBuildDefault(void);
const char* PspGfxBackend_GetName(void);

static inline int PspGfxBackend_IsGu(void) {
    return gPspGfxBackendKind == PSP_GFX_KIND_GU;
}

#if defined(PSP_GFX_BACKEND_DIRECT_CALLS) && PSP_GFX_BACKEND_GU && !PSP_GFX_BACKEND_PSPGL
#define PSP_GFX_BACKEND_RAW_CALL(name, arguments) PspGfxBackendGu_##name arguments
#elif defined(PSP_GFX_BACKEND_DIRECT_CALLS) && PSP_GFX_BACKEND_PSPGL && !PSP_GFX_BACKEND_GU
#define PSP_GFX_BACKEND_RAW_CALL(name, arguments) PspGfxBackendPspgl_##name arguments
#elif PSP_GFX_BACKEND_GU && PSP_GFX_BACKEND_PSPGL
#define PSP_GFX_BACKEND_RAW_CALL(name, arguments) gPspGfxBackendOps->name arguments
#endif

#if PROFILE_CENSUS_DISPATCH
void PspCensus_Dispatch(unsigned index);
#define PSP_GFX_BACKEND_CALL(name, arguments) \
    (PspCensus_Dispatch(offsetof(PspGfxBackendOps, name) / sizeof(void (*)(void))), \
     PSP_GFX_BACKEND_RAW_CALL(name, arguments))
#else
#define PSP_GFX_BACKEND_CALL(name, arguments) PSP_GFX_BACKEND_RAW_CALL(name, arguments)
#endif

#if defined(PSP_GFX_BACKEND_DIRECT_CALLS) || (PSP_GFX_BACKEND_GU && PSP_GFX_BACKEND_PSPGL)
#define PspGfxBackend_TextureDebugId(handle) (PSP_GFX_BACKEND_CALL(TextureDebugId, ((handle))))
#define PspGfxBackend_TextureDebugGeneration(handle) (PSP_GFX_BACKEND_CALL(TextureDebugGeneration, ((handle))))
#define PspGfxBackend_TextureSupported(request) (PSP_GFX_BACKEND_CALL(TextureSupported, ((request))))
#define PspGfxBackend_FindTexture(request, result) (PSP_GFX_BACKEND_CALL(FindTexture, ((request), (result))))
#define PspGfxBackend_CreateTexture(request, result) (PSP_GFX_BACKEND_CALL(CreateTexture, ((request), (result))))
#define PspGfxBackend_InvalidateRgba16Texture(pixels) \
    (PSP_GFX_BACKEND_CALL(InvalidateRgba16Texture, ((pixels))))
#define PspGfxBackend_SetScissor(ulx, uly, lrx, lry) \
    (PSP_GFX_BACKEND_CALL(SetScissor, ((ulx), (uly), (lrx), (lry))))
#define PspGfxBackend_ClearScissor() (PSP_GFX_BACKEND_CALL(ClearScissor, ()))
#define PspGfxBackend_DrawTriangles(vertices, count, state) \
    (PSP_GFX_BACKEND_CALL(DrawTriangles, ((vertices), (count), (state))))
#define PspGfxBackend_ReserveVertices(capacity, reservation) \
    (PSP_GFX_BACKEND_CALL(ReserveVertices, ((capacity), (reservation))))
#define PspGfxBackend_DrawReservedTriangles(reservation, count, state) \
    (PSP_GFX_BACKEND_CALL(DrawReservedTriangles, ((reservation), (count), (state))))
#define PspGfxBackend_DrawFogTriangles(vertices, count, state) \
    (PSP_GFX_BACKEND_CALL(DrawFogTriangles, ((vertices), (count), (state))))
#define PspGfxBackend_DrawSprites(vertices, count, state) \
    (PSP_GFX_BACKEND_CALL(DrawSprites, ((vertices), (count), (state))))
#define PspGfxBackend_DrawSolidRect(ulx, uly, lrx, lry, color, blend, viewport) \
    (PSP_GFX_BACKEND_CALL(DrawSolidRect, ((ulx), (uly), (lrx), (lry), (color), (blend), (viewport))))
#define PspGfxBackend_SetHudAnchor(x, y) (PSP_GFX_BACKEND_CALL(SetHudAnchor, ((x), (y))))
#define PspGfxBackend_BeginReplayCache() (PSP_GFX_BACKEND_CALL(BeginReplayCache, ()))
#define PspGfxBackend_EndReplayCache() (PSP_GFX_BACKEND_CALL(EndReplayCache, ()))
#define PspGfxBackend_ReplayCacheReady() (PSP_GFX_BACKEND_CALL(ReplayCacheReady, ()))
#define PspGfxBackend_ReplayCacheInvalidate() (PSP_GFX_BACKEND_CALL(ReplayCacheInvalidate, ()))
#define PspGfxBackend_ReplayCache() (PSP_GFX_BACKEND_CALL(ReplayCache, ()))
#else
u32 PspGfxBackend_TextureDebugId(PspGfxTextureHandle handle);
u32 PspGfxBackend_TextureDebugGeneration(PspGfxTextureHandle handle);
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
void PspGfxBackend_DrawFogTriangles(const PspGfxFogVertex* vertices, u32 vertexCount,
                                    const PspGfxFogDrawState* state);
void PspGfxBackend_DrawSprites(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state);
void PspGfxBackend_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend,
                                 PspGfxViewportPolicy viewport);
void PspGfxBackend_SetHudAnchor(s16 x, s16 y);
void PspGfxBackend_BeginReplayCache(void);
void PspGfxBackend_EndReplayCache(void);
int PspGfxBackend_ReplayCacheReady(void);
void PspGfxBackend_ReplayCacheInvalidate(void);
void PspGfxBackend_ReplayCache(void);
#endif

#if PSP_GFX_BACKEND_GU
typedef struct {
    float u, v;
    u32 color;
    s8 nx, ny, nz, pad;
    float x, y, z;
} PspGfxGuLitVertex;

typedef char PspGfxGuLitVertexLayoutCheck[(sizeof(PspGfxGuLitVertex) == 28 &&
    offsetof(PspGfxGuLitVertex, nx) == 12 && offsetof(PspGfxGuLitVertex, x) == 16) ? 1 : -1];

typedef struct {
    float x, y, z;
    u32 diffuse;
} PspGfxGuMeshLight;

typedef struct {
    u32 ambient;
    u32 count;
    int powered;
    PspGfxGuMeshLight lights[4];
} PspGfxGuMeshLighting;

typedef struct {
    float scaleS, scaleT, offsetS, offsetT;
} PspGfxGuMeshUv;

// Each allocation starts one model instance shared by the following mesh spans
void* PspGfxBackendGu_AllocateMeshVertices(u32 count, u32 draws, u32 vertexSize);
int PspGfxBackendGu_BeginStaticMesh(u32 draws);
int PspGfxBackendGu_MeshSideClipSupported(PspGfxViewportPolicy viewport);
void PspGfxBackendGu_DrawMesh(const void* vertices, const u16* indices, u32 count,
                             const PspGfxDrawState* state, const float* modelview, int cullFront, int cullBack,
                             const PspGfxGuMeshLighting* lighting, const PspGfxGuMeshUv* uv);
void PspGfxBackendGu_SealMeshData(const void* data, u32 bytes);
#endif

#include "src/psp/gfx/gfx_psp_backend_impl.h"

#endif
