#include "src/psp/gfx/gfx_psp_backend.h"

#include "src/psp/gfx/gfx_pspgl.h"

#include <stdint.h>

static PspGfxTextureHandle psp_gfx_backend_texture(u32 textureId, PspGfxPspglTextureRef textureRef) {
    PspGfxTextureHandle handle;

    if (textureId == 0) {
        return PspGfxTextureHandle_Null();
    }

    handle.opaque[0] = textureId;
    handle.opaque[1] = (u32) (uintptr_t) textureRef.state;
    handle.opaque[2] = textureRef.texture;
    handle.opaque[3] = textureRef.generation;
    return handle;
}

static u32 psp_gfx_backend_texture_id(PspGfxTextureHandle handle) {
    return handle.opaque[0];
}

static PspGfxPspglTextureRef psp_gfx_backend_texture_ref(PspGfxTextureHandle handle) {
    PspGfxPspglTextureRef textureRef = { 0 };

    textureRef.state = (PspGfxPspglTextureParameterState*) (uintptr_t) handle.opaque[1];
    textureRef.texture = handle.opaque[2];
    textureRef.generation = handle.opaque[3];
    return textureRef;
}

static PspGfxPspglTextureEnv psp_gfx_backend_texture_env(PspGfxTextureEnv textureEnv) {
    switch (textureEnv) {
        case PSP_GFX_TEX_MODULATE:
            return PSP_GFX_PSPGL_TEX_MODULATE;
        case PSP_GFX_TEX_BLEND:
            return PSP_GFX_PSPGL_TEX_BLEND;
        case PSP_GFX_TEX_REPLACE:
        default:
            return PSP_GFX_PSPGL_TEX_REPLACE;
    }
}

static PspGfxPspglTextureWrap psp_gfx_backend_texture_wrap(PspGfxTextureWrap wrap) {
    switch (wrap) {
        case PSP_GFX_WRAP_REPEAT:
            return PSP_GFX_PSPGL_WRAP_REPEAT;
        case PSP_GFX_WRAP_MIRROR:
            return PSP_GFX_PSPGL_WRAP_MIRROR;
        case PSP_GFX_WRAP_CLAMP:
        default:
            return PSP_GFX_PSPGL_WRAP_CLAMP;
    }
}

static PspGfxPspglVertexReservation psp_gfx_backend_reservation(const PspGfxVertexReservation* reservation) {
    PspGfxPspglVertexReservation pspglReservation;

    pspglReservation.vertices = reservation->vertices;
    pspglReservation.firstVertex = reservation->token.opaque;
    pspglReservation.capacity = reservation->capacity;
    return pspglReservation;
}

static void psp_gfx_backend_draw_state(const PspGfxDrawState* state, u32* textureId,
                                       PspGfxPspglTextureRef* textureRef) {
    *textureId = psp_gfx_backend_texture_id(state->texture);
    *textureRef = psp_gfx_backend_texture_ref(state->texture);
}

u32 PspGfxBackend_TextureDebugId(PspGfxTextureHandle handle) {
    return handle.opaque[0];
}

u32 PspGfxBackend_TextureDebugGeneration(PspGfxTextureHandle handle) {
    return handle.opaque[3];
}

void PspGfxBackend_SetMirrorEncoding(int mirrorS, int mirrorT) {
    PspGfxPspgl_SetMirrorEncoding(mirrorS, mirrorT);
}

int PspGfxBackend_CanMirrorEncode(u32 width, u32 height, int mirrorS, int mirrorT) {
    return PspGfxPspgl_CanMirrorEncode(width, height, mirrorS, mirrorT);
}

int PspGfxBackend_MirrorEncodingFailed(void) {
    return PspGfxPspgl_MirrorEncodingFailed();
}

void PspGfxBackend_InvalidateRgba16Texture(const u16* pixels) {
    PspGfxPspgl_InvalidateRgba16Texture(pixels);
}

void PspGfxBackend_SetScissor(float ulx, float uly, float lrx, float lry) {
    PspGfxPspgl_SetScissor(ulx, uly, lrx, lry);
}

void PspGfxBackend_ClearScissor(void) {
    PspGfxPspgl_ClearScissor();
}

int PspGfxBackend_FindCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height,
                                 PspGfxTextureHandle* texture, u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found =
        PspGfxPspgl_FindCi8Texture(indices, palette, width, height, &textureId, &textureRef, uploadWidth, uploadHeight);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height,
                                                   u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId =
        PspGfxPspgl_CreateCi8Texture(indices, palette, width, height, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height,
                                 PspGfxTextureHandle* texture, u32* uploadWidth, u32* uploadHeight, u32* uploadX,
                                 u32* uploadY) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindCi4Texture(indices, palette, width, height, &textureId, &textureRef, uploadWidth,
                                           uploadHeight, uploadX, uploadY);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height,
                                                   u32* uploadWidth, u32* uploadHeight, u32* uploadX, u32* uploadY) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId = PspGfxPspgl_CreateCi4Texture(indices, palette, width, height, uploadWidth, uploadHeight, uploadX,
                                                 uploadY, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply,
                                    PspGfxTextureHandle* texture, u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindRgba16Texture(pixels, width, height, premultiply, &textureId, &textureRef, uploadWidth,
                                              uploadHeight);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply,
                                                      u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId =
        PspGfxPspgl_CreateRgba16Texture(pixels, width, height, premultiply, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply,
                                    PspGfxTextureHandle* texture, u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindRgba32Texture(pixels, width, height, premultiply, &textureId, &textureRef, uploadWidth,
                                              uploadHeight);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply,
                                                      u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId =
        PspGfxPspgl_CreateRgba32Texture(pixels, width, height, premultiply, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindRgba32EnvBlendTexture(const void* pixels, u32 width, u32 height, u32 primitiveColor,
                                            u32 environmentColor, PspGfxTextureHandle* texture, u32* uploadWidth,
                                            u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindRgba32EnvBlendTexture(pixels, width, height, primitiveColor, environmentColor,
                                                      &textureId, &textureRef, uploadWidth, uploadHeight);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateRgba32EnvBlendTexture(const void* pixels, u32 width, u32 height,
                                                              u32 primitiveColor, u32 environmentColor,
                                                              u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId = PspGfxPspgl_CreateRgba32EnvBlendTexture(pixels, width, height, primitiveColor, environmentColor,
                                                            uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindIa8Texture(const u8* pixels, u32 width, u32 height, PspGfxTextureHandle* texture,
                                 u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindIa8Texture(pixels, width, height, &textureId, uploadWidth, uploadHeight, &textureRef);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth,
                                                   u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId = PspGfxPspgl_CreateIa8Texture(pixels, width, height, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindIa8SoftCoverageTexture(const u8* pixels, u32 width, u32 height, PspGfxTextureHandle* texture,
                                             u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindIa8SoftCoverageTexture(pixels, width, height, &textureId, uploadWidth, uploadHeight,
                                                       &textureRef);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateIa8SoftCoverageTexture(const u8* pixels, u32 width, u32 height,
                                                               u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId =
        PspGfxPspgl_CreateIa8SoftCoverageTexture(pixels, width, height, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindIa8EnvBlendTexture(const u8* pixels, u32 width, u32 height, u32 primitiveColor,
                                         u32 environmentColor, PspGfxTextureHandle* texture, u32* uploadWidth,
                                         u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindIa8EnvBlendTexture(pixels, width, height, primitiveColor, environmentColor, &textureId,
                                                   uploadWidth, uploadHeight, &textureRef);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateIa8EnvBlendTexture(const u8* pixels, u32 width, u32 height, u32 primitiveColor,
                                                           u32 environmentColor, u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId = PspGfxPspgl_CreateIa8EnvBlendTexture(pixels, width, height, primitiveColor, environmentColor,
                                                         uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindIa16Texture(const u16* pixels, u32 width, u32 height, PspGfxTextureHandle* texture,
                                  u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindIa16Texture(pixels, width, height, &textureId, uploadWidth, uploadHeight, &textureRef);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth,
                                                    u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId = PspGfxPspgl_CreateIa16Texture(pixels, width, height, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

int PspGfxBackend_FindIa16SoftCoverageTexture(const u16* pixels, u32 width, u32 height, PspGfxTextureHandle* texture,
                                              u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;
    int found = PspGfxPspgl_FindIa16SoftCoverageTexture(pixels, width, height, &textureId, uploadWidth, uploadHeight,
                                                        &textureRef);

    if (found) {
        *texture = psp_gfx_backend_texture(textureId, textureRef);
    }
    return found;
}

PspGfxTextureHandle PspGfxBackend_CreateIa16SoftCoverageTexture(const u16* pixels, u32 width, u32 height,
                                                                u32* uploadWidth, u32* uploadHeight) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId =
        PspGfxPspgl_CreateIa16SoftCoverageTexture(pixels, width, height, uploadWidth, uploadHeight, &textureRef);

    return psp_gfx_backend_texture(textureId, textureRef);
}

void PspGfxBackend_DrawTriangles(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;

    psp_gfx_backend_draw_state(state, &textureId, &textureRef);
    PspGfxPspgl_DrawColoredTriangles(
        vertices, vertexCount, textureId, textureRef, psp_gfx_backend_texture_env(state->textureEnv),
        state->textureEnvColor, psp_gfx_backend_texture_wrap(state->wrapS), psp_gfx_backend_texture_wrap(state->wrapT),
        state->alphaTest, state->blend, state->premultiplied, state->depthTest, state->depthWrite, state->fog,
        state->fogColor, state->fogStart, state->fogEnd, state->projectionMatrix, state->projectionSerial,
        state->pretransformed, state->pointFilter);
}

int PspGfxBackend_ReserveVertices(u32 vertexCapacity, PspGfxVertexReservation* reservation) {
    PspGfxPspglVertexReservation pspglReservation;

    if (!PspGfxPspgl_ReserveColoredVertices(vertexCapacity, &pspglReservation)) {
        return 0;
    }
    reservation->vertices = pspglReservation.vertices;
    reservation->token.opaque = pspglReservation.firstVertex;
    reservation->capacity = pspglReservation.capacity;
    return 1;
}

void PspGfxBackend_DrawReservedTriangles(const PspGfxVertexReservation* reservation, u32 vertexCount,
                                         const PspGfxDrawState* state) {
    PspGfxPspglVertexReservation pspglReservation = psp_gfx_backend_reservation(reservation);
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;

    psp_gfx_backend_draw_state(state, &textureId, &textureRef);
    PspGfxPspgl_DrawReservedColoredTriangles(
        &pspglReservation, vertexCount, textureId, textureRef, psp_gfx_backend_texture_env(state->textureEnv),
        state->textureEnvColor, psp_gfx_backend_texture_wrap(state->wrapS), psp_gfx_backend_texture_wrap(state->wrapT),
        state->alphaTest, state->blend, state->premultiplied, state->depthTest, state->depthWrite, state->fog,
        state->fogColor, state->fogStart, state->fogEnd, state->projectionMatrix, state->projectionSerial,
        state->pretransformed, state->pointFilter);
}

void PspGfxBackend_DrawFogTriangles(const PspGfxFogVertex* vertices, u32 vertexCount, const float* projectionMatrix,
                                    u32 projectionSerial, int pretransformed, int restoreDepthTest,
                                    int restoreDepthWrite, PspGfxTextureHandle restoreTexture,
                                    const PspGfxVertex* restoreVertices) {
    PspGfxPspgl_DrawFogTriangles(vertices, vertexCount, projectionMatrix, projectionSerial, pretransformed,
                                 restoreDepthTest, restoreDepthWrite, psp_gfx_backend_texture_id(restoreTexture),
                                 restoreVertices);
}

void PspGfxBackend_DrawSprites(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state,
                               PspGfxViewportPolicy viewport) {
    PspGfxPspglTextureRef textureRef = { 0 };
    u32 textureId;

    psp_gfx_backend_draw_state(state, &textureId, &textureRef);
    PspGfxPspgl_DrawColoredSprites(
        vertices, vertexCount, textureId, textureRef, psp_gfx_backend_texture_env(state->textureEnv),
        state->textureEnvColor, psp_gfx_backend_texture_wrap(state->wrapS), psp_gfx_backend_texture_wrap(state->wrapT),
        state->alphaTest, state->blend, state->premultiplied, state->depthTest, state->depthWrite, state->fog,
        state->fogColor, state->fogStart, state->fogEnd, state->projectionMatrix, state->projectionSerial,
        state->pretransformed, state->pointFilter, viewport);
}

void PspGfxBackend_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend, int fullViewport) {
    PspGfxPspgl_DrawSolidRect(ulx, uly, lrx, lry, color, blend, fullViewport);
}

void PspGfxBackend_SetViewportPolicy(PspGfxViewportPolicy viewport) {
    PspGfxPspgl_SetViewportPolicy(viewport);
}

void PspGfxBackend_SetHudAnchor(s16 x, s16 y) {
    PspGfxPspgl_SetHudAnchor(x, y);
}

void PspGfxBackend_BeginReplayCache(void) {
    PspGfxPspgl_BeginReplayCache();
}

void PspGfxBackend_EndReplayCache(void) {
    PspGfxPspgl_EndReplayCache();
}

void PspGfxBackend_ReplayCache(void) {
    PspGfxPspgl_ReplayCache();
}
