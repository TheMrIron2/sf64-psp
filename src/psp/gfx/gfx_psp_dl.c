#include "src/psp/gfx/gfx_psp_dl.h"

#include "macros.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"

#include <stdint.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif
#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#define PSP_GFX_DL_MAX_DEPTH 8
#define PSP_GFX_DL_MAX_COMMANDS 8192
#define PSP_GFX_DL_MAX_VERTICES 64
#define PSP_GFX_DL_BATCH_VERTICES 3072
#define PSP_GFX_DL_MTX_STACK_DEPTH 32
#define PSP_GFX_DL_CLIP_PLANES 6
#define PSP_GFX_DL_MAX_CLIP_VERTICES 12

#define PSP_GFX_OP_F3D_SPNOOP 0x00
#define PSP_GFX_OP_F3D_MTX 0x01
#define PSP_GFX_OP_F3D_MOVEMEM 0x03
#define PSP_GFX_OP_F3D_VTX 0x04
#define PSP_GFX_OP_F3D_DL 0x06
#define PSP_GFX_OP_F3D_TRI1 0xbf
#define PSP_GFX_OP_F3D_CULLDL 0xbe
#define PSP_GFX_OP_F3D_POPMTX 0xbd
#define PSP_GFX_OP_F3D_MOVEWORD 0xbc
#define PSP_GFX_OP_F3D_TEXTURE 0xbb
#define PSP_GFX_OP_F3D_SETOTHERMODE_H 0xba
#define PSP_GFX_OP_F3D_SETOTHERMODE_L 0xb9
#define PSP_GFX_OP_F3D_ENDDL 0xb8
#define PSP_GFX_OP_F3D_SETGEOMETRYMODE 0xb7
#define PSP_GFX_OP_F3D_CLEARGEOMETRYMODE 0xb6
#define PSP_GFX_OP_F3D_RDPHALF_1 0xb4
#define PSP_GFX_OP_F3D_RDPHALF_2 0xb3
#define PSP_GFX_OP_F3D_MODIFYVTX 0xb2
#define PSP_GFX_OP_F3D_TRI2 0xb1

typedef struct {
    float x;
    float y;
    float z;
    float clipX;
    float clipY;
    float clipZ;
    float clipW;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    s16 s;
    s16 t;
    u8 clipCode;
    int valid;
} PspGfxDlVertex;

typedef struct {
    float x;
    float y;
    float z;
    float w;
    float r;
    float g;
    float b;
    float a;
    float u;
    float v;
} PspGfxDlClipVertex;

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    s8 x;
    s8 y;
    s8 z;
} PspGfxDlLight;

typedef enum {
    PSP_GFX_DL_COMBINE_UNKNOWN,
    PSP_GFX_DL_COMBINE_SHADE,
    PSP_GFX_DL_COMBINE_PRIMITIVE,
    PSP_GFX_DL_COMBINE_DECAL_RGB,
    PSP_GFX_DL_COMBINE_DECAL_RGBA,
    PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA,
    PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA,
    PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA,
} PspGfxDlCombineMode;

typedef struct {
    PspGfxDlStats stats;
    PspGfxDlVertex vertices[PSP_GFX_DL_MAX_VERTICES];
    PspGfxPspglColorVertex batch[PSP_GFX_DL_BATCH_VERTICES];
    float modelview[4][4];
    float projection[4][4];
    float modelviewStack[PSP_GFX_DL_MTX_STACK_DEPTH][4][4];
    u32 batchCount;
    u32 modelviewStackDepth;
    u32 matrixFlagsSeen;
    s16 viewportScaleX;
    s16 viewportScaleY;
    s16 viewportTransX;
    s16 viewportTransY;
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
    const void* textureImage;
    const u16* texturePalette;
    u32 textureFormat;
    u32 textureSize;
    u32 texturePaletteIndex;
    u32 textureWidth;
    u32 textureHeight;
    u32 textureUploadWidth;
    u32 textureUploadHeight;
    u32 textureId;
    int textureUploadAttempted;
    u32 batchTextureId;
    u32 geometryMode;
    u32 lightCount;
    PspGfxDlLight lights[7];
    u8 ambientR;
    u8 ambientG;
    u8 ambientB;
    u8 primitiveR;
    u8 primitiveG;
    u8 primitiveB;
    u8 primitiveA;
    PspGfxDlCombineMode combineMode;
    PspGfxPspglTextureEnv batchTextureEnv;
    int combineUsesTextureAlpha;
    int textureEnabled;
    int batchAlphaTest;
    int batchBlend;
    int batchDepthTest;
    int batchDepthWrite;
    u32 otherModeL;
    int hasModelview;
    int hasProjection;
    int hasVertexBounds;
} PspGfxDlContext;

static PspGfxDlContext sPspGfxDlContext;
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
static int sLoggedFirstDrawableTask;
#endif

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred);

static int psp_gfx_dl_alpha_test_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha &&
           (((ctx->otherModeL & 3U) != G_AC_NONE) || ((ctx->otherModeL & CVG_X_ALPHA) != 0));
}

static int psp_gfx_dl_blend_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha && ((ctx->otherModeL & FORCE_BL) != 0);
}

static u8 psp_gfx_dl_opcode(const Gfx* gfx) {
    return (u8) (gfx->words.w0 >> 24);
}

static int psp_gfx_dl_is_native_ptr(uintptr_t ptr) {
    return (ptr >= 0x08000000U) && (ptr < 0x0A000000U);
}

static const void* psp_gfx_dl_resolve_ptr(u32 raw) {
    uintptr_t ptr = (uintptr_t) raw;
    u32 segment;
    u32 base;

    if (ptr == 0) {
        return NULL;
    }
    if (psp_gfx_dl_is_native_ptr(ptr)) {
        return (const void*) ptr;
    }

    segment = (raw >> 24) & 0xF;
    base = gSegments[segment];
    if (base == 0) {
        return NULL;
    }
    return SEGMENTED_TO_VIRTUAL(raw);
}

static int psp_gfx_dl_is_end(u8 opcode) {
    return opcode == PSP_GFX_OP_F3D_ENDDL;
}

static int psp_gfx_dl_is_noop_state(u8 opcode) {
    if (opcode >= G_TEXRECT) {
        return 1;
    }

    switch (opcode) {
        case PSP_GFX_OP_F3D_SPNOOP:
        case PSP_GFX_OP_F3D_MOVEMEM:
        case PSP_GFX_OP_F3D_CULLDL:
        case PSP_GFX_OP_F3D_POPMTX:
        case PSP_GFX_OP_F3D_MOVEWORD:
        case PSP_GFX_OP_F3D_TEXTURE:
        case PSP_GFX_OP_F3D_SETOTHERMODE_H:
        case PSP_GFX_OP_F3D_SETOTHERMODE_L:
        case PSP_GFX_OP_F3D_SETGEOMETRYMODE:
        case PSP_GFX_OP_F3D_CLEARGEOMETRYMODE:
        case PSP_GFX_OP_F3D_RDPHALF_1:
        case PSP_GFX_OP_F3D_RDPHALF_2:
        case PSP_GFX_OP_F3D_MODIFYVTX:
            return 1;
        default:
            return 0;
    }
}

static u8 psp_gfx_dl_decode_tri_index(u32 packed) {
    return (u8) (packed / 2);
}

static u8 psp_gfx_dl_clip_code(float x, float y, float z, float w) {
    u8 code = 0;

    if (x < -w) {
        code |= 1U << 0;
    }
    if (x > w) {
        code |= 1U << 1;
    }
    if (y < -w) {
        code |= 1U << 2;
    }
    if (y > w) {
        code |= 1U << 3;
    }
    if (z < -w) {
        code |= 1U << 4;
    }
    if (z > w) {
        code |= 1U << 5;
    }
    return code;
}

static void psp_gfx_dl_count_unsupported(PspGfxDlContext* ctx, u32 opcode) {
    if (ctx->stats.unsupportedCount == 0) {
        ctx->stats.firstUnsupportedOpcode = opcode;
    }
    ctx->stats.unsupportedCount++;
}

static void psp_gfx_dl_identity(float mtx[4][4]) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            mtx[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }
}

static void psp_gfx_dl_mtx_l2f(float out[4][4], const Mtx* src) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            s32 fixed = ((s32) ((u32) src->u.i[row][col] << 16)) | src->u.f[row][col];
            out[row][col] = fixed / 65536.0f;
        }
    }
}

static void psp_gfx_dl_mtx_copy(float out[4][4], float in[4][4]) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row][col] = in[row][col];
        }
    }
}

static void psp_gfx_dl_mtx_mul(float out[4][4], float a[4][4], float b[4][4]) {
    float result[4][4];
    u32 row;
    u32 col;
    u32 k;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            result[row][col] = 0.0f;
            for (k = 0; k < 4; k++) {
                result[row][col] += a[row][k] * b[k][col];
            }
        }
    }
    psp_gfx_dl_mtx_copy(out, result);
}

static void psp_gfx_dl_transform(float mtx[4][4], float inX, float inY, float inZ, float* outX, float* outY,
                                 float* outZ, float* outW) {
    *outX = (mtx[0][0] * inX) + (mtx[1][0] * inY) + (mtx[2][0] * inZ) + mtx[3][0];
    *outY = (mtx[0][1] * inX) + (mtx[1][1] * inY) + (mtx[2][1] * inZ) + mtx[3][1];
    *outZ = (mtx[0][2] * inX) + (mtx[1][2] * inY) + (mtx[2][2] * inZ) + mtx[3][2];
    *outW = (mtx[0][3] * inX) + (mtx[1][3] * inY) + (mtx[2][3] * inZ) + mtx[3][3];
}

static int psp_gfx_dl_transform_vertex(PspGfxDlContext* ctx, const Vtx* in, PspGfxDlVertex* out) {
    float x = in->v.ob[0];
    float y = in->v.ob[1];
    float z = in->v.ob[2];
    float w = 1.0f;
    float clipX;
    float clipY;
    float clipZ;
    float clipW;

    if (ctx->hasModelview) {
        psp_gfx_dl_transform(ctx->modelview, x, y, z, &x, &y, &z, &w);
    }
    if (!ctx->hasProjection) {
        out->x = x / 320.0f;
        out->y = -y / 240.0f;
        out->z = z / 4096.0f;
        out->clipX = out->x;
        out->clipY = out->y;
        out->clipZ = out->z;
        out->clipW = 1.0f;
        out->clipCode = psp_gfx_dl_clip_code(out->x, out->y, out->z, out->clipW);
        return 1;
    }

    psp_gfx_dl_transform(ctx->projection, x, y, z, &clipX, &clipY, &clipZ, &clipW);
    if ((clipW > -0.001f) && (clipW < 0.001f)) {
        ctx->stats.nearZeroWCount++;
        return 0;
    }

    out->x = clipX / clipW;
    out->y = clipY / clipW;
    out->z = clipZ / clipW;
    out->clipX = clipX;
    out->clipY = clipY;
    out->clipZ = clipZ;
    out->clipW = clipW;
    out->clipCode = psp_gfx_dl_clip_code(clipX, clipY, clipZ, clipW);
    if (clipW < 0.0f) {
        ctx->stats.behindEyeVertexCount++;
    }
    return 1;
}

static void psp_gfx_dl_handle_mtx(PspGfxDlContext* ctx, const Gfx* gfx) {
    const Mtx* src = (const Mtx*) psp_gfx_dl_resolve_ptr(gfx->words.w1);
    u32 flags = (gfx->words.w0 >> 16) & 0xFF;
    float loaded[4][4];
    float (*target)[4];
    int* hasTarget;

    ctx->stats.mtxCount++;
    ctx->matrixFlagsSeen |= flags;
    if (src == NULL) {
        ctx->stats.matrixPointerRejected++;
        return;
    }

    if ((flags & G_MTX_PROJECTION) != 0) {
        target = ctx->projection;
        hasTarget = &ctx->hasProjection;
    } else {
        target = ctx->modelview;
        hasTarget = &ctx->hasModelview;
        if ((flags & G_MTX_PUSH) != 0) {
            if (ctx->modelviewStackDepth < PSP_GFX_DL_MTX_STACK_DEPTH) {
                psp_gfx_dl_mtx_copy(ctx->modelviewStack[ctx->modelviewStackDepth], ctx->modelview);
                ctx->modelviewStackDepth++;
                ctx->stats.mtxPushCount++;
            } else {
                ctx->stats.mtxStackRejected++;
            }
        }
    }

    psp_gfx_dl_mtx_l2f(loaded, src);
    if ((flags & G_MTX_LOAD) != 0) {
        psp_gfx_dl_mtx_copy(target, loaded);
    } else if (*hasTarget) {
        psp_gfx_dl_mtx_mul(target, loaded, target);
    } else {
        psp_gfx_dl_mtx_copy(target, loaded);
    }
    *hasTarget = 1;
}

static void psp_gfx_dl_handle_pop_mtx(PspGfxDlContext* ctx) {
    if (ctx->modelviewStackDepth == 0) {
        ctx->stats.mtxStackRejected++;
        return;
    }

    ctx->modelviewStackDepth--;
    psp_gfx_dl_mtx_copy(ctx->modelview, ctx->modelviewStack[ctx->modelviewStackDepth]);
    ctx->hasModelview = 1;
    ctx->stats.mtxPopCount++;
}

static void psp_gfx_dl_handle_movemem(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 index = (gfx->words.w0 >> 16) & 0xFF;
    const Vp* viewport;
    const Light* light;
    u32 lightSlot;

    if (index == G_MV_VIEWPORT) {
        viewport = (const Vp*) psp_gfx_dl_resolve_ptr(gfx->words.w1);
        if (viewport == NULL) {
            psp_gfx_dl_count_unsupported(ctx, PSP_GFX_OP_F3D_MOVEMEM);
            return;
        }
        ctx->stats.viewportCount++;
        ctx->viewportScaleX = viewport->vp.vscale[0];
        ctx->viewportScaleY = viewport->vp.vscale[1];
        ctx->viewportTransX = viewport->vp.vtrans[0];
        ctx->viewportTransY = viewport->vp.vtrans[1];
        return;
    }

    if ((index < G_MV_L0) || (index > G_MV_L7) || (((index - G_MV_L0) & 1U) != 0)) {
        return;
    }

    light = (const Light*) psp_gfx_dl_resolve_ptr(gfx->words.w1);
    if (light == NULL) {
        return;
    }
    lightSlot = (index - G_MV_L0) >> 1;
    if (lightSlot < ctx->lightCount) {
        ctx->lights[lightSlot].r = light->l.col[0];
        ctx->lights[lightSlot].g = light->l.col[1];
        ctx->lights[lightSlot].b = light->l.col[2];
        ctx->lights[lightSlot].x = light->l.dir[0];
        ctx->lights[lightSlot].y = light->l.dir[1];
        ctx->lights[lightSlot].z = light->l.dir[2];
    } else if (lightSlot == ctx->lightCount) {
        ctx->ambientR = light->l.col[0];
        ctx->ambientG = light->l.col[1];
        ctx->ambientB = light->l.col[2];
    }
}

static void psp_gfx_dl_flush(PspGfxDlContext* ctx) {
    if (ctx->batchCount == 0) {
        return;
    }
    PspGfxPspgl_DrawColoredTriangles(ctx->batch, ctx->batchCount, ctx->batchTextureId, ctx->batchTextureEnv,
                                     ctx->batchAlphaTest, ctx->batchBlend, ctx->batchDepthTest,
                                     ctx->batchDepthWrite);
    ctx->stats.drawVertexCount += ctx->batchCount;
    ctx->batchCount = 0;
}

static void psp_gfx_dl_set_batch_texture(PspGfxDlContext* ctx, u32 textureId, PspGfxPspglTextureEnv textureEnv,
                                         int alphaTest, int blend) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchTextureId != textureId) || (ctx->batchTextureEnv != textureEnv) ||
         (ctx->batchAlphaTest != alphaTest) || (ctx->batchBlend != blend))) {
        psp_gfx_dl_flush(ctx);
    }
    ctx->batchTextureId = textureId;
    ctx->batchTextureEnv = textureEnv;
    ctx->batchAlphaTest = alphaTest;
    ctx->batchBlend = blend;
}

static void psp_gfx_dl_set_batch_depth(PspGfxDlContext* ctx, int depthTest, int depthWrite) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchDepthTest != depthTest) || (ctx->batchDepthWrite != depthWrite))) {
        psp_gfx_dl_flush(ctx);
    }
    ctx->batchDepthTest = depthTest;
    ctx->batchDepthWrite = depthWrite;
}

static int psp_gfx_dl_vertex_is_valid(PspGfxDlContext* ctx, u8 index) {
    return (index < PSP_GFX_DL_MAX_VERTICES) && ctx->vertices[index].valid;
}

static void psp_gfx_dl_build_clip_vertex(PspGfxDlContext* ctx, const PspGfxDlVertex* src,
                                         PspGfxDlClipVertex* dst) {
    dst->x = src->clipX;
    dst->y = src->clipY;
    dst->z = src->clipZ;
    dst->w = src->clipW;
    if ((ctx->combineMode == PSP_GFX_DL_COMBINE_PRIMITIVE) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA) ||
        ((ctx->geometryMode & G_SHADE) == 0)) {
        dst->r = (float) ctx->primitiveR / 255.0f;
        dst->g = (float) ctx->primitiveG / 255.0f;
        dst->b = (float) ctx->primitiveB / 255.0f;
        dst->a = (float) ctx->primitiveA / 255.0f;
    } else if ((ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGB) ||
               (ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGBA)) {
        dst->r = 1.0f;
        dst->g = 1.0f;
        dst->b = 1.0f;
        dst->a = (float) src->a / 255.0f;
    } else {
        dst->r = (float) src->r / 255.0f;
        dst->g = (float) src->g / 255.0f;
        dst->b = (float) src->b / 255.0f;
        dst->a = (float) src->a / 255.0f;
    }
    if ((ctx->textureUploadWidth != 0) && (ctx->textureUploadHeight != 0)) {
        dst->u = ((float) src->s / 32.0f) / (float) ctx->textureUploadWidth;
        dst->v = ((float) src->t / 32.0f) / (float) ctx->textureUploadHeight;
    } else {
        dst->u = 0.0f;
        dst->v = 0.0f;
    }
}

static void psp_gfx_dl_emit_clip_vertex(PspGfxDlContext* ctx, const PspGfxDlClipVertex* src) {
    PspGfxPspglColorVertex* dst;
    float inverseW;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush(ctx);
    }

    inverseW = 1.0f / src->w;
    dst = &ctx->batch[ctx->batchCount++];
    dst->x = src->x * inverseW;
    dst->y = src->y * inverseW;
    dst->z = src->z * inverseW;
    dst->r = src->r;
    dst->g = src->g;
    dst->b = src->b;
    dst->a = src->a;
    dst->u = src->u;
    dst->v = src->v;
}

static float psp_gfx_dl_clip_distance(const PspGfxDlClipVertex* vertex, u32 plane) {
    switch (plane) {
        case 0:
            return vertex->x + vertex->w;
        case 1:
            return vertex->w - vertex->x;
        case 2:
            return vertex->y + vertex->w;
        case 3:
            return vertex->w - vertex->y;
        case 4:
            return vertex->z + vertex->w;
        default:
            return vertex->w - vertex->z;
    }
}

static void psp_gfx_dl_interpolate_clip_vertex(PspGfxDlClipVertex* out, const PspGfxDlClipVertex* from,
                                               const PspGfxDlClipVertex* to, float t) {
    out->x = from->x + ((to->x - from->x) * t);
    out->y = from->y + ((to->y - from->y) * t);
    out->z = from->z + ((to->z - from->z) * t);
    out->w = from->w + ((to->w - from->w) * t);
    out->r = from->r + ((to->r - from->r) * t);
    out->g = from->g + ((to->g - from->g) * t);
    out->b = from->b + ((to->b - from->b) * t);
    out->a = from->a + ((to->a - from->a) * t);
    out->u = from->u + ((to->u - from->u) * t);
    out->v = from->v + ((to->v - from->v) * t);
}

static u32 psp_gfx_dl_clip_polygon_plane(const PspGfxDlClipVertex* input, u32 inputCount,
                                         PspGfxDlClipVertex* output, u32 plane) {
    const PspGfxDlClipVertex* previous;
    float previousDistance;
    int previousInside;
    u32 outputCount = 0;
    u32 i;

    if (inputCount == 0) {
        return 0;
    }

    previous = &input[inputCount - 1];
    previousDistance = psp_gfx_dl_clip_distance(previous, plane);
    previousInside = previousDistance >= 0.0f;
    for (i = 0; i < inputCount; i++) {
        const PspGfxDlClipVertex* current = &input[i];
        float currentDistance = psp_gfx_dl_clip_distance(current, plane);
        int currentInside = currentDistance >= 0.0f;

        if (currentInside != previousInside) {
            float denominator = previousDistance - currentDistance;
            float t = (denominator != 0.0f) ? (previousDistance / denominator) : 0.0f;

            if (outputCount < PSP_GFX_DL_MAX_CLIP_VERTICES) {
                psp_gfx_dl_interpolate_clip_vertex(&output[outputCount++], previous, current, t);
            }
        }
        if (currentInside && (outputCount < PSP_GFX_DL_MAX_CLIP_VERTICES)) {
            output[outputCount++] = *current;
        }

        previous = current;
        previousDistance = currentDistance;
        previousInside = currentInside;
    }
    return outputCount;
}

static u32 psp_gfx_dl_emit_clipped_triangle(PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                            const PspGfxDlVertex* b, const PspGfxDlVertex* c) {
    PspGfxDlClipVertex buffers[2][PSP_GFX_DL_MAX_CLIP_VERTICES];
    PspGfxDlClipVertex* input = buffers[0];
    PspGfxDlClipVertex* output = buffers[1];
    PspGfxDlClipVertex* swap;
    u32 vertexCount = 3;
    u32 plane;
    u32 i;

    psp_gfx_dl_build_clip_vertex(ctx, a, &input[0]);
    psp_gfx_dl_build_clip_vertex(ctx, b, &input[1]);
    psp_gfx_dl_build_clip_vertex(ctx, c, &input[2]);
    for (plane = 0; plane < PSP_GFX_DL_CLIP_PLANES; plane++) {
        vertexCount = psp_gfx_dl_clip_polygon_plane(input, vertexCount, output, plane);
        if (vertexCount < 3) {
            return 0;
        }
        swap = input;
        input = output;
        output = swap;
    }

    for (i = 1; i + 1 < vertexCount; i++) {
        psp_gfx_dl_emit_clip_vertex(ctx, &input[0]);
        psp_gfx_dl_emit_clip_vertex(ctx, &input[i]);
        psp_gfx_dl_emit_clip_vertex(ctx, &input[i + 1]);
    }
    return vertexCount - 2;
}

static void psp_gfx_dl_emit_tri(PspGfxDlContext* ctx, u8 a, u8 b, u8 c) {
    const PspGfxDlVertex* va;
    const PspGfxDlVertex* vb;
    const PspGfxDlVertex* vc;
    float area;
    u8 combinedClipCode;
    u8 sharedClipCode;
    u32 emittedTriangles;
    u32 textureId;
    PspGfxPspglTextureEnv textureEnv = PSP_GFX_PSPGL_TEX_REPLACE;

    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        ctx->stats.invalidTriangleCount++;
        return;
    }

    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1);
    }
    textureId = ctx->textureEnabled ? ctx->textureId : 0;

    va = &ctx->vertices[a];
    vb = &ctx->vertices[b];
    vc = &ctx->vertices[c];
    sharedClipCode = va->clipCode & vb->clipCode & vc->clipCode;
    combinedClipCode = va->clipCode | vb->clipCode | vc->clipCode;
    if (sharedClipCode != 0) {
        ctx->stats.sharedClipTriangleCount++;
    }
    if ((va->clipW < 0.0f) && (vb->clipW < 0.0f) && (vc->clipW < 0.0f)) {
        ctx->stats.behindEyeTriangleCount++;
    } else if ((va->clipW < 0.0f) || (vb->clipW < 0.0f) || (vc->clipW < 0.0f)) {
        ctx->stats.eyePlaneCrossingTriangleCount++;
    }
    area = ((vb->x - va->x) * (vc->y - va->y)) - ((vb->y - va->y) * (vc->x - va->x));
    if ((area > -0.000001f) && (area < 0.000001f)) {
        ctx->stats.degenerateTriangleCount++;
    }

    if ((ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA)) {
        textureEnv = PSP_GFX_PSPGL_TEX_MODULATE;
    }
    psp_gfx_dl_set_batch_texture(ctx, textureId, textureEnv, psp_gfx_dl_alpha_test_enabled(ctx),
                                 psp_gfx_dl_blend_enabled(ctx));
    psp_gfx_dl_set_batch_depth(ctx, (ctx->geometryMode & G_ZBUFFER) != 0, (ctx->otherModeL & Z_UPD) != 0);
    if (ctx->batchDepthTest) {
        ctx->stats.depthTestTriangleCount++;
    }
    if (ctx->batchDepthWrite) {
        ctx->stats.depthWriteTriangleCount++;
    }
    if (sharedClipCode != 0) {
        emittedTriangles = 0;
        ctx->stats.clipRejectedTriangleCount++;
    } else if (combinedClipCode != 0) {
        ctx->stats.clippedTriangleCount++;
        if ((combinedClipCode & (1U << 4)) != 0) {
            ctx->stats.nearPlaneClippedTriangleCount++;
        }
        emittedTriangles = psp_gfx_dl_emit_clipped_triangle(ctx, va, vb, vc);
        if (emittedTriangles == 0) {
            ctx->stats.clipRejectedTriangleCount++;
        } else {
            ctx->stats.clipGeneratedTriangleCount += emittedTriangles;
        }
    } else {
        PspGfxDlClipVertex vertex;

        psp_gfx_dl_build_clip_vertex(ctx, va, &vertex);
        psp_gfx_dl_emit_clip_vertex(ctx, &vertex);
        psp_gfx_dl_build_clip_vertex(ctx, vb, &vertex);
        psp_gfx_dl_emit_clip_vertex(ctx, &vertex);
        psp_gfx_dl_build_clip_vertex(ctx, vc, &vertex);
        psp_gfx_dl_emit_clip_vertex(ctx, &vertex);
        emittedTriangles = 1;
    }
    ctx->stats.triangleCount++;
    if ((textureId != 0) && (emittedTriangles != 0)) {
        ctx->stats.texturedTriangleCount++;
        if (ctx->batchAlphaTest) {
            ctx->stats.alphaTestTriangleCount++;
        }
        if (ctx->batchBlend) {
            ctx->stats.blendTriangleCount++;
        }
    }
}

static void psp_gfx_dl_emit_rect_vertex(PspGfxDlContext* ctx, float x, float y, float u, float v) {
    PspGfxPspglColorVertex* dst;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush(ctx);
    }
    dst = &ctx->batch[ctx->batchCount++];
    dst->x = (x / 160.0f) - 1.0f;
    dst->y = 1.0f - (y / 120.0f);
    dst->z = 0.0f;
    dst->r = (float) ctx->primitiveR / 255.0f;
    dst->g = (float) ctx->primitiveG / 255.0f;
    dst->b = (float) ctx->primitiveB / 255.0f;
    dst->a = (float) ctx->primitiveA / 255.0f;
    dst->u = u / (float) ctx->textureUploadWidth;
    dst->v = v / (float) ctx->textureUploadHeight;
}

static void psp_gfx_dl_handle_texture_rectangle(PspGfxDlContext* ctx, const Gfx* cmd, const Gfx* half1,
                                                const Gfx* half2, int flip) {
    float x0 = (float) ((cmd->words.w1 >> 12) & 0xFFF) * 0.25f;
    float y0 = (float) (cmd->words.w1 & 0xFFF) * 0.25f;
    float x1 = (float) ((cmd->words.w0 >> 12) & 0xFFF) * 0.25f;
    float y1 = (float) (cmd->words.w0 & 0xFFF) * 0.25f;
    float s0 = (float) (s16) (half1->words.w1 >> 16) / 32.0f;
    float t0 = (float) (s16) (half1->words.w1 & 0xFFFF) / 32.0f;
    float dsdx = (float) (s16) (half2->words.w1 >> 16) / 1024.0f;
    float dtdy = (float) (s16) (half2->words.w1 & 0xFFFF) / 1024.0f;
    float s1;
    float t1;

    if (ctx->textureId == 0) {
        psp_gfx_dl_prepare_texture(ctx, 1);
    }
    if ((psp_gfx_dl_opcode(half1) != PSP_GFX_OP_F3D_RDPHALF_1) ||
        (psp_gfx_dl_opcode(half2) != PSP_GFX_OP_F3D_RDPHALF_2) || (ctx->textureId == 0) ||
        (ctx->textureUploadWidth == 0) || (ctx->textureUploadHeight == 0)) {
        ctx->stats.textureRectangleRejected++;
        return;
    }

    psp_gfx_dl_set_batch_texture(ctx, ctx->textureId, PSP_GFX_PSPGL_TEX_MODULATE,
                                 psp_gfx_dl_alpha_test_enabled(ctx), psp_gfx_dl_blend_enabled(ctx));
    psp_gfx_dl_set_batch_depth(ctx, 0, 0);

    if (flip) {
        s1 = s0 + ((y1 - y0) * dsdx);
        t1 = t0 + ((x1 - x0) * dtdy);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y0, s0, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y1, s1, t0);
    } else {
        s1 = s0 + ((x1 - x0) * dsdx);
        t1 = t0 + ((y1 - y0) * dtdy);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y0, s1, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y1, s0, t1);
    }
    ctx->stats.textureRectangleCount++;
}

static void psp_gfx_dl_handle_set_primitive_color(PspGfxDlContext* ctx, const Gfx* gfx) {
    ctx->primitiveR = (u8) (gfx->words.w1 >> 24);
    ctx->primitiveG = (u8) (gfx->words.w1 >> 16);
    ctx->primitiveB = (u8) (gfx->words.w1 >> 8);
    ctx->primitiveA = (u8) gfx->words.w1;
}

static int psp_gfx_dl_combine_cycle0_matches(u32 mux0, u32 mux1, u32 a, u32 b, u32 c, u32 d, u32 aa, u32 ab,
                                             u32 ac, u32 ad) {
    u32 ca = (mux0 >> 20) & 0xF;
    u32 cc = (mux0 >> 15) & 0x1F;
    u32 caa = (mux0 >> 12) & 0x7;
    u32 cac = (mux0 >> 9) & 0x7;
    u32 cb = (mux1 >> 28) & 0xF;
    u32 cd = (mux1 >> 15) & 0x7;
    u32 cab = (mux1 >> 12) & 0x7;
    u32 cad = (mux1 >> 9) & 0x7;

    return (ca == (a & 0xF)) && (cb == (b & 0xF)) && (cc == (c & 0x1F)) && (cd == (d & 0x7)) &&
           (caa == (aa & 0x7)) && (cab == (ab & 0x7)) && (cac == (ac & 0x7)) && (cad == (ad & 0x7));
}

static void psp_gfx_dl_handle_set_combine(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 mux0 = gfx->words.w0 & 0x00FFFFFF;
    u32 mux1 = gfx->words.w1;

    if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_SHADE,
                                          G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_SHADE;
        ctx->combineUsesTextureAlpha = 0;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0,
                                                 G_CCMUX_PRIMITIVE, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0,
                                                 G_ACMUX_PRIMITIVE)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_PRIMITIVE;
        ctx->combineUsesTextureAlpha = 0;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0,
                                                 G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_DECAL_RGB;
        ctx->combineUsesTextureAlpha = 0;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0,
                                                 G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_DECAL_RGBA;
        ctx->combineUsesTextureAlpha = 1;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                                 G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA;
        ctx->combineUsesTextureAlpha = 0;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                                 G_CCMUX_0, G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_SHADE,
                                                 G_ACMUX_0)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA;
        ctx->combineUsesTextureAlpha = 1;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                                 G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA;
        ctx->combineUsesTextureAlpha = 1;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                 G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0,
                                                 G_ACMUX_PRIMITIVE)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA;
        ctx->combineUsesTextureAlpha = 0;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                 G_CCMUX_0, G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE,
                                                 G_ACMUX_0) ||
               psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                 G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA;
        ctx->combineUsesTextureAlpha = 1;
    } else {
        ctx->combineMode = PSP_GFX_DL_COMBINE_UNKNOWN;
        ctx->combineUsesTextureAlpha = 1;
    }
}

static void psp_gfx_dl_handle_vtx(PspGfxDlContext* ctx, const Gfx* gfx) {
    const Vtx* src = (const Vtx*) psp_gfx_dl_resolve_ptr(gfx->words.w1);
    u32 w0 = gfx->words.w0;
    u32 count;
    s32 v0;
    u32 i;

    if (src == NULL) {
        ctx->stats.vertexPointerRejected++;
        return;
    }

    count = (w0 >> 10) & 0x3F;
    v0 = (s32) ((w0 >> 17) & 0x7F);

    if ((count == 0) || (v0 < 0) || (((u32) v0 + count) > PSP_GFX_DL_MAX_VERTICES)) {
        psp_gfx_dl_count_unsupported(ctx, psp_gfx_dl_opcode(gfx));
        return;
    }

    for (i = 0; i < count; i++) {
        const Vtx* in = &src[i];
        PspGfxDlVertex* out = &ctx->vertices[v0 + i];

        out->valid = psp_gfx_dl_transform_vertex(ctx, in, out);
        if (!out->valid) {
            ctx->stats.invalidVertexCount++;
        } else {
            if (!ctx->hasVertexBounds) {
                ctx->minX = ctx->maxX = out->x;
                ctx->minY = ctx->maxY = out->y;
                ctx->minZ = ctx->maxZ = out->z;
                ctx->hasVertexBounds = 1;
            } else {
                if (out->x < ctx->minX) {
                    ctx->minX = out->x;
                }
                if (out->x > ctx->maxX) {
                    ctx->maxX = out->x;
                }
                if (out->y < ctx->minY) {
                    ctx->minY = out->y;
                }
                if (out->y > ctx->maxY) {
                    ctx->maxY = out->y;
                }
                if (out->z < ctx->minZ) {
                    ctx->minZ = out->z;
                }
                if (out->z > ctx->maxZ) {
                    ctx->maxZ = out->z;
                }
            }
            if ((out->x < -1.0f) || (out->x > 1.0f) || (out->y < -1.0f) || (out->y > 1.0f) ||
                (out->z < -1.0f) || (out->z > 1.0f)) {
                ctx->stats.outsideVertexCount++;
            }
        }
        if ((ctx->geometryMode & G_LIGHTING) != 0) {
            float nx = (float) (s8) in->v.cn[0];
            float ny = (float) (s8) in->v.cn[1];
            float nz = (float) (s8) in->v.cn[2];
            float length;
            float r = ctx->ambientR;
            float g = ctx->ambientG;
            float b = ctx->ambientB;
            u32 lightIndex;

            if (ctx->hasModelview) {
                float transformedX = (ctx->modelview[0][0] * nx) + (ctx->modelview[1][0] * ny) +
                                     (ctx->modelview[2][0] * nz);
                float transformedY = (ctx->modelview[0][1] * nx) + (ctx->modelview[1][1] * ny) +
                                     (ctx->modelview[2][1] * nz);
                float transformedZ = (ctx->modelview[0][2] * nx) + (ctx->modelview[1][2] * ny) +
                                     (ctx->modelview[2][2] * nz);

                nx = transformedX;
                ny = transformedY;
                nz = transformedZ;
            }
            length = sqrtf((nx * nx) + (ny * ny) + (nz * nz));
            if (length > 0.001f) {
                nx /= length;
                ny /= length;
                nz /= length;
            }
            for (lightIndex = 0; lightIndex < ctx->lightCount; lightIndex++) {
                const PspGfxDlLight* light = &ctx->lights[lightIndex];
                float lx = (float) light->x;
                float ly = (float) light->y;
                float lz = (float) light->z;
                float lightLength = sqrtf((lx * lx) + (ly * ly) + (lz * lz));
                float dot;

                if (lightLength > 0.001f) {
                    lx /= lightLength;
                    ly /= lightLength;
                    lz /= lightLength;
                }
                dot = (nx * lx) + (ny * ly) + (nz * lz);
                if (dot > 0.0f) {
                    r += (float) light->r * dot;
                    g += (float) light->g * dot;
                    b += (float) light->b * dot;
                }
            }
            out->r = (u8) ((r > 255.0f) ? 255.0f : r);
            out->g = (u8) ((g > 255.0f) ? 255.0f : g);
            out->b = (u8) ((b > 255.0f) ? 255.0f : b);
        } else {
            out->r = in->v.cn[0];
            out->g = in->v.cn[1];
            out->b = in->v.cn[2];
        }
        out->a = in->v.cn[3];
        out->s = in->v.tc[0];
        out->t = in->v.tc[1];
    }

    ctx->stats.vertexCount += count;
}

static void psp_gfx_dl_handle_move_word(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 offset = (gfx->words.w0 >> 8) & 0xFFFF;
    u32 index = gfx->words.w0 & 0xFF;
    u32 encodedCount;

    if ((index != G_MW_NUMLIGHT) || (offset != G_MWO_NUMLIGHT)) {
        return;
    }

    encodedCount = (gfx->words.w1 & 0x7FFFFFFF) / 32U;
    ctx->lightCount = (encodedCount > 0) ? (encodedCount - 1) : 0;
    if (ctx->lightCount > 7) {
        ctx->lightCount = 7;
    }
}

static void psp_gfx_dl_handle_other_mode_l(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 shift = (gfx->words.w0 >> 8) & 0xFF;
    u32 length = gfx->words.w0 & 0xFF;
    u32 mask;

    if ((length == 0) || (shift >= 32) || (length > (32 - shift))) {
        psp_gfx_dl_count_unsupported(ctx, PSP_GFX_OP_F3D_SETOTHERMODE_L);
        return;
    }
    mask = (length == 32) ? 0xFFFFFFFFU : (((1U << length) - 1U) << shift);
    if ((ctx->batchCount != 0) &&
        (((ctx->otherModeL ^ gfx->words.w1) & mask & (3U | CVG_X_ALPHA | FORCE_BL | Z_UPD)) != 0)) {
        psp_gfx_dl_flush(ctx);
    }
    ctx->otherModeL = (ctx->otherModeL & ~mask) | (gfx->words.w1 & mask);
}

static void psp_gfx_dl_handle_geometry_mode(PspGfxDlContext* ctx, const Gfx* gfx, int set) {
    if (set) {
        ctx->geometryMode |= gfx->words.w1;
    } else {
        ctx->geometryMode &= ~gfx->words.w1;
    }
}

static void psp_gfx_dl_handle_texture(PspGfxDlContext* ctx, const Gfx* gfx) {
    int enabled = (gfx->words.w0 & 0xFF) != G_OFF;

    if ((ctx->batchCount != 0) && (ctx->textureEnabled != enabled)) {
        psp_gfx_dl_flush(ctx);
    }
    ctx->textureEnabled = enabled;
}

static void psp_gfx_dl_handle_set_texture_image(PspGfxDlContext* ctx, const Gfx* gfx) {
    psp_gfx_dl_flush(ctx);
    ctx->textureFormat = (gfx->words.w0 >> 21) & 0x7;
    ctx->textureSize = (gfx->words.w0 >> 19) & 0x3;
    ctx->textureImage = psp_gfx_dl_resolve_ptr(gfx->words.w1);
    ctx->textureId = 0;
    ctx->textureUploadWidth = 0;
    ctx->textureUploadHeight = 0;
    ctx->textureUploadAttempted = 0;
}

static void psp_gfx_dl_handle_load_tlut(PspGfxDlContext* ctx) {
    if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b) &&
        (ctx->textureImage != NULL)) {
        ctx->texturePalette = (const u16*) ctx->textureImage;
        ctx->textureUploadAttempted = 0;
    }
}

static void psp_gfx_dl_handle_set_tile(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 tile = (gfx->words.w1 >> 24) & 0x7;

    if (tile != G_TX_RENDERTILE) {
        return;
    }

    ctx->textureFormat = (gfx->words.w0 >> 21) & 0x7;
    ctx->textureSize = (gfx->words.w0 >> 19) & 0x3;
    ctx->texturePaletteIndex = (gfx->words.w1 >> 20) & 0xF;
    ctx->textureUploadAttempted = 0;
}

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred) {
    if ((ctx->textureId != 0) || ctx->textureUploadAttempted || (ctx->textureImage == NULL) ||
        (ctx->textureWidth == 0) || (ctx->textureHeight == 0)) {
        return ctx->textureId != 0;
    }
    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->texturePalette == NULL)) {
        return 0;
    }

    ctx->textureUploadAttempted = 1;
    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_8b)) {
        ctx->textureId =
            PspGfxPspgl_GetCi8Texture((const u8*) ctx->textureImage, ctx->texturePalette, ctx->textureWidth,
                                     ctx->textureHeight, &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b)) {
        ctx->textureId =
            PspGfxPspgl_GetCi4Texture((const u8*) ctx->textureImage,
                                     ctx->texturePalette + (ctx->texturePaletteIndex * 16), ctx->textureWidth,
                                     ctx->textureHeight, &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        ctx->textureId =
            PspGfxPspgl_GetRgba16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                        &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b)) {
        ctx->textureId =
            PspGfxPspgl_GetIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                     &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        ctx->textureId =
            PspGfxPspgl_GetIa16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                      &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else {
        return 0;
    }

    if (ctx->textureId != 0) {
        ctx->stats.textureCount++;
        if (deferred) {
            ctx->stats.deferredTextureCount++;
        }
        if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b)) {
            ctx->stats.ci4TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b)) {
            ctx->stats.rgba16TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b)) {
            ctx->stats.ia8TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b)) {
            ctx->stats.ia16TextureCount++;
        }
        return 1;
    }
    ctx->stats.textureRejected++;
    return 0;
}

static void psp_gfx_dl_handle_set_tile_size(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 tile = (gfx->words.w1 >> 24) & 0x7;
    u32 lrs;
    u32 lrt;

    if (tile != G_TX_RENDERTILE) {
        return;
    }

    lrs = (gfx->words.w1 >> 12) & 0xFFF;
    lrt = gfx->words.w1 & 0xFFF;
    ctx->textureWidth = (lrs >> G_TEXTURE_IMAGE_FRAC) + 1;
    ctx->textureHeight = (lrt >> G_TEXTURE_IMAGE_FRAC) + 1;
    ctx->textureUploadAttempted = 0;
    psp_gfx_dl_prepare_texture(ctx, 0);
}

static int psp_gfx_dl_run_internal(PspGfxDlContext* ctx, const Gfx* dl, u32 depth) {
    const Gfx* pc = dl;

    if (dl == NULL) {
        return 0;
    }
    if (depth >= PSP_GFX_DL_MAX_DEPTH) {
        ctx->stats.depthLimitHit++;
        return 0;
    }
    if (depth > ctx->stats.maxDepthReached) {
        ctx->stats.maxDepthReached = depth;
    }

    while (ctx->stats.commandCount < PSP_GFX_DL_MAX_COMMANDS) {
        const Gfx* cmd = pc++;
        u8 opcode = psp_gfx_dl_opcode(cmd);

        ctx->stats.commandCount++;

        if (psp_gfx_dl_is_end(opcode)) {
            return 1;
        }

        if (opcode == PSP_GFX_OP_F3D_DL) {
            const Gfx* child = (const Gfx*) psp_gfx_dl_resolve_ptr(cmd->words.w1);
            int noPush = ((cmd->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH;

            if (child == NULL) {
                ctx->stats.nestedDlRejected++;
                ctx->stats.displayListPointerRejected++;
                continue;
            }

            ctx->stats.nestedDlFollowed++;
            psp_gfx_dl_run_internal(ctx, child, depth + 1);
            if (noPush) {
                return 1;
            }
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_MTX) {
            psp_gfx_dl_handle_mtx(ctx, cmd);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_POPMTX) {
            psp_gfx_dl_handle_pop_mtx(ctx);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_MOVEMEM) {
            psp_gfx_dl_handle_movemem(ctx, cmd);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_MOVEWORD) {
            psp_gfx_dl_handle_move_word(ctx, cmd);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_SETOTHERMODE_L) {
            psp_gfx_dl_handle_other_mode_l(ctx, cmd);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_SETGEOMETRYMODE) {
            psp_gfx_dl_handle_geometry_mode(ctx, cmd, 1);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_CLEARGEOMETRYMODE) {
            psp_gfx_dl_handle_geometry_mode(ctx, cmd, 0);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_TEXTURE) {
            psp_gfx_dl_handle_texture(ctx, cmd);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_VTX) {
            psp_gfx_dl_handle_vtx(ctx, cmd);
            continue;
        }

        if (opcode == G_SETTIMG) {
            psp_gfx_dl_handle_set_texture_image(ctx, cmd);
            continue;
        }

        if (opcode == G_SETPRIMCOLOR) {
            psp_gfx_dl_handle_set_primitive_color(ctx, cmd);
            continue;
        }

        if (opcode == G_SETCOMBINE) {
            psp_gfx_dl_handle_set_combine(ctx, cmd);
            continue;
        }

        if (opcode == G_LOADTLUT) {
            psp_gfx_dl_handle_load_tlut(ctx);
            continue;
        }

        if (opcode == G_SETTILE) {
            psp_gfx_dl_handle_set_tile(ctx, cmd);
            continue;
        }

        if (opcode == G_SETTILESIZE) {
            psp_gfx_dl_handle_set_tile_size(ctx, cmd);
            continue;
        }

        if ((opcode == G_TEXRECT) || (opcode == G_TEXRECTFLIP)) {
            const Gfx* half1;
            const Gfx* half2;

            if ((ctx->stats.commandCount + 2) > PSP_GFX_DL_MAX_COMMANDS) {
                ctx->stats.commandLimitHit++;
                return 0;
            }
            half1 = pc++;
            half2 = pc++;
            ctx->stats.commandCount += 2;
            psp_gfx_dl_handle_texture_rectangle(ctx, cmd, half1, half2, opcode == G_TEXRECTFLIP);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_TRI1) {
            u32 w1 = cmd->words.w1;
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF),
                                psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF),
                                psp_gfx_dl_decode_tri_index(w1 & 0xFF));
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_TRI2) {
            u32 w0 = cmd->words.w0;
            u32 w1 = cmd->words.w1;

            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w0 >> 16) & 0xFF),
                                psp_gfx_dl_decode_tri_index((w0 >> 8) & 0xFF),
                                psp_gfx_dl_decode_tri_index(w0 & 0xFF));
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF),
                                psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF),
                                psp_gfx_dl_decode_tri_index(w1 & 0xFF));
            continue;
        }

        if (!psp_gfx_dl_is_noop_state(opcode)) {
            psp_gfx_dl_count_unsupported(ctx, opcode);
        }
    }

    ctx->stats.commandLimitHit++;
    return 0;
}

static void psp_gfx_dl_reset_context(PspGfxDlContext* ctx) {
    u8* bytes = (u8*) ctx;
    u32 i;

    for (i = 0; i < sizeof(*ctx); i++) {
        bytes[i] = 0;
    }
    psp_gfx_dl_identity(ctx->modelview);
    psp_gfx_dl_identity(ctx->projection);
    ctx->primitiveR = 255;
    ctx->primitiveG = 255;
    ctx->primitiveB = 255;
    ctx->primitiveA = 255;
    ctx->combineUsesTextureAlpha = 1;
}

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats) {
    PspGfxDlContext* ctx = &sPspGfxDlContext;
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
    char line[512];
#endif

    psp_gfx_dl_reset_context(ctx);
    psp_gfx_dl_run_internal(ctx, dl, 0);
    psp_gfx_dl_flush(ctx);

    if (outStats != NULL) {
        *outStats = ctx->stats;
    }

#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
    if ((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
        (ctx->stats.depthLimitHit != 0)) {
        snprintf(line, sizeof(line),
                 "[pspgl-dl] task=%lu cmds=%lu vtx=%lu tri=%lu drawv=%lu dl=%lu reject=%lu mtx=%lu unsup=%lu "
                 "push=%lu pop=%lu mtxReject=%lu vp=%lu invalid=%lu outside=%lu tex=%lu texReject=%lu "
                 "rgba16=%lu ci4=%lu ia8=%lu ia16=%lu texTri=%lu alphaTestTri=%lu blendTri=%lu "
                 "texRect=%lu rectReject=%lu "
                 "firstUnsup=0x%02lx "
                 "cmdLimit=%lu depthLimit=%lu",
                 (unsigned long) taskIndex, (unsigned long) ctx->stats.commandCount,
                 (unsigned long) ctx->stats.vertexCount, (unsigned long) ctx->stats.triangleCount,
                 (unsigned long) ctx->stats.drawVertexCount, (unsigned long) ctx->stats.nestedDlFollowed,
                 (unsigned long) ctx->stats.nestedDlRejected, (unsigned long) ctx->stats.mtxCount,
                 (unsigned long) ctx->stats.unsupportedCount, (unsigned long) ctx->stats.mtxPushCount,
                 (unsigned long) ctx->stats.mtxPopCount, (unsigned long) ctx->stats.mtxStackRejected,
                 (unsigned long) ctx->stats.viewportCount, (unsigned long) ctx->stats.invalidVertexCount,
                 (unsigned long) ctx->stats.outsideVertexCount, (unsigned long) ctx->stats.textureCount,
                 (unsigned long) ctx->stats.textureRejected, (unsigned long) ctx->stats.rgba16TextureCount,
                 (unsigned long) ctx->stats.ci4TextureCount, (unsigned long) ctx->stats.ia8TextureCount,
                 (unsigned long) ctx->stats.ia16TextureCount, (unsigned long) ctx->stats.texturedTriangleCount,
                 (unsigned long) ctx->stats.alphaTestTriangleCount,
                 (unsigned long) ctx->stats.blendTriangleCount,
                 (unsigned long) ctx->stats.textureRectangleCount,
                 (unsigned long) ctx->stats.textureRectangleRejected,
                 (unsigned long) ctx->stats.firstUnsupportedOpcode,
                 (unsigned long) ctx->stats.commandLimitHit, (unsigned long) ctx->stats.depthLimitHit);
        PspPlatform_LogLine(line);
    }

    if (((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
         (ctx->stats.depthLimitHit != 0)) &&
        ((ctx->stats.vertexCount != 0) || (ctx->stats.invalidTriangleCount != 0) ||
         (ctx->stats.displayListPointerRejected != 0))) {
        snprintf(line, sizeof(line),
                 "[pspgl-geom] task=%lu nearW=%lu behindVtx=%lu invalidTri=%lu sharedClipTri=%lu "
                 "eyeCrossTri=%lu behindTri=%lu clippedTri=%lu nearClipTri=%lu clipRejectTri=%lu "
                 "clipGenTri=%lu degenerateTri=%lu depthTestTri=%lu depthWriteTri=%lu deferTex=%lu "
                 "ptrReject=%lu/%lu/%lu maxDlDepth=%lu",
                 (unsigned long) taskIndex, (unsigned long) ctx->stats.nearZeroWCount,
                 (unsigned long) ctx->stats.behindEyeVertexCount, (unsigned long) ctx->stats.invalidTriangleCount,
                 (unsigned long) ctx->stats.sharedClipTriangleCount,
                 (unsigned long) ctx->stats.eyePlaneCrossingTriangleCount,
                 (unsigned long) ctx->stats.behindEyeTriangleCount,
                 (unsigned long) ctx->stats.clippedTriangleCount,
                 (unsigned long) ctx->stats.nearPlaneClippedTriangleCount,
                 (unsigned long) ctx->stats.clipRejectedTriangleCount,
                 (unsigned long) ctx->stats.clipGeneratedTriangleCount,
                 (unsigned long) ctx->stats.degenerateTriangleCount,
                 (unsigned long) ctx->stats.depthTestTriangleCount,
                 (unsigned long) ctx->stats.depthWriteTriangleCount,
                 (unsigned long) ctx->stats.deferredTextureCount,
                 (unsigned long) ctx->stats.matrixPointerRejected,
                 (unsigned long) ctx->stats.vertexPointerRejected,
                 (unsigned long) ctx->stats.displayListPointerRejected,
                 (unsigned long) ctx->stats.maxDepthReached);
        PspPlatform_LogLine(line);
    }

    if (!sLoggedFirstDrawableTask && (ctx->stats.triangleCount != 0)) {
        snprintf(line, sizeof(line),
                 "[pspgl-dl-bounds] task=%lu x=%.3f..%.3f y=%.3f..%.3f z=%.3f..%.3f mtxFlags=0x%02lx "
                 "vpScale=%d,%d vpTrans=%d,%d",
                 (unsigned long) taskIndex, ctx->minX, ctx->maxX, ctx->minY, ctx->maxY, ctx->minZ, ctx->maxZ,
                 (unsigned long) ctx->matrixFlagsSeen, ctx->viewportScaleX, ctx->viewportScaleY,
                 ctx->viewportTransX, ctx->viewportTransY);
        PspPlatform_LogLine(line);
        sLoggedFirstDrawableTask = 1;
    }
#endif

    return ctx->stats.commandCount > 0;
}
