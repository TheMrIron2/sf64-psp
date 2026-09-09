#include <n64psp/display.h>

#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/display.h"
#include "src/psp/profiler.h"
#include "macros.h"

#include <pspkernel.h>
#include <pspgu.h>

#define PSP_GFX_GU_N64_WIDTH 320.0f
#define PSP_GFX_GU_N64_HEIGHT 240.0f
#define PSP_GFX_GU_LIST_BYTES (262144U * sizeof(unsigned int))
#define PSP_GFX_GU_LIST_DRAW_RESERVE 4096U

typedef struct {
    u32 color;
    float x;
    float y;
    float z;
} PspGfxGuColorVertex;

typedef char PspGfxGuColorVertexSizeCheck[(sizeof(PspGfxGuColorVertex) == 16) ? 1 : -1];

static const ScePspFMatrix4 sPspGfxGuIdentityMatrix __attribute__((aligned(16))) = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

static int psp_gfx_gu_select_viewport(PspGfxViewportPolicy policy) {
    const n64psp_display_config* display = PspDisplay_GetConfig();
    int x;
    int y;
    int width;
    int height;

    if ((policy == PSP_GFX_VIEWPORT_FULL) || (policy == PSP_GFX_VIEWPORT_WIDE_UI)) {
        x = display->viewport_x;
        y = display->viewport_y;
        width = display->viewport_width;
        height = display->viewport_height;
    } else if ((policy == PSP_GFX_VIEWPORT_AUTO) || (policy == PSP_GFX_VIEWPORT_CENTERED_UI)) {
        if (display->mode == N64PSP_DISPLAY_PSP_480X272) {
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
    } else {
        return 0;
    }

    sceGuOffset(2048 - (display->framebuffer_width / 2), 2048 - (display->framebuffer_height / 2));
    sceGuViewport(2048 - (display->framebuffer_width / 2) + x + (width / 2),
                  2048 - (display->framebuffer_height / 2) + y + (height / 2), width, height);
    return 1;
}

static void psp_gfx_gu_load_identity(void) {
    sceGuSetMatrix(GU_PROJECTION, &sPspGfxGuIdentityMatrix);
    sceGuSetMatrix(GU_VIEW, &sPspGfxGuIdentityMatrix);
    sceGuSetMatrix(GU_MODEL, &sPspGfxGuIdentityMatrix);
}

static PspGfxGuColorVertex* psp_gfx_gu_alloc_color_vertices(u32 vertexCount) {
    u32 bytes;
    u32 listBytes;
    int checkedListBytes;
    PspGfxGuColorVertex* vertices;

    if ((vertexCount == 0) || (vertexCount > (0x7FFFFFFFU / sizeof(PspGfxGuColorVertex)))) {
        return NULL;
    }

    bytes = vertexCount * sizeof(PspGfxGuColorVertex);
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

    vertices = (PspGfxGuColorVertex*) sceGuGetMemory((int) bytes);
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

static int psp_gfx_gu_prepare_colored_draw(const PspGfxDrawState* state) {
    if ((state == NULL) || !psp_gfx_gu_select_viewport(state->viewport)) {
        return 0;
    }

    if (PspGfxTextureHandle_IsValid(state->texture) || state->fog) {
        return 0;
    }

    if (state->pretransformed || (state->projectionMatrix == NULL)) {
        psp_gfx_gu_load_identity();
    } else {
        sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4*) state->projectionMatrix);
        sceGuSetMatrix(GU_VIEW, &sPspGfxGuIdentityMatrix);
        sceGuSetMatrix(GU_MODEL, &sPspGfxGuIdentityMatrix);
    }

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    return 1;
}

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
    const n64psp_display_config* display = PspDisplay_GetConfig();

    sceGuScissor(0, 0, display->framebuffer_width, display->framebuffer_height);
    sceGuEnable(GU_SCISSOR_TEST);
}

void PspGfxBackend_DrawTriangles(const PspGfxVertex* vertices, u32 vertexCount, const PspGfxDrawState* state) {
    PspGfxGuColorVertex* guVertices;
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

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    guVertices = psp_gfx_gu_alloc_color_vertices(vertexCount);
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

    bytes = vertexCount * sizeof(PspGfxGuColorVertex);
    sceKernelDcacheWritebackRange(guVertices, bytes);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, vertexCount, 0, guVertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    psp_gfx_gu_count_vertex_copy(bytes);
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
    PspGfxGuColorVertex source[6] __attribute__((aligned(16)));
    PspGfxGuColorVertex* vertices;
    int i;

    if (blend || (lrx <= ulx) || (lry <= uly) || !psp_gfx_gu_select_viewport(viewport)) {
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
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    vertices = psp_gfx_gu_alloc_color_vertices(6);
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
    (void) x;
    (void) y;
}

void PspGfxBackend_BeginReplayCache(void) {
}

void PspGfxBackend_EndReplayCache(void) {
}

void PspGfxBackend_ReplayCache(void) {
}
