#include <n64psp/display.h>

#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/gfx/gfx_psp_backend_gu.h"
#include "src/psp/display.h"
#include "src/psp/gfx/gfx_psp_gu_texture.h"
#include "src/psp/profiler.h"
#include "macros.h"

#include <math.h>
#include <pspkernel.h>
#include <pspgu.h>

#define PSP_GFX_GU_N64_WIDTH 320.0f
#define PSP_GFX_GU_N64_HEIGHT 240.0f
#define PSP_GFX_GU_LIST_BYTES (262144U * sizeof(unsigned int))
#define PSP_GFX_GU_LIST_DRAW_RESERVE 4096U
#define PSP_GFX_GU_DEPTH_FUNC GU_GEQUAL
#define PSP_GFX_GU_RESERVATION_SLOTS 64
#define PSP_GFX_GU_ALPHA_HALF 127
#define PSP_GFX_GU_FIXED_ONE 0x00FFFFFFU

typedef struct {
    u32 color;
    float x;
    float y;
    float z;
} PspGfxGuColorVertex;

typedef char PspGfxGuColorVertexSizeCheck[(sizeof(PspGfxGuColorVertex) == 16) ? 1 : -1];

typedef struct {
    float u;
    float v;
    u32 color;
    float x;
    float y;
    float z;
} PspGfxGuTextureVertex;

typedef char PspGfxGuTextureVertexSizeCheck[(sizeof(PspGfxGuTextureVertex) == 24) ? 1 : -1];

typedef struct {
    PspGfxVertex* vertices;
    u32 capacity;
    u32 token;
    u32 frame;
    int active;
} PspGfxGuReservation;

static const ScePspFMatrix4 sPspGfxGuIdentityMatrix __attribute__((aligned(16))) = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

static PspGfxGuReservation sPspGfxGuReservations[PSP_GFX_GU_RESERVATION_SLOTS];
static u32 sPspGfxGuReservationFrame;
static u32 sPspGfxGuReservationToken;
static int sPspGfxGuReservationFrameActive;
static s16 sPspGfxGuHudAnchorX;
static s16 sPspGfxGuHudAnchorY;

static void psp_gfx_gu_apply_viewport(const n64psp_display_config* display, int x, int y, int width, int height) {
    sceGuOffset(2048 - (display->framebuffer_width / 2), 2048 - (display->framebuffer_height / 2));
    sceGuViewport(2048 - (display->framebuffer_width / 2) + x + (width / 2),
                  2048 - (display->framebuffer_height / 2) + y + (height / 2), width, height);
}

static int psp_gfx_gu_is_hud_anchor_viewport(PspGfxViewportPolicy policy) {
    return ((policy >= PSP_GFX_VIEWPORT_HUD_TOP_LEFT) && (policy <= PSP_GFX_VIEWPORT_HUD_TOP_CENTER)) ||
           (policy == PSP_GFX_VIEWPORT_HUD_SCALED_TOP_LEFT) ||
           (policy == PSP_GFX_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT);
}

static int psp_gfx_gu_select_viewport(PspGfxViewportPolicy policy) {
    const n64psp_display_config* display = PspDisplay_GetConfig();
    int x;
    int y;
    int width;
    int height;

    if (policy == PSP_GFX_VIEWPORT_WIDE_UI) {
        policy = PSP_GFX_VIEWPORT_FULL;
    }
    if ((policy == PSP_GFX_VIEWPORT_CENTERED_UI) && (display->mode != N64PSP_DISPLAY_PSP_480X272)) {
        policy = PSP_GFX_VIEWPORT_FULL;
    }

    if (policy == PSP_GFX_VIEWPORT_NATIVE_HUD) {
        x = ((int) display->framebuffer_width - 320) / 2;
        y = ((int) display->framebuffer_height - 240) / 2;
        width = 320;
        height = 240;
    } else if (psp_gfx_gu_is_hud_anchor_viewport(policy)) {
        int scaledWidth = display->ui_viewport_width;
        int scaledHeight = display->ui_viewport_height;
        int forceScaled = (policy == PSP_GFX_VIEWPORT_HUD_SCALED_TOP_LEFT) ||
                          (policy == PSP_GFX_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT);
        int left = display->viewport_x;
        int top = display->viewport_y;
        int right = display->viewport_x + display->viewport_width;
        int bottom = display->viewport_y + display->viewport_height;

        width = (PspDisplay_IsUiScalingEnabled() || forceScaled) ? scaledWidth : 320;
        height = (PspDisplay_IsUiScalingEnabled() || forceScaled) ? scaledHeight : 240;
        x = (left + right - width) / 2;
        y = top;

        if (display->mode == N64PSP_DISPLAY_PSP_480X272) {
            left = top = 0;
            right = display->framebuffer_width;
            bottom = display->framebuffer_height;
        }
        if ((policy == PSP_GFX_VIEWPORT_HUD_TOP_LEFT) || (policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_LEFT) ||
            (policy == PSP_GFX_VIEWPORT_HUD_SCALED_TOP_LEFT)) {
            x = left;
        }
        if ((policy == PSP_GFX_VIEWPORT_HUD_TOP_RIGHT) || (policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_RIGHT) ||
            (policy == PSP_GFX_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT)) {
            x = right - width;
        }
        if ((policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_LEFT) || (policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_RIGHT) ||
            (policy == PSP_GFX_VIEWPORT_HUD_SCALED_BOTTOM_RIGHT)) {
            y = bottom - height;
        }
        if (!PspDisplay_IsUiScalingEnabled() && !forceScaled) {
            float targetX;
            float targetY;

            if ((policy == PSP_GFX_VIEWPORT_HUD_TOP_LEFT) || (policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_LEFT)) {
                targetX = left + sPspGfxGuHudAnchorX * ((float) scaledWidth / 320.0f);
            } else if ((policy == PSP_GFX_VIEWPORT_HUD_TOP_RIGHT) ||
                       (policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_RIGHT)) {
                targetX = right - (320 - sPspGfxGuHudAnchorX) * ((float) scaledWidth / 320.0f);
            } else {
                targetX = (left + right) * 0.5f + (sPspGfxGuHudAnchorX - 160) * ((float) scaledWidth / 320.0f);
            }
            if ((policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_LEFT) ||
                (policy == PSP_GFX_VIEWPORT_HUD_BOTTOM_RIGHT)) {
                targetY = bottom - (240 - sPspGfxGuHudAnchorY) * ((float) scaledHeight / 240.0f);
            } else {
                targetY = top + sPspGfxGuHudAnchorY * ((float) scaledHeight / 240.0f);
            }
            x = (int) roundf(targetX) - sPspGfxGuHudAnchorX;
            y = (int) roundf(targetY) - sPspGfxGuHudAnchorY;
        }
    } else if (policy == PSP_GFX_VIEWPORT_CENTERED_UI) {
        x = display->ui_viewport_x;
        y = display->ui_viewport_y;
        width = display->ui_viewport_width;
        height = display->ui_viewport_height;
    } else {
        x = display->viewport_x;
        y = display->viewport_y;
        width = display->viewport_width;
        height = display->viewport_height;
    }

    psp_gfx_gu_apply_viewport(display, x, y, width, height);
    return 1;
}

static u8 psp_gfx_gu_float_to_u8(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }
    return (u8) ((value * 255.0f) + 0.5f);
}

static u32 psp_gfx_gu_fog_color(const float* color) {
    return GU_RGBA(psp_gfx_gu_float_to_u8(color[0]), psp_gfx_gu_float_to_u8(color[1]),
                   psp_gfx_gu_float_to_u8(color[2]), psp_gfx_gu_float_to_u8(color[3]));
}

static void psp_gfx_gu_prepare_native_fog(const PspGfxDrawState* state) {
    if (state->fog && !state->pretransformed && (state->fogColor != NULL) && (state->fogEnd > state->fogStart)) {
        sceGuFog(state->fogStart, state->fogEnd, psp_gfx_gu_fog_color(state->fogColor));
        sceGuEnable(GU_FOG);
    } else {
        sceGuDisable(GU_FOG);
    }
}

static void psp_gfx_gu_load_identity(void) {
    sceGuSetMatrix(GU_PROJECTION, &sPspGfxGuIdentityMatrix);
    sceGuSetMatrix(GU_VIEW, &sPspGfxGuIdentityMatrix);
    sceGuSetMatrix(GU_MODEL, &sPspGfxGuIdentityMatrix);
}

static void* psp_gfx_gu_alloc_vertices(u32 vertexCount, u32 vertexSize) {
    u32 bytes;
    u32 listBytes;
    int checkedListBytes;
    void* vertices;

    if ((vertexCount == 0) || (vertexSize == 0) || (vertexCount > (0x7FFFFFFFU / vertexSize))) {
        return NULL;
    }

    bytes = vertexCount * vertexSize;
    checkedListBytes = sceGuCheckList();
    if (checkedListBytes < 0) {
        return NULL;
    }

    listBytes = (u32) checkedListBytes;
    if ((listBytes > PSP_GFX_GU_LIST_BYTES) ||
        (bytes > (PSP_GFX_GU_LIST_BYTES - listBytes)) ||
        (PSP_GFX_GU_LIST_DRAW_RESERVE > (PSP_GFX_GU_LIST_BYTES - listBytes - bytes))) {
        return NULL;
    }

    vertices = sceGuGetMemory((int) bytes);
    if (vertices == NULL) {
        return NULL;
    }

    return vertices;
}

static void psp_gfx_gu_count_vertex_copy(u32 bytes) {
    int checkedListBytes = sceGuCheckList();
    u32 highWater = (checkedListBytes > 0) ? (u32) checkedListBytes : 0;

    PspProfiler_CountVertexStream(0, 0, 1, bytes, 0, 0, 0, PSP_GFX_GU_LIST_BYTES, highWater, 0, 0, 0, 0);
    (void) bytes;
    (void) highWater;
}

static void psp_gfx_gu_count_reserved_vertex_draw(u32 vertexCount) {
    int checkedListBytes = sceGuCheckList();
    u32 highWater = (checkedListBytes > 0) ? (u32) checkedListBytes : 0;

    PspProfiler_CountVertexStream(1, vertexCount, 0, 0, 0, 0, 0, PSP_GFX_GU_LIST_BYTES, highWater, 0, 0, 0, 0);
    (void) vertexCount;
    (void) highWater;
}

void PspGfxBackendGu_BeginFrame(void) {
    u32 i;

    sPspGfxGuReservationFrame++;
    if (sPspGfxGuReservationFrame == 0) {
        sPspGfxGuReservationFrame = 1;
    }
    for (i = 0; i < PSP_GFX_GU_RESERVATION_SLOTS; i++) {
        sPspGfxGuReservations[i].active = 0;
    }
    sPspGfxGuReservationFrameActive = 1;
}

void PspGfxBackendGu_EndFrame(void) {
    u32 i;

    sPspGfxGuReservationFrameActive = 0;
    for (i = 0; i < PSP_GFX_GU_RESERVATION_SLOTS; i++) {
        sPspGfxGuReservations[i].active = 0;
    }
}

static int psp_gfx_gu_prepare_texture(const PspGfxDrawState* state) {
    const void* pixels;
    u32 width;
    u32 height;
    int wrapS;
    int wrapT;
    int textureFunction;
    int textureComponents;

    if (!PspGfxGuTexture_Resolve(state->texture, &pixels, &width, &height)) {
        return 0;
    }

    wrapS = (state->wrapS == PSP_GFX_WRAP_CLAMP) ? GU_CLAMP : GU_REPEAT;
    wrapT = (state->wrapT == PSP_GFX_WRAP_CLAMP) ? GU_CLAMP : GU_REPEAT;
    if (state->textureEnv == PSP_GFX_TEX_BLEND) {
        textureFunction = GU_TFX_BLEND;
    } else if (state->textureEnv == PSP_GFX_TEX_MODULATE) {
        textureFunction = GU_TFX_MODULATE;
    } else {
        textureFunction = GU_TFX_REPLACE;
    }
    textureComponents = (state->textureEnv == PSP_GFX_TEX_BLEND) ? GU_TCC_RGB : GU_TCC_RGBA;
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, (int) width, (int) height, (int) width, pixels);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    sceGuTexFunc(textureFunction, textureComponents);
    sceGuTexEnvColor(state->textureEnvColor & 0x00FFFFFFU);
    sceGuTexWrap(wrapS, wrapT);
    sceGuTexFilter(state->pointFilter ? GU_NEAREST : GU_LINEAR, state->pointFilter ? GU_NEAREST : GU_LINEAR);
    sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
    sceGuTexLevelMode(GU_TEXTURE_CONST, 0.0f);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuEnable(GU_TEXTURE_2D);
    return 1;
}

static void psp_gfx_gu_prepare_alpha_blend(const PspGfxDrawState* state, int textured) {
    if (textured && state->alphaTest) {
        sceGuAlphaFunc(GU_GREATER, (state->alphaTest > 1) ? PSP_GFX_GU_ALPHA_HALF : 0, 0xFF);
        sceGuEnable(GU_ALPHA_TEST);
    } else {
        sceGuDisable(GU_ALPHA_TEST);
    }

    if (textured && state->blend) {
        sceGuEnable(GU_BLEND);
        if (state->premultiplied) {
            sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, PSP_GFX_GU_FIXED_ONE, 0);
        } else {
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        }
    } else {
        sceGuDisable(GU_BLEND);
    }
}

static int psp_gfx_gu_prepare_colored_draw(const PspGfxDrawState* state) {
    int textured;

    if ((state == NULL) || !psp_gfx_gu_select_viewport(state->viewport)) {
        return 0;
    }

    textured = PspGfxTextureHandle_IsValid(state->texture);
    if (textured && !psp_gfx_gu_prepare_texture(state)) {
        return 0;
    }

    if (state->pretransformed || (state->projectionMatrix == NULL)) {
        psp_gfx_gu_load_identity();
    } else {
        sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4*) state->projectionMatrix);
        sceGuSetMatrix(GU_VIEW, &sPspGfxGuIdentityMatrix);
        sceGuSetMatrix(GU_MODEL, &sPspGfxGuIdentityMatrix);
    }

    if (state->depthTest) {
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthFunc(PSP_GFX_GU_DEPTH_FUNC);
    } else {
        sceGuDisable(GU_DEPTH_TEST);
    }
    sceGuDepthMask(state->depthWrite ? GU_FALSE : GU_TRUE);
    if (!textured) {
        sceGuDisable(GU_TEXTURE_2D);
    }
    psp_gfx_gu_prepare_alpha_blend(state, textured);
    psp_gfx_gu_prepare_native_fog(state);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    return 1;
}

u32 PspGfxBackend_TextureDebugId(PspGfxTextureHandle handle) {
    return handle.opaque[0];
}

u32 PspGfxBackend_TextureDebugGeneration(PspGfxTextureHandle handle) {
    return handle.opaque[3];
}

int PspGfxBackend_TextureSupported(const PspGfxTextureRequest* request) {
    return PspGfxGuTexture_Supported(request);
}

int PspGfxBackend_FindTexture(const PspGfxTextureRequest* request, PspGfxTextureResult* result) {
    return PspGfxGuTexture_Find(request, result);
}

int PspGfxBackend_CreateTexture(const PspGfxTextureRequest* request, PspGfxTextureResult* result) {
    return PspGfxGuTexture_Create(request, result);
}

void PspGfxBackend_InvalidateRgba16Texture(const u16* pixels) {
    PspGfxGuTexture_InvalidateRgba16(pixels);
}

void PspGfxBackend_SetScissor(float ulx, float uly, float lrx, float lry) {
    const n64psp_display_config* display = PspDisplay_GetConfig();
    float scaleX = (float) display->viewport_width / PSP_GFX_GU_N64_WIDTH;
    float scaleY = (float) display->viewport_height / PSP_GFX_GU_N64_HEIGHT;
    int x0;
    int y0;
    int x1;
    int y1;

    if ((ulx == SCREEN_MARGIN) && (uly == SCREEN_MARGIN) &&
        (lrx == SCREEN_WIDTH - SCREEN_MARGIN) && (lry == SCREEN_HEIGHT - SCREEN_MARGIN)) {
        ulx = uly = 0.0f;
        lrx = PSP_GFX_GU_N64_WIDTH;
        lry = PSP_GFX_GU_N64_HEIGHT;
    }

    if (ulx < 0.0f) ulx = 0.0f;
    if (uly < 0.0f) uly = 0.0f;
    if (lrx > PSP_GFX_GU_N64_WIDTH) lrx = PSP_GFX_GU_N64_WIDTH;
    if (lry > PSP_GFX_GU_N64_HEIGHT) lry = PSP_GFX_GU_N64_HEIGHT;
    if ((lrx <= ulx) || (lry <= uly)) return;

    x0 = display->viewport_x + (int) (ulx * scaleX);
    y0 = display->viewport_y + (int) (uly * scaleY);
    x1 = display->viewport_x + (int) ((lrx * scaleX) + 0.5f);
    y1 = display->viewport_y + (int) ((lry * scaleY) + 0.5f);
    sceGuScissor(x0, y0, x1 - x0, y1 - y0);
    sceGuEnable(GU_SCISSOR_TEST);
}

void PspGfxBackend_ClearScissor(void) {
    sceGuDisable(GU_SCISSOR_TEST);
}

void PspGfxBackend_DrawTriangles(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state) {
    void* guVertices;
    u32 drawFlags;
    u32 vertexSize;
    int textured;
    u32 i;
    u32 bytes;

    if ((vertices == NULL) || (vertexCount == 0) || ((vertexCount % 3) != 0) ||
        (state == NULL)) {
        return;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_prepare_colored_draw(state)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        return;
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    textured = PspGfxTextureHandle_IsValid(state->texture);
    vertexSize = textured ? sizeof(PspGfxGuTextureVertex) : sizeof(PspGfxGuColorVertex);
    drawFlags = GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
    if (textured) {
        drawFlags |= GU_TEXTURE_32BITF;
    }
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    guVertices = psp_gfx_gu_alloc_vertices(vertexCount, vertexSize);
    if (guVertices == NULL) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
        return;
    }

    for (i = 0; i < vertexCount; i++) {
        if (textured) {
            PspGfxGuTextureVertex* textureVertices = (PspGfxGuTextureVertex*) guVertices;

            textureVertices[i].u = vertices[i].u;
            textureVertices[i].v = vertices[i].v;
            textureVertices[i].color = vertices[i].color;
            textureVertices[i].x = vertices[i].x;
            textureVertices[i].y = vertices[i].y;
            textureVertices[i].z = vertices[i].z;
        } else {
            PspGfxGuColorVertex* colorVertices = (PspGfxGuColorVertex*) guVertices;

            colorVertices[i].color = vertices[i].color;
            colorVertices[i].x = vertices[i].x;
            colorVertices[i].y = vertices[i].y;
            colorVertices[i].z = vertices[i].z;
        }
    }

    bytes = vertexCount * vertexSize;
    sceKernelDcacheWritebackRange(guVertices, bytes);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    sceGuDrawArray(GU_TRIANGLES, drawFlags, vertexCount, 0, guVertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    psp_gfx_gu_count_vertex_copy(bytes);
}

int PspGfxBackend_ReserveVertices(u32 vertexCapacity, PspGfxVertexReservation* reservation) {
    PspGfxGuReservation* guReservation = NULL;
    PspGfxVertex* vertices;
    u32 i;
    u32 token;

    if (reservation != NULL) {
        reservation->vertices = NULL;
        reservation->token.opaque = 0;
        reservation->capacity = 0;
    }
    if (!sPspGfxGuReservationFrameActive || (reservation == NULL) || (vertexCapacity == 0) ||
        (vertexCapacity > (0x7FFFFFFFU / sizeof(PspGfxVertex)))) {
        return 0;
    }
    for (i = 0; i < PSP_GFX_GU_RESERVATION_SLOTS; i++) {
        if (!sPspGfxGuReservations[i].active) {
            guReservation = &sPspGfxGuReservations[i];
            break;
        }
    }
    if (guReservation == NULL) {
        return 0;
    }
    vertices = (PspGfxVertex*) psp_gfx_gu_alloc_vertices(vertexCapacity, sizeof(PspGfxVertex));
    if (vertices == NULL) {
        return 0;
    }
    sPspGfxGuReservationToken++;
    if (sPspGfxGuReservationToken == 0) {
        sPspGfxGuReservationToken = 1;
    }
    token = sPspGfxGuReservationToken;
    guReservation->vertices = vertices;
    guReservation->capacity = vertexCapacity;
    guReservation->token = token;
    guReservation->frame = sPspGfxGuReservationFrame;
    guReservation->active = 1;
    reservation->vertices = vertices;
    reservation->token.opaque = token;
    reservation->capacity = vertexCapacity;
    return 1;
}

void PspGfxBackend_DrawReservedTriangles(const PspGfxVertexReservation* reservation, u32 vertexCount,
                                         const PspGfxDrawState* state) {
    PspGfxGuReservation* guReservation;
    u32 i;
    u32 bytes;

    if (!sPspGfxGuReservationFrameActive || (reservation == NULL) || (state == NULL) ||
        (reservation->vertices == NULL) ||
        (reservation->token.opaque == 0) || (vertexCount == 0) || ((vertexCount % 3) != 0) ||
        (vertexCount > reservation->capacity)) {
        return;
    }
    guReservation = NULL;
    for (i = 0; i < PSP_GFX_GU_RESERVATION_SLOTS; i++) {
        if (sPspGfxGuReservations[i].active &&
            (sPspGfxGuReservations[i].token == reservation->token.opaque) &&
            (sPspGfxGuReservations[i].frame == sPspGfxGuReservationFrame) &&
            (sPspGfxGuReservations[i].vertices == reservation->vertices) &&
            (sPspGfxGuReservations[i].capacity == reservation->capacity)) {
            guReservation = &sPspGfxGuReservations[i];
            break;
        }
    }
    if (guReservation == NULL) {
        return;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_prepare_colored_draw(state)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        return;
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    bytes = vertexCount * sizeof(PspGfxVertex);
    sceKernelDcacheWritebackRange(guReservation->vertices, bytes);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   vertexCount, 0, guReservation->vertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    psp_gfx_gu_count_reserved_vertex_draw(vertexCount);
}

void PspGfxBackend_DrawFogTriangles(const PspGfxFogVertex* vertices, u32 vertexCount,
                                    const PspGfxFogDrawState* state) {
    PspGfxGuColorVertex* guVertices;
    u32 bytes;
    u32 i;

    if ((vertices == NULL) || (vertexCount == 0) || ((vertexCount % 3) != 0) || (state == NULL)) {
        return;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_select_viewport(state->viewport)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        return;
    }
    if (state->pretransformed || (state->projectionMatrix == NULL)) {
        psp_gfx_gu_load_identity();
    } else {
        sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4*) state->projectionMatrix);
        sceGuSetMatrix(GU_VIEW, &sPspGfxGuIdentityMatrix);
        sceGuSetMatrix(GU_MODEL, &sPspGfxGuIdentityMatrix);
    }
    if (state->depthTest) {
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthFunc(GU_EQUAL);
    } else {
        sceGuDisable(GU_DEPTH_TEST);
    }
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    guVertices = (PspGfxGuColorVertex*) psp_gfx_gu_alloc_vertices(vertexCount, sizeof(PspGfxFogVertex));
    if (guVertices == NULL) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
        return;
    }
    for (i = 0; i < vertexCount; i++) {
        guVertices[i].color = vertices[i].color;
        guVertices[i].x = vertices[i].x;
        guVertices[i].y = vertices[i].y;
        guVertices[i].z = vertices[i].z;
    }
    bytes = vertexCount * sizeof(PspGfxFogVertex);
    sceKernelDcacheWritebackRange(guVertices, bytes);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   vertexCount, 0, guVertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    psp_gfx_gu_count_vertex_copy(bytes);
}

void PspGfxBackend_DrawSprites(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state) {
    void* guVertices;
    u32 drawFlags;
    u32 vertexSize;
    int textured;
    u32 i;
    u32 bytes;

    if ((vertices == NULL) || (vertexCount == 0) || ((vertexCount % 2) != 0) || (state == NULL)) {
        return;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_prepare_colored_draw(state)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        return;
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    textured = PspGfxTextureHandle_IsValid(state->texture);
    vertexSize = textured ? sizeof(PspGfxGuTextureVertex) : sizeof(PspGfxGuColorVertex);
    drawFlags = GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
    if (textured) {
        drawFlags |= GU_TEXTURE_32BITF;
    }
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    guVertices = psp_gfx_gu_alloc_vertices(vertexCount, vertexSize);
    if (guVertices == NULL) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
        return;
    }

    for (i = 0; i < vertexCount; i++) {
        if (textured) {
            PspGfxGuTextureVertex* textureVertices = (PspGfxGuTextureVertex*) guVertices;

            textureVertices[i].u = vertices[i].u;
            textureVertices[i].v = vertices[i].v;
            textureVertices[i].color = vertices[i].color;
            textureVertices[i].x = vertices[i].x;
            textureVertices[i].y = vertices[i].y;
            textureVertices[i].z = vertices[i].z;
        } else {
            PspGfxGuColorVertex* colorVertices = (PspGfxGuColorVertex*) guVertices;

            colorVertices[i].color = vertices[i].color;
            colorVertices[i].x = vertices[i].x;
            colorVertices[i].y = vertices[i].y;
            colorVertices[i].z = vertices[i].z;
        }
    }

    bytes = vertexCount * vertexSize;
    sceKernelDcacheWritebackRange(guVertices, bytes);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    sceGuDrawArray(GU_SPRITES, drawFlags, vertexCount, 0, guVertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    psp_gfx_gu_count_vertex_copy(bytes);
}

void PspGfxBackend_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend,
                                 PspGfxViewportPolicy viewport) {
    PspGfxGuColorVertex source[6] __attribute__((aligned(16)));
    PspGfxGuColorVertex* vertices;
    int i;

    if ((lrx <= ulx) || (lry <= uly) || !psp_gfx_gu_select_viewport(viewport)) {
        return;
    }

    source[0] = (PspGfxGuColorVertex) { color, (ulx / 160.0f) - 1.0f, 1.0f - (uly / 120.0f), 0.0f };
    source[1] = (PspGfxGuColorVertex) { color, (lrx / 160.0f) - 1.0f, 1.0f - (uly / 120.0f), 0.0f };
    source[2] = (PspGfxGuColorVertex) { color, (lrx / 160.0f) - 1.0f, 1.0f - (lry / 120.0f), 0.0f };
    source[3] = source[0];
    source[4] = source[2];
    source[5] = (PspGfxGuColorVertex) { color, (ulx / 160.0f) - 1.0f, 1.0f - (lry / 120.0f), 0.0f };

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    psp_gfx_gu_load_identity();
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    if (blend) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    } else {
        sceGuDisable(GU_BLEND);
    }
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    vertices = psp_gfx_gu_alloc_vertices(6, sizeof(PspGfxGuColorVertex));
    if (vertices == NULL) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
        return;
    }
    for (i = 0; i < 6; i++) {
        vertices[i] = source[i];
    }
    sceKernelDcacheWritebackRange(vertices, 6 * sizeof(PspGfxGuColorVertex));
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, 0, vertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(6);
    psp_gfx_gu_count_vertex_copy(6 * sizeof(PspGfxGuColorVertex));
}

void PspGfxBackend_SetHudAnchor(s16 x, s16 y) {
    sPspGfxGuHudAnchorX = x;
    sPspGfxGuHudAnchorY = y;
}

void PspGfxBackend_BeginReplayCache(void) {
}

void PspGfxBackend_EndReplayCache(void) {
}

void PspGfxBackend_ReplayCache(void) {
}
