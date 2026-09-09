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
#include <string.h>

#define PSP_GFX_GU_N64_WIDTH 320.0f
#define PSP_GFX_GU_N64_HEIGHT 240.0f
#define PSP_GFX_GU_LIST_BYTES (262144U * sizeof(unsigned int))
#define PSP_GFX_GU_LIST_DRAW_RESERVE 4096U
#define PSP_GFX_GU_DEPTH_FUNC GU_GEQUAL
#define PSP_GFX_GU_RESERVATION_SLOTS 64
#define PSP_GFX_GU_ALPHA_HALF 127
#define PSP_GFX_GU_FIXED_ONE 0x00FFFFFFU
#define PSP_GFX_GU_REPLAY_CACHE_DRAWS 128
#define PSP_GFX_GU_REPLAY_CACHE_VERTICES 8192

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
static float sPspGfxGuScissorUlx;
static float sPspGfxGuScissorUly;
static float sPspGfxGuScissorLrx;
static float sPspGfxGuScissorLry;
static int sPspGfxGuScissorEnabled;

typedef struct {
    int valid;
    int viewportValid;
    int viewportOffsetX;
    int viewportOffsetY;
    int viewportX;
    int viewportY;
    int viewportWidth;
    int viewportHeight;
    int textureValid;
    int textureBound;
    PspGfxTextureHandle texture;
    int textureFunction;
    u32 textureEnvColor;
    int textureWrapS;
    int textureWrapT;
    int texturePointFilter;
    int textureEnabled;
    int alphaTestEnabled;
    int alphaTestReference;
    int alphaTestReferenceValid;
    int blendEnabled;
    int blendPremultiplied;
    int blendFuncValid;
    int depthTestEnabled;
    int depthFunction;
    int depthFunctionValid;
    int depthWrite;
    int fogValid;
    int fogEnabled;
    u32 fogColor;
    float fogStart;
    float fogEnd;
    int lightingEnabled;
    int cullEnabled;
    int clipPlanesEnabled;
    int shadeModel;
    int matrixValid;
    int projectionIdentity;
    const float* projectionMatrix;
    u32 projectionSerial;
    int viewModelIdentity;
} PspGfxGuStateCache;

static PspGfxGuStateCache sPspGfxGuState;

typedef enum {
    PSP_GFX_GU_REPLAY_TRIANGLES,
    PSP_GFX_GU_REPLAY_SPRITES,
    PSP_GFX_GU_REPLAY_SOLID_RECT,
    PSP_GFX_GU_REPLAY_FOG,
} PspGfxGuReplayType;

typedef struct {
    PspGfxGuReplayType type;
    u32 first;
    u32 count;
    PspGfxDrawState state;
    float fogColor[4];
    float projection[16];
    PspGfxFogDrawState fogState;
    float fogProjection[16];
    float ulx;
    float uly;
    float lrx;
    float lry;
    u32 color;
    int blend;
    PspGfxViewportPolicy viewport;
    float scissorUlx;
    float scissorUly;
    float scissorLrx;
    float scissorLry;
    int scissorEnabled;
    s16 hudAnchorX;
    s16 hudAnchorY;
} PspGfxGuReplayPacket;

static PspGfxGuReplayPacket sPspGfxGuReplayPackets[PSP_GFX_GU_REPLAY_CACHE_DRAWS];
static PspGfxVertex sPspGfxGuReplayVertices[PSP_GFX_GU_REPLAY_CACHE_VERTICES] __attribute__((aligned(16)));
static PspGfxFogVertex sPspGfxGuReplayFogVertices[PSP_GFX_GU_REPLAY_CACHE_VERTICES] __attribute__((aligned(16)));
static u32 sPspGfxGuReplayPacketCount;
static u32 sPspGfxGuReplayVertexCount;
static u32 sPspGfxGuReplayFogVertexCount;
static int sPspGfxGuReplayCapturing;
static int sPspGfxGuReplayReady;
static int sPspGfxGuReplayCaptureFailed;
static int sPspGfxGuReplayActive;

static void psp_gfx_gu_replay_clear(void) {
    sPspGfxGuReplayCapturing = 0;
    sPspGfxGuReplayReady = 0;
    sPspGfxGuReplayPacketCount = 0;
    sPspGfxGuReplayVertexCount = 0;
    sPspGfxGuReplayFogVertexCount = 0;
    sPspGfxGuReplayCaptureFailed = 0;
}

static void psp_gfx_gu_replay_capture_failed(void) {
    sPspGfxGuReplayCaptureFailed = 1;
    sPspGfxGuReplayReady = 0;
}

static void psp_gfx_gu_replay_note_draw_failure(void) {
    if (sPspGfxGuReplayCapturing && !sPspGfxGuReplayActive) {
        psp_gfx_gu_replay_capture_failed();
    }
}

static void psp_gfx_gu_replay_context(PspGfxGuReplayPacket* packet) {
    packet->scissorUlx = sPspGfxGuScissorUlx;
    packet->scissorUly = sPspGfxGuScissorUly;
    packet->scissorLrx = sPspGfxGuScissorLrx;
    packet->scissorLry = sPspGfxGuScissorLry;
    packet->scissorEnabled = sPspGfxGuScissorEnabled;
    packet->hudAnchorX = sPspGfxGuHudAnchorX;
    packet->hudAnchorY = sPspGfxGuHudAnchorY;
}

static void psp_gfx_gu_replay_draw_state(PspGfxGuReplayPacket* packet, const PspGfxDrawState* state) {
    packet->state = *state;
    if (state->fogColor != NULL) {
        memcpy(packet->fogColor, state->fogColor, sizeof(packet->fogColor));
        packet->state.fogColor = packet->fogColor;
    } else {
        packet->state.fogColor = NULL;
    }
    if (state->projectionMatrix != NULL) {
        memcpy(packet->projection, state->projectionMatrix, sizeof(packet->projection));
        packet->state.projectionMatrix = packet->projection;
    } else {
        packet->state.projectionMatrix = NULL;
    }
}

static void psp_gfx_gu_state_invalidate(void) {
    memset(&sPspGfxGuState, 0, sizeof(sPspGfxGuState));
}

static void psp_gfx_gu_set_capability(int capability, int enabled, int* cached) {
    if (sPspGfxGuState.valid && (*cached == enabled)) {
        return;
    }
    if (enabled) {
        sceGuEnable(capability);
    } else {
        sceGuDisable(capability);
    }
    *cached = enabled;
}

static void psp_gfx_gu_replay_capture_colored(const PspGfxVertex* vertices, u32 vertexCount,
                                               const PspGfxDrawState* state, PspGfxGuReplayType type) {
    PspGfxGuReplayPacket* packet;

    if (!sPspGfxGuReplayCapturing || sPspGfxGuReplayActive) {
        return;
    }
    if ((sPspGfxGuReplayPacketCount >= PSP_GFX_GU_REPLAY_CACHE_DRAWS) ||
        (vertexCount > (PSP_GFX_GU_REPLAY_CACHE_VERTICES - sPspGfxGuReplayVertexCount))) {
        psp_gfx_gu_replay_capture_failed();
        return;
    }
    packet = &sPspGfxGuReplayPackets[sPspGfxGuReplayPacketCount++];
    packet->type = type;
    packet->first = sPspGfxGuReplayVertexCount;
    packet->count = vertexCount;
    psp_gfx_gu_replay_draw_state(packet, state);
    psp_gfx_gu_replay_context(packet);
    memcpy(&sPspGfxGuReplayVertices[sPspGfxGuReplayVertexCount], vertices,
           vertexCount * sizeof(*vertices));
    sPspGfxGuReplayVertexCount += vertexCount;
}

static void psp_gfx_gu_replay_capture_fog(const PspGfxFogVertex* vertices, u32 vertexCount,
                                           const PspGfxFogDrawState* state) {
    PspGfxGuReplayPacket* packet;

    if (!sPspGfxGuReplayCapturing || sPspGfxGuReplayActive) {
        return;
    }
    if ((sPspGfxGuReplayPacketCount >= PSP_GFX_GU_REPLAY_CACHE_DRAWS) ||
        (vertexCount > (PSP_GFX_GU_REPLAY_CACHE_VERTICES - sPspGfxGuReplayFogVertexCount))) {
        psp_gfx_gu_replay_capture_failed();
        return;
    }
    packet = &sPspGfxGuReplayPackets[sPspGfxGuReplayPacketCount++];
    packet->type = PSP_GFX_GU_REPLAY_FOG;
    packet->first = sPspGfxGuReplayFogVertexCount;
    packet->count = vertexCount;
    packet->fogState = *state;
    if (state->projectionMatrix != NULL) {
        memcpy(packet->fogProjection, state->projectionMatrix, sizeof(packet->fogProjection));
        packet->fogState.projectionMatrix = packet->fogProjection;
    } else {
        packet->fogState.projectionMatrix = NULL;
    }
    psp_gfx_gu_replay_context(packet);
    memcpy(&sPspGfxGuReplayFogVertices[sPspGfxGuReplayFogVertexCount], vertices,
           vertexCount * sizeof(*vertices));
    sPspGfxGuReplayFogVertexCount += vertexCount;
}

static int psp_gfx_gu_replay_textures_valid(void) {
    u32 i;

    for (i = 0; i < sPspGfxGuReplayPacketCount; i++) {
        const PspGfxGuReplayPacket* packet = &sPspGfxGuReplayPackets[i];
        const void* pixels;
        u32 width;
        u32 height;

        if (((packet->type == PSP_GFX_GU_REPLAY_TRIANGLES) ||
             (packet->type == PSP_GFX_GU_REPLAY_SPRITES)) &&
            PspGfxTextureHandle_IsValid(packet->state.texture) &&
            !PspGfxGuTexture_Resolve(packet->state.texture, &pixels, &width, &height)) {
            return 0;
        }
    }
    return 1;
}

static void psp_gfx_gu_replay_restore_context(const PspGfxGuReplayPacket* packet) {
    PspGfxBackend_SetHudAnchor(packet->hudAnchorX, packet->hudAnchorY);
    if (packet->scissorEnabled) {
        PspGfxBackend_SetScissor(packet->scissorUlx, packet->scissorUly, packet->scissorLrx, packet->scissorLry);
    } else {
        PspGfxBackend_ClearScissor();
    }
}

static void psp_gfx_gu_apply_viewport(const n64psp_display_config* display, int x, int y, int width, int height) {
    int offsetX = 2048 - (display->framebuffer_width / 2);
    int offsetY = 2048 - (display->framebuffer_height / 2);
    int viewportX = offsetX + x + (width / 2);
    int viewportY = offsetY + y + (height / 2);

    if (!sPspGfxGuState.viewportValid || (sPspGfxGuState.viewportOffsetX != offsetX) ||
        (sPspGfxGuState.viewportOffsetY != offsetY)) {
        sceGuOffset(offsetX, offsetY);
        sPspGfxGuState.viewportOffsetX = offsetX;
        sPspGfxGuState.viewportOffsetY = offsetY;
    }
    if (!sPspGfxGuState.viewportValid || (sPspGfxGuState.viewportX != viewportX) ||
        (sPspGfxGuState.viewportY != viewportY) || (sPspGfxGuState.viewportWidth != width) ||
        (sPspGfxGuState.viewportHeight != height)) {
        sceGuViewport(viewportX, viewportY, width, height);
        sPspGfxGuState.viewportX = viewportX;
        sPspGfxGuState.viewportY = viewportY;
        sPspGfxGuState.viewportWidth = width;
        sPspGfxGuState.viewportHeight = height;
    }
    sPspGfxGuState.viewportValid = 1;
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
    int enabled = state->fog && !state->pretransformed && (state->fogColor != NULL) &&
                  (state->fogEnd > state->fogStart);
    u32 color = enabled ? psp_gfx_gu_fog_color(state->fogColor) : 0;

    if (enabled && (!sPspGfxGuState.fogValid || !sPspGfxGuState.fogEnabled ||
                    (sPspGfxGuState.fogColor != color) || (sPspGfxGuState.fogStart != state->fogStart) ||
                    (sPspGfxGuState.fogEnd != state->fogEnd))) {
        sceGuFog(state->fogStart, state->fogEnd, color);
    }
    psp_gfx_gu_set_capability(GU_FOG, enabled, &sPspGfxGuState.fogEnabled);
    if (enabled) {
        sPspGfxGuState.fogColor = color;
        sPspGfxGuState.fogStart = state->fogStart;
        sPspGfxGuState.fogEnd = state->fogEnd;
    }
    sPspGfxGuState.fogValid = 1;
}

static void psp_gfx_gu_set_matrices(const PspGfxDrawState* state) {
    int identity = state->pretransformed || (state->projectionMatrix == NULL);

    if (!sPspGfxGuState.matrixValid || (sPspGfxGuState.projectionIdentity != identity) ||
        (!identity && ((sPspGfxGuState.projectionMatrix != state->projectionMatrix) ||
                       (sPspGfxGuState.projectionSerial != state->projectionSerial)))) {
        if (identity) {
            sceGuSetMatrix(GU_PROJECTION, &sPspGfxGuIdentityMatrix);
        } else {
            sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4*) state->projectionMatrix);
        }
    }
    if (!sPspGfxGuState.matrixValid || !sPspGfxGuState.viewModelIdentity) {
        sceGuSetMatrix(GU_VIEW, &sPspGfxGuIdentityMatrix);
        sceGuSetMatrix(GU_MODEL, &sPspGfxGuIdentityMatrix);
    }
    sPspGfxGuState.projectionIdentity = identity;
    sPspGfxGuState.projectionMatrix = state->projectionMatrix;
    sPspGfxGuState.projectionSerial = state->projectionSerial;
    sPspGfxGuState.viewModelIdentity = 1;
    sPspGfxGuState.matrixValid = 1;
}

static int psp_gfx_gu_prepare_texture(const PspGfxDrawState* state) {
    const void* pixels;
    u32 width;
    u32 height;
    int wrapS;
    int wrapT;
    int textureFunction;
    int pointFilter = state->pointFilter != 0;

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

    if (!sPspGfxGuState.textureValid) {
        sceGuTexMode(GU_PSM_8888, 0, 0, 1);
        sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
        sceGuTexLevelMode(GU_TEXTURE_CONST, 0.0f);
        sceGuTexScale(1.0f, 1.0f);
        sceGuTexOffset(0.0f, 0.0f);
    }
    if (!sPspGfxGuState.textureValid || !sPspGfxGuState.textureBound ||
        !PspGfxTextureHandle_Equals(sPspGfxGuState.texture, state->texture)) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
        sceGuTexImage(0, (int) width, (int) height, (int) width, pixels);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
        sPspGfxGuState.texture = state->texture;
        sPspGfxGuState.textureBound = 1;
    }
    if (!sPspGfxGuState.textureValid || (sPspGfxGuState.textureFunction != textureFunction)) {
        sceGuTexFunc(textureFunction, GU_TCC_RGBA);
        sPspGfxGuState.textureFunction = textureFunction;
    }
    if (!sPspGfxGuState.textureValid || (sPspGfxGuState.textureEnvColor != state->textureEnvColor)) {
        sceGuTexEnvColor(state->textureEnvColor & 0x00FFFFFFU);
        sPspGfxGuState.textureEnvColor = state->textureEnvColor;
    }
    if (!sPspGfxGuState.textureValid || (sPspGfxGuState.textureWrapS != wrapS) ||
        (sPspGfxGuState.textureWrapT != wrapT)) {
        sceGuTexWrap(wrapS, wrapT);
        sPspGfxGuState.textureWrapS = wrapS;
        sPspGfxGuState.textureWrapT = wrapT;
    }
    if (!sPspGfxGuState.textureValid || (sPspGfxGuState.texturePointFilter != pointFilter)) {
        sceGuTexFilter(pointFilter ? GU_NEAREST : GU_LINEAR, pointFilter ? GU_NEAREST : GU_LINEAR);
        sPspGfxGuState.texturePointFilter = pointFilter;
    }
    sPspGfxGuState.textureValid = 1;
    return 1;
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

    psp_gfx_gu_state_invalidate();
    sPspGfxGuReservationFrame++;
    if (sPspGfxGuReservationFrame == 0) {
        sPspGfxGuReservationFrame = 1;
    }
    for (i = 0; i < PSP_GFX_GU_RESERVATION_SLOTS; i++) {
        sPspGfxGuReservations[i].active = 0;
    }
    sPspGfxGuReservationFrameActive = 1;
    sPspGfxGuScissorEnabled = 0;
}

void PspGfxBackendGu_EndFrame(void) {
    u32 i;

    sPspGfxGuReservationFrameActive = 0;
    for (i = 0; i < PSP_GFX_GU_RESERVATION_SLOTS; i++) {
        sPspGfxGuReservations[i].active = 0;
    }
}

static void psp_gfx_gu_prepare_alpha_blend(const PspGfxDrawState* state, int textured) {
    int alphaTest = textured && state->alphaTest;
    int blend = textured && state->blend;

    if (alphaTest) {
        int reference = (state->alphaTest > 1) ? PSP_GFX_GU_ALPHA_HALF : 0;

        if (!sPspGfxGuState.alphaTestReferenceValid ||
            (sPspGfxGuState.alphaTestReference != reference)) {
            sceGuAlphaFunc(GU_GREATER, reference, 0xFF);
            sPspGfxGuState.alphaTestReference = reference;
            sPspGfxGuState.alphaTestReferenceValid = 1;
        }
        psp_gfx_gu_set_capability(GU_ALPHA_TEST, 1, &sPspGfxGuState.alphaTestEnabled);
    } else {
        psp_gfx_gu_set_capability(GU_ALPHA_TEST, 0, &sPspGfxGuState.alphaTestEnabled);
    }

    psp_gfx_gu_set_capability(GU_BLEND, blend, &sPspGfxGuState.blendEnabled);
    if (blend && (!sPspGfxGuState.blendFuncValid ||
                  (sPspGfxGuState.blendPremultiplied != state->premultiplied))) {
        if (state->premultiplied) {
            sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, PSP_GFX_GU_FIXED_ONE, 0);
        } else {
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        }
        sPspGfxGuState.blendPremultiplied = state->premultiplied;
        sPspGfxGuState.blendFuncValid = 1;
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

    psp_gfx_gu_set_matrices(state);
    psp_gfx_gu_set_capability(GU_DEPTH_TEST, state->depthTest, &sPspGfxGuState.depthTestEnabled);
    if (state->depthTest && (!sPspGfxGuState.depthFunctionValid ||
                             (sPspGfxGuState.depthFunction != PSP_GFX_GU_DEPTH_FUNC))) {
        sceGuDepthFunc(PSP_GFX_GU_DEPTH_FUNC);
        sPspGfxGuState.depthFunction = PSP_GFX_GU_DEPTH_FUNC;
        sPspGfxGuState.depthFunctionValid = 1;
    }
    if (!sPspGfxGuState.valid || (sPspGfxGuState.depthWrite != state->depthWrite)) {
        sceGuDepthMask(state->depthWrite ? GU_FALSE : GU_TRUE);
        sPspGfxGuState.depthWrite = state->depthWrite;
    }
    psp_gfx_gu_set_capability(GU_TEXTURE_2D, textured, &sPspGfxGuState.textureEnabled);
    psp_gfx_gu_prepare_alpha_blend(state, textured);
    psp_gfx_gu_prepare_native_fog(state);
    psp_gfx_gu_set_capability(GU_LIGHTING, 0, &sPspGfxGuState.lightingEnabled);
    psp_gfx_gu_set_capability(GU_CULL_FACE, 0, &sPspGfxGuState.cullEnabled);
    psp_gfx_gu_set_capability(GU_CLIP_PLANES, 1, &sPspGfxGuState.clipPlanesEnabled);
    if (!sPspGfxGuState.valid || (sPspGfxGuState.shadeModel != GU_SMOOTH)) {
        sceGuShadeModel(GU_SMOOTH);
        sPspGfxGuState.shadeModel = GU_SMOOTH;
    }
    sPspGfxGuState.valid = 1;
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

    sPspGfxGuScissorUlx = ulx;
    sPspGfxGuScissorUly = uly;
    sPspGfxGuScissorLrx = lrx;
    sPspGfxGuScissorLry = lry;
    sPspGfxGuScissorEnabled = 1;

    x0 = display->viewport_x + (int) (ulx * scaleX);
    y0 = display->viewport_y + (int) (uly * scaleY);
    x1 = display->viewport_x + (int) ((lrx * scaleX) + 0.5f);
    y1 = display->viewport_y + (int) ((lry * scaleY) + 0.5f);
    sceGuScissor(x0, y0, x1 - x0, y1 - y0);
    sceGuEnable(GU_SCISSOR_TEST);
}

void PspGfxBackend_ClearScissor(void) {
    sPspGfxGuScissorEnabled = 0;
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

    psp_gfx_gu_replay_capture_colored(vertices, vertexCount, state, PSP_GFX_GU_REPLAY_TRIANGLES);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_prepare_colored_draw(state)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        psp_gfx_gu_replay_note_draw_failure();
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
        psp_gfx_gu_replay_note_draw_failure();
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

    psp_gfx_gu_replay_capture_colored(reservation->vertices, vertexCount, state, PSP_GFX_GU_REPLAY_TRIANGLES);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_prepare_colored_draw(state)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        psp_gfx_gu_replay_note_draw_failure();
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

    psp_gfx_gu_replay_capture_fog(vertices, vertexCount, state);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    psp_gfx_gu_state_invalidate();
    if (!psp_gfx_gu_select_viewport(state->viewport)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        psp_gfx_gu_replay_note_draw_failure();
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
        psp_gfx_gu_replay_note_draw_failure();
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

    psp_gfx_gu_replay_capture_colored(vertices, vertexCount, state, PSP_GFX_GU_REPLAY_SPRITES);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (!psp_gfx_gu_prepare_colored_draw(state)) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        psp_gfx_gu_replay_note_draw_failure();
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
        psp_gfx_gu_replay_note_draw_failure();
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

    if ((lrx <= ulx) || (lry <= uly)) {
        return;
    }
    psp_gfx_gu_state_invalidate();
    if (!psp_gfx_gu_select_viewport(viewport)) {
        return;
    }

    if (sPspGfxGuReplayCapturing && !sPspGfxGuReplayActive) {
        PspGfxGuReplayPacket* packet;

        if (sPspGfxGuReplayPacketCount >= PSP_GFX_GU_REPLAY_CACHE_DRAWS) {
            psp_gfx_gu_replay_capture_failed();
        } else {
            packet = &sPspGfxGuReplayPackets[sPspGfxGuReplayPacketCount++];
            packet->type = PSP_GFX_GU_REPLAY_SOLID_RECT;
            packet->ulx = ulx;
            packet->uly = uly;
            packet->lrx = lrx;
            packet->lry = lry;
            packet->color = color;
            packet->blend = blend;
            packet->viewport = viewport;
            psp_gfx_gu_replay_context(packet);
        }
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
        psp_gfx_gu_replay_note_draw_failure();
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
    psp_gfx_gu_replay_clear();
    sPspGfxGuReplayCapturing = 1;
}

void PspGfxBackend_EndReplayCache(void) {
    sPspGfxGuReplayCapturing = 0;
    sPspGfxGuReplayReady = !sPspGfxGuReplayCaptureFailed && (sPspGfxGuReplayPacketCount != 0);
}

int PspGfxBackend_ReplayCacheReady(void) {
    if (sPspGfxGuReplayReady && !psp_gfx_gu_replay_textures_valid()) {
        PspGfxBackend_ReplayCacheInvalidate();
    }
    return sPspGfxGuReplayReady;
}

void PspGfxBackend_ReplayCacheInvalidate(void) {
    psp_gfx_gu_replay_clear();
}

void PspGfxBackend_ReplayCache(void) {
    u32 i;

    if (!PspGfxBackend_ReplayCacheReady()) {
        return;
    }
    sPspGfxGuReplayActive = 1;
    for (i = 0; i < sPspGfxGuReplayPacketCount; i++) {
        const PspGfxGuReplayPacket* packet = &sPspGfxGuReplayPackets[i];

        psp_gfx_gu_replay_restore_context(packet);
        switch (packet->type) {
            case PSP_GFX_GU_REPLAY_TRIANGLES:
                PspGfxBackend_DrawTriangles(&sPspGfxGuReplayVertices[packet->first], packet->count, &packet->state);
                break;
            case PSP_GFX_GU_REPLAY_SPRITES:
                PspGfxBackend_DrawSprites(&sPspGfxGuReplayVertices[packet->first], packet->count, &packet->state);
                break;
            case PSP_GFX_GU_REPLAY_SOLID_RECT:
                PspGfxBackend_DrawSolidRect(packet->ulx, packet->uly, packet->lrx, packet->lry, packet->color,
                                            packet->blend, packet->viewport);
                break;
            case PSP_GFX_GU_REPLAY_FOG:
                PspGfxBackend_DrawFogTriangles(&sPspGfxGuReplayFogVertices[packet->first], packet->count,
                                               &packet->fogState);
                break;
        }
    }
    sPspGfxGuReplayActive = 0;
}
