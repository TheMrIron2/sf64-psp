#include "src/psp/gfx/gfx_psp_dl.h"

#include "macros.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define PSP_GFX_DL_MAX_DEPTH 8
#define PSP_GFX_DL_MAX_COMMANDS 8192
#define PSP_GFX_DL_MAX_VERTICES 64
#define PSP_GFX_DL_BATCH_VERTICES 3072
#define PSP_GFX_DL_MTX_STACK_DEPTH 32

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
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    s16 s;
    s16 t;
    int valid;
} PspGfxDlVertex;

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
    int hasModelview;
    int hasProjection;
    int hasVertexBounds;
} PspGfxDlContext;

static PspGfxDlContext sPspGfxDlContext;
static int sLoggedFirstDrawableTask;

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

static int psp_gfx_dl_transform_vertex(PspGfxDlContext* ctx, const Vtx* in, float* outX, float* outY, float* outZ) {
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
        *outX = x / 320.0f;
        *outY = -y / 240.0f;
        *outZ = z / 4096.0f;
        return 1;
    }

    psp_gfx_dl_transform(ctx->projection, x, y, z, &clipX, &clipY, &clipZ, &clipW);
    if ((clipW > -0.001f) && (clipW < 0.001f)) {
        return 0;
    }

    *outX = clipX / clipW;
    *outY = clipY / clipW;
    *outZ = clipZ / clipW;
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
        psp_gfx_dl_count_unsupported(ctx, PSP_GFX_OP_F3D_MTX);
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
        psp_gfx_dl_mtx_mul(target, target, loaded);
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

    if (index != G_MV_VIEWPORT) {
        return;
    }

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
}

static void psp_gfx_dl_flush(PspGfxDlContext* ctx) {
    if (ctx->batchCount == 0) {
        return;
    }
    PspGfxPspgl_DrawColoredTriangles(ctx->batch, ctx->batchCount, ctx->textureId);
    ctx->stats.drawVertexCount += ctx->batchCount;
    ctx->batchCount = 0;
}

static int psp_gfx_dl_vertex_is_valid(PspGfxDlContext* ctx, u8 index) {
    return (index < PSP_GFX_DL_MAX_VERTICES) && ctx->vertices[index].valid;
}

static void psp_gfx_dl_emit_vertex(PspGfxDlContext* ctx, u8 index) {
    const PspGfxDlVertex* src;
    PspGfxPspglColorVertex* dst;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush(ctx);
    }

    src = &ctx->vertices[index];
    dst = &ctx->batch[ctx->batchCount++];

    dst->x = src->x;
    dst->y = src->y;
    dst->z = src->z;
    dst->r = (float) src->r / 255.0f;
    dst->g = (float) src->g / 255.0f;
    dst->b = (float) src->b / 255.0f;
    dst->a = (float) src->a / 255.0f;
    if ((ctx->textureUploadWidth != 0) && (ctx->textureUploadHeight != 0)) {
        dst->u = ((float) src->s / 32.0f) / (float) ctx->textureUploadWidth;
        dst->v = ((float) src->t / 32.0f) / (float) ctx->textureUploadHeight;
    } else {
        dst->u = 0.0f;
        dst->v = 0.0f;
    }
}

static void psp_gfx_dl_emit_tri(PspGfxDlContext* ctx, u8 a, u8 b, u8 c) {
    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        psp_gfx_dl_count_unsupported(ctx, 0x100);
        return;
    }

    psp_gfx_dl_emit_vertex(ctx, a);
    psp_gfx_dl_emit_vertex(ctx, b);
    psp_gfx_dl_emit_vertex(ctx, c);
    ctx->stats.triangleCount++;
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
    dst->r = 1.0f;
    dst->g = 1.0f;
    dst->b = 1.0f;
    dst->a = 1.0f;
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

    if ((psp_gfx_dl_opcode(half1) != PSP_GFX_OP_F3D_RDPHALF_1) ||
        (psp_gfx_dl_opcode(half2) != PSP_GFX_OP_F3D_RDPHALF_2) || (ctx->textureId == 0) ||
        (ctx->textureUploadWidth == 0) || (ctx->textureUploadHeight == 0)) {
        ctx->stats.textureRectangleRejected++;
        return;
    }

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

static void psp_gfx_dl_handle_vtx(PspGfxDlContext* ctx, const Gfx* gfx) {
    const Vtx* src = (const Vtx*) psp_gfx_dl_resolve_ptr(gfx->words.w1);
    u32 w0 = gfx->words.w0;
    u32 count;
    s32 v0;
    u32 i;

    if (src == NULL) {
        psp_gfx_dl_count_unsupported(ctx, psp_gfx_dl_opcode(gfx));
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

        out->valid = psp_gfx_dl_transform_vertex(ctx, in, &out->x, &out->y, &out->z);
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
        out->r = in->v.cn[0];
        out->g = in->v.cn[1];
        out->b = in->v.cn[2];
        out->a = in->v.cn[3];
        out->s = in->v.tc[0];
        out->t = in->v.tc[1];
    }

    ctx->stats.vertexCount += count;
}

static void psp_gfx_dl_handle_set_texture_image(PspGfxDlContext* ctx, const Gfx* gfx) {
    psp_gfx_dl_flush(ctx);
    ctx->textureFormat = (gfx->words.w0 >> 21) & 0x7;
    ctx->textureSize = (gfx->words.w0 >> 19) & 0x3;
    ctx->textureImage = psp_gfx_dl_resolve_ptr(gfx->words.w1);
    ctx->textureId = 0;
    ctx->textureUploadWidth = 0;
    ctx->textureUploadHeight = 0;
}

static void psp_gfx_dl_handle_load_tlut(PspGfxDlContext* ctx) {
    if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b) &&
        (ctx->textureImage != NULL)) {
        ctx->texturePalette = (const u16*) ctx->textureImage;
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

    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_8b) &&
        (ctx->textureImage != NULL) && (ctx->texturePalette != NULL)) {
        ctx->textureId =
            PspGfxPspgl_GetCi8Texture((const u8*) ctx->textureImage, ctx->texturePalette, ctx->textureWidth,
                                     ctx->textureHeight, &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (ctx->textureId != 0) {
            ctx->stats.textureCount++;
        } else {
            ctx->stats.textureRejected++;
        }
    } else if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b) &&
               (ctx->textureImage != NULL) && (ctx->texturePalette != NULL)) {
        ctx->textureId =
            PspGfxPspgl_GetCi4Texture((const u8*) ctx->textureImage,
                                     ctx->texturePalette + (ctx->texturePaletteIndex * 16), ctx->textureWidth,
                                     ctx->textureHeight, &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (ctx->textureId != 0) {
            ctx->stats.textureCount++;
            ctx->stats.ci4TextureCount++;
        } else {
            ctx->stats.textureRejected++;
        }
    } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b) &&
               (ctx->textureImage != NULL)) {
        ctx->textureId =
            PspGfxPspgl_GetRgba16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                        &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (ctx->textureId != 0) {
            ctx->stats.textureCount++;
        } else {
            ctx->stats.textureRejected++;
        }
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b) &&
               (ctx->textureImage != NULL)) {
        ctx->textureId =
            PspGfxPspgl_GetIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                     &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (ctx->textureId != 0) {
            ctx->stats.textureCount++;
            ctx->stats.ia8TextureCount++;
        } else {
            ctx->stats.textureRejected++;
        }
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b) &&
               (ctx->textureImage != NULL)) {
        ctx->textureId =
            PspGfxPspgl_GetIa16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                      &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (ctx->textureId != 0) {
            ctx->stats.textureCount++;
            ctx->stats.ia16TextureCount++;
        } else {
            ctx->stats.textureRejected++;
        }
    }
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

        if (opcode == PSP_GFX_OP_F3D_VTX) {
            psp_gfx_dl_handle_vtx(ctx, cmd);
            continue;
        }

        if (opcode == G_SETTIMG) {
            psp_gfx_dl_handle_set_texture_image(ctx, cmd);
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
}

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats) {
    PspGfxDlContext* ctx = &sPspGfxDlContext;
    char line[384];

    psp_gfx_dl_reset_context(ctx);
    psp_gfx_dl_run_internal(ctx, dl, 0);
    psp_gfx_dl_flush(ctx);

    if (outStats != NULL) {
        *outStats = ctx->stats;
    }

    if ((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
        (ctx->stats.depthLimitHit != 0)) {
        snprintf(line, sizeof(line),
                 "[pspgl-dl] task=%lu cmds=%lu vtx=%lu tri=%lu drawv=%lu dl=%lu reject=%lu mtx=%lu unsup=%lu "
                 "push=%lu pop=%lu mtxReject=%lu vp=%lu invalid=%lu outside=%lu tex=%lu texReject=%lu "
                 "ci4=%lu ia8=%lu ia16=%lu texRect=%lu rectReject=%lu "
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
                 (unsigned long) ctx->stats.textureRejected, (unsigned long) ctx->stats.ci4TextureCount,
                 (unsigned long) ctx->stats.ia8TextureCount, (unsigned long) ctx->stats.ia16TextureCount,
                 (unsigned long) ctx->stats.textureRectangleCount,
                 (unsigned long) ctx->stats.textureRectangleRejected,
                 (unsigned long) ctx->stats.firstUnsupportedOpcode,
                 (unsigned long) ctx->stats.commandLimitHit, (unsigned long) ctx->stats.depthLimitHit);
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

    return ctx->stats.commandCount > 0;
}
