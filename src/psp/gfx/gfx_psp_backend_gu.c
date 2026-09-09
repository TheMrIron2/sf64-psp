#include "src/psp/gfx/gfx_psp_backend.h"

u32 PspGfxBackend_TextureDebugId(PspGfxTextureHandle handle) {
    (void) handle;
    return 0;
}

u32 PspGfxBackend_TextureDebugGeneration(PspGfxTextureHandle handle) {
    (void) handle;
    return 0;
}

int PspGfxBackend_TextureSupported(const PspGfxTextureRequest* request) {
    (void) request;
    return 0;
}

int PspGfxBackend_FindTexture(const PspGfxTextureRequest* request, PspGfxTextureResult* result) {
    (void) request;
    if (result != NULL) {
        *result = (PspGfxTextureResult) { 0 };
        result->cacheResult = PSP_GFX_TEXTURE_CACHE_MISS;
    }
    return 0;
}

int PspGfxBackend_CreateTexture(const PspGfxTextureRequest* request, PspGfxTextureResult* result) {
    (void) request;
    if (result != NULL) {
        *result = (PspGfxTextureResult) { 0 };
        result->cacheResult = PSP_GFX_TEXTURE_CACHE_FAILED;
    }
    return 0;
}

void PspGfxBackend_InvalidateRgba16Texture(const u16* pixels) {
    (void) pixels;
}

void PspGfxBackend_SetScissor(float ulx, float uly, float lrx, float lry) {
    (void) ulx;
    (void) uly;
    (void) lrx;
    (void) lry;
}

void PspGfxBackend_ClearScissor(void) {
}

void PspGfxBackend_DrawTriangles(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state) {
    (void) vertices;
    (void) vertexCount;
    (void) state;
}

int PspGfxBackend_ReserveVertices(u32 vertexCapacity, PspGfxVertexReservation* reservation) {
    (void) vertexCapacity;
    if (reservation != NULL) {
        reservation->vertices = NULL;
        reservation->token.opaque = 0;
        reservation->capacity = 0;
    }
    return 0;
}

void PspGfxBackend_DrawReservedTriangles(const PspGfxVertexReservation* reservation, u32 vertexCount,
                                         const PspGfxDrawState* state) {
    (void) reservation;
    (void) vertexCount;
    (void) state;
}

void PspGfxBackend_DrawFogTriangles(const PspGfxFogVertex* vertices, u32 vertexCount,
                                    const PspGfxFogDrawState* state) {
    (void) vertices;
    (void) vertexCount;
    (void) state;
}

void PspGfxBackend_DrawSprites(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state) {
    (void) vertices;
    (void) vertexCount;
    (void) state;
}

void PspGfxBackend_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend,
                                 PspGfxViewportPolicy viewport) {
    (void) ulx;
    (void) uly;
    (void) lrx;
    (void) lry;
    (void) color;
    (void) blend;
    (void) viewport;
}

void PspGfxBackend_SetHudAnchor(s16 x, s16 y) {
    (void) x;
    (void) y;
}

void PspGfxBackend_BeginReplayCache(void) {
}

void PspGfxBackend_EndReplayCache(void) {
}

void PspGfxBackend_ReplayCache(void) {
}
