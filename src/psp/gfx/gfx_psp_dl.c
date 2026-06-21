#include "src/psp/gfx/gfx_psp_dl.h"

#include "macros.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"

#include <stdint.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#ifndef USE_N64PSP_MATH
#define USE_N64PSP_MATH 0
#endif

#ifndef PSP_VALIDATE_N64PSP_MATH
#define PSP_VALIDATE_N64PSP_MATH 0
#endif

#if (USE_N64PSP_MATH + 0)
#include <n64psp/math.h>
#endif

#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#define PSP_GFX_DL_MAX_DEPTH 8
#define PSP_GFX_DL_MAX_COMMANDS 8192
#define PSP_GFX_DL_MAX_NESTED_COMMANDS 2048
#define PSP_GFX_DL_MAX_VERTICES 64
#define PSP_GFX_DL_BATCH_VERTICES 3072
#define PSP_GFX_DL_MTX_STACK_DEPTH 32
#define PSP_GFX_DL_CLIP_PLANES 6
#define PSP_GFX_DL_MAX_CLIP_VERTICES 12
#define PSP_GFX_DL_PERSPECTIVE_W_RATIO 1.5f
#define PSP_GFX_DL_PERSPECTIVE_MAX_DEPTH 5

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
    float viewX;
    float viewY;
    float viewZ;
    float viewW;
    float clipX;
    float clipY;
    float clipZ;
    float clipW;
    float projection[4][4];
    u32 projectionSerial;
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
    float viewX;
    float viewY;
    float viewZ;
    float viewW;
    float r;
    float g;
    float b;
    float a;
    float u;
    float v;
    int generated;
} PspGfxDlClipVertex;

typedef struct {
    u8 r;
    u8 g;
    u8 b;

    float x;
    float y;
    float z;
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
    u32 taskIndex;
    u32 segments[16];
    PspGfxDlVertex vertices[PSP_GFX_DL_MAX_VERTICES];
    float modelview[4][4];
    float projection[4][4];
    float modelviewStack[PSP_GFX_DL_MTX_STACK_DEPTH][4][4];
    float batchProjection[4][4];
    u32 batchCount;
    u32 modelviewStackDepth;
    u32 projectionSerial;
    u32 batchProjectionSerial;
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
    u32 textureCms;
    u32 textureCmt;
    u32 textureMaskS;
    u32 textureMaskT;
    u32 textureId;
    int textureUploadAttempted;
    u32 batchTextureId;
    u32 geometryMode;
    u32 lightCount;
    PspGfxDlLight lights[7];
    u8 ambientR;
    u8 ambientG;
    u8 ambientB;
    u32 lightingVertexCount;
    float lightingRawMin;
    float lightingRawMax;
    u8 lightingMappedMin;
    u8 lightingMappedMax;
    int hasLightingRange;
    u8 primitiveR;
    u8 primitiveG;
    u8 primitiveB;
    u8 primitiveA;
    PspGfxDlCombineMode combineMode;
    PspGfxPspglTextureEnv batchTextureEnv;
    PspGfxPspglTextureWrap batchWrapS;
    PspGfxPspglTextureWrap batchWrapT;
    int combineUsesTextureAlpha;
    int textureEnabled;
    int batchAlphaTest;
    int batchBlend;
    int batchPremultiplied;
    int batchDepthTest;
    int batchDepthWrite;
    int batchFog;
    float batchFogColor[4];
    float batchFogStart;
    float batchFogEnd;
    int hasFogDepthRange;
    float fogRangeStart;
    float fogRangeEnd;
    float fogDepthMin;
    float fogDepthMax;
    int batchPretransformed;
    int batchTransformSet;
    u32 otherModeL;
    u8 fogR;
    u8 fogG;
    u8 fogB;
    u8 fogA;
    s16 fogMul;
    s16 fogOffset;
    int hasModelview;
    int hasProjection;
    int hasVertexBounds;
    int hasClipSample;
    u32 clipSampleVertexCount;
    u32 clipSampleGeneratedCount;
    float clipLargestWRatio;
    float clipSampleMinW;
    float clipSampleMaxW;
    float clipSampleMinX;
    float clipSampleMaxX;
    float clipSampleMinY;
    float clipSampleMaxY;
    float clipSampleMinZ;
    float clipSampleMaxZ;
    float clipSampleMinU;
    float clipSampleMaxU;
    float clipSampleMinV;
    float clipSampleMaxV;
} PspGfxDlContext;

static PspGfxDlContext sPspGfxDlContext;

static PspGfxPspglColorVertex
    sPspGfxDlBatch[PSP_GFX_DL_BATCH_VERTICES]
    __attribute__((aligned(16)));

static u8 sLightingRemap[256];
static int sLightingRemapInitialized;

#if (USE_N64PSP_BATCH_TRANSFORM + 0)
static n64psp_vec4f
    sPspGfxDlTransformInput[PSP_GFX_DL_MAX_VERTICES];

static n64psp_vec4f_pair
    sPspGfxDlTransformOutput[PSP_GFX_DL_MAX_VERTICES];
#endif

#if (USE_N64PSP_BATCH_TRANSFORM + 0) && \
    (PSP_VALIDATE_N64PSP_BATCH_TRANSFORM + 0)
static int sLoggedN64PspBatchTransformMismatch;
#endif

#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
static int sLoggedFirstDrawableTask;
static int sLoggedFirstLightingTask;
static int sLoggedTexturedClipSample;
static u32 sLoggedRejectedDlTargets;
#endif

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred, int premultiply);

static void psp_gfx_dl_init_lighting_remap(void) {
    u32 i;

    if (sLightingRemapInitialized) {
        return;
    }
    /* Match the N64 brightness response used by the Dreamcast renderer. */
    for (i = 0; i < 256; i++) {
        sLightingRemap[i] = (u8) (255.0f * sqrtf((float) i / 255.0f));
    }
    sLightingRemapInitialized = 1;
}

static u8 psp_gfx_dl_remap_lighting(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 255.0f) {
        return 255;
    }

    return sLightingRemap[(u8) value];
}

static u8 psp_gfx_dl_float_to_u8(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 1.0f) {
        return 255;
    }

    return (u8) ((value * 255.0f) + 0.5f);
}

static u32 psp_gfx_dl_pack_rgba(float r, float g, float b, float a) {
    u32 red = psp_gfx_dl_float_to_u8(r);
    u32 green = psp_gfx_dl_float_to_u8(g);
    u32 blue = psp_gfx_dl_float_to_u8(b);
    u32 alpha = psp_gfx_dl_float_to_u8(a);

    return red |
           (green << 8) |
           (blue << 16) |
           (alpha << 24);
}

static float psp_gfx_dl_fog_distance(const float projection[4][4], float ndcZ) {
    float denominator = projection[2][2] - (ndcZ * projection[2][3]);

    if ((denominator > -0.000001f) && (denominator < 0.000001f)) {
        return 0.0f;
    }
    return (projection[3][2] - (ndcZ * projection[3][3])) / denominator;
}

static PspGfxPspglTextureWrap psp_gfx_dl_texture_wrap(u32 mode, u32 mask) {
    if ((mode & G_TX_CLAMP) != 0) {
        return PSP_GFX_PSPGL_WRAP_CLAMP;
    }
    if (mask == G_TX_NOMASK) {
        return PSP_GFX_PSPGL_WRAP_CLAMP;
    }
    /*
     * PSPGL submits mirror and repeat with the same effective GL_REPEAT
     * backend state, so canonicalise non-clamped tiles to REPEAT.
     */
    return PSP_GFX_PSPGL_WRAP_REPEAT;
}

static int psp_gfx_dl_alpha_test_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha &&
           (((ctx->otherModeL & 3U) != G_AC_NONE) || ((ctx->otherModeL & CVG_X_ALPHA) != 0));
}

static int psp_gfx_dl_blend_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha && ((ctx->otherModeL & FORCE_BL) != 0);
}

static int psp_gfx_dl_premultiplied_blend_enabled(PspGfxDlContext* ctx) {
    return psp_gfx_dl_blend_enabled(ctx) && ((ctx->otherModeL & CVG_DST_SAVE) == CVG_DST_SAVE) &&
           (ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b);
}

static u8 psp_gfx_dl_opcode(const Gfx* gfx) {
    return (u8) (gfx->words.w0 >> 24);
}

static int psp_gfx_dl_is_native_ptr(uintptr_t ptr) {
    return PSP_IS_NATIVE_PTR(ptr);
}

static const void* psp_gfx_dl_resolve_ptr(const PspGfxDlContext* ctx, u32 raw) {
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
    base = ctx->segments[segment];
    if (base == 0) {
        return NULL;
    }
    return (const void*) (uintptr_t) (base + (raw & 0xFFFFFFU));
}

static int psp_gfx_dl_is_end(u8 opcode) {
    return opcode == PSP_GFX_OP_F3D_ENDDL;
}

static int psp_gfx_dl_has_bounded_end(const Gfx* dl) {
    u32 i;

    for (i = 0; i < PSP_GFX_DL_MAX_NESTED_COMMANDS; i++) {
        if (psp_gfx_dl_is_end(psp_gfx_dl_opcode(&dl[i]))) {
            return 1;
        }
    }
    return 0;
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

static void psp_gfx_dl_mtx_copy(float out[4][4], const float in[4][4]) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row][col] = in[row][col];
        }
    }
}

static void psp_gfx_dl_mtx_mul_scalar(float out[4][4], const float a[4][4], const float b[4][4]) {
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

static void psp_gfx_dl_mtx_mul(
    float out[4][4],
    const float a[4][4],
    const float b[4][4]
) {
#if USE_N64PSP_MATH
    n64psp_mat4f alignedA;
    n64psp_mat4f alignedB;
    n64psp_mat4f alignedResult;

#if PSP_VALIDATE_N64PSP_MATH
    float scalarResult[4][4];
    u32 column;
    u32 row;
    int mismatch = 0;
#endif

    psp_gfx_dl_mtx_copy(alignedA.m, a);
    psp_gfx_dl_mtx_copy(alignedB.m, b);

    /*
     * SF64's old psp_gfx_dl_mtx_mul(a, b) composes b after a.
     *
     * n64psp_mat4f_mul(x, p, q) composes p after q.
     *
     * Reverse the wrapper arguments to preserve SF64 behaviour:
     *
     *     old(a, b) == n64psp(b, a)
     */
    n64psp_mat4f_mul(
        &alignedResult,
        &alignedB,
        &alignedA
    );

#if PSP_VALIDATE_N64PSP_MATH
    psp_gfx_dl_mtx_mul_scalar(scalarResult, a, b);

    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            float difference =
                scalarResult[column][row] -
                alignedResult.m[column][row];

            if (difference < 0.0f) {
                difference = -difference;
            }

            if (difference > 0.0001f) {
                mismatch = 1;
            }
        }
    }

    if (mismatch) {
        /*
         * Preserve rendering if validation ever fails.
         * Add first-mismatch logging separately when PSP_LOG=1.
         */
        psp_gfx_dl_mtx_copy(out, scalarResult);
        return;
    }
#endif

    psp_gfx_dl_mtx_copy(out, alignedResult.m);
#else
    psp_gfx_dl_mtx_mul_scalar(out, a, b);
#endif
}

#if (USE_N64PSP_BATCH_TRANSFORM + 0)
static void psp_gfx_dl_prepare_batch_matrices(
    const PspGfxDlContext* ctx,
    n64psp_mat4f* modelview,
    n64psp_mat4f* projectionModelview
) {
    n64psp_mat4f projection;

    if (ctx->hasModelview) {
        psp_gfx_dl_mtx_copy(
            modelview->m,
            ctx->modelview
        );
    } else {
        psp_gfx_dl_identity(modelview->m);
    }

    if (ctx->hasProjection) {
        psp_gfx_dl_mtx_copy(
            projection.m,
            ctx->projection
        );

        /*
         * projectionModelview(input)
         *     = projection(modelview(input))
         */
        n64psp_mat4f_mul(
            projectionModelview,
            &projection,
            modelview
        );
    } else {
        /*
         * The second batch output is unused without projection,
         * but the API still requires a valid second matrix.
         */
        *projectionModelview = *modelview;
    }
}
#endif

#if (USE_N64PSP_BATCH_TRANSFORM + 0)
static int psp_gfx_dl_store_transformed_vertex(
    PspGfxDlContext* ctx,
    PspGfxDlVertex* out,
    const n64psp_vec4f* view,
    const n64psp_vec4f* clip
) {
    out->viewX = view->x;
    out->viewY = view->y;
    out->viewZ = view->z;
    out->viewW = view->w;

    if (!ctx->hasProjection) {
        out->x = view->x / 320.0f;
        out->y = -view->y / 240.0f;
        out->z = view->z / 4096.0f;

        out->clipX = out->x;
        out->clipY = out->y;
        out->clipZ = out->z;
        out->clipW = 1.0f;

        out->clipCode =
            psp_gfx_dl_clip_code(
                out->x,
                out->y,
                out->z,
                out->clipW
            );

        out->projectionSerial = 0;
        return 1;
    }

    psp_gfx_dl_mtx_copy(
        out->projection,
        ctx->projection
    );

    out->projectionSerial = ctx->projectionSerial;

    if ((clip->w > -0.001f) && (clip->w < 0.001f)) {
        ctx->stats.nearZeroWCount++;
        return 0;
    }

    out->x = clip->x / clip->w;
    out->y = clip->y / clip->w;
    out->z = clip->z / clip->w;

    out->clipX = clip->x;
    out->clipY = clip->y;
    out->clipZ = clip->z;
    out->clipW = clip->w;

    out->clipCode =
        psp_gfx_dl_clip_code(
            clip->x,
            clip->y,
            clip->z,
            clip->w
        );

    if (clip->w < 0.0f) {
        ctx->stats.behindEyeVertexCount++;
    }

    return 1;
}
#endif

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
    out->viewX = x;
    out->viewY = y;
    out->viewZ = z;
    out->viewW = w;
    if (!ctx->hasProjection) {
        out->x = x / 320.0f;
        out->y = -y / 240.0f;
        out->z = z / 4096.0f;
        out->clipX = out->x;
        out->clipY = out->y;
        out->clipZ = out->z;
        out->clipW = 1.0f;
        out->clipCode = psp_gfx_dl_clip_code(out->x, out->y, out->z, out->clipW);
        out->projectionSerial = 0;
        return 1;
    }

    psp_gfx_dl_mtx_copy(out->projection, ctx->projection);
    out->projectionSerial = ctx->projectionSerial;
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

#if (USE_N64PSP_BATCH_TRANSFORM + 0) && \
    (PSP_VALIDATE_N64PSP_BATCH_TRANSFORM + 0)
static void psp_gfx_dl_reference_position_pair(
    const PspGfxDlContext* ctx,
    const Vtx* in,
    n64psp_vec4f_pair* reference
) {
    float x = (float) in->v.ob[0];
    float y = (float) in->v.ob[1];
    float z = (float) in->v.ob[2];
    float w = 1.0f;

    if (ctx->hasModelview) {
        psp_gfx_dl_transform(
            ctx->modelview,
            x,
            y,
            z,
            &x,
            &y,
            &z,
            &w
        );
    }

    reference->first.x = x;
    reference->first.y = y;
    reference->first.z = z;
    reference->first.w = w;

    if (ctx->hasProjection) {
        psp_gfx_dl_transform(
            ctx->projection,
            x,
            y,
            z,
            &reference->second.x,
            &reference->second.y,
            &reference->second.z,
            &reference->second.w
        );
    } else {
        reference->second = reference->first;
    }
}
#endif

#if (USE_N64PSP_BATCH_TRANSFORM + 0) && \
    (PSP_VALIDATE_N64PSP_BATCH_TRANSFORM + 0)
static int psp_gfx_dl_float_matches(
    float oldValue,
    float newValue,
    float absoluteTolerance,
    float relativeTolerance
) {
    float difference = fabsf(oldValue - newValue);
    float magnitude =
        fmaxf(fabsf(oldValue), fabsf(newValue));

    return difference <=
        (absoluteTolerance +
         (relativeTolerance * magnitude));
}
#endif

#if (USE_N64PSP_BATCH_TRANSFORM + 0) && \
    (PSP_VALIDATE_N64PSP_BATCH_TRANSFORM + 0)

static int psp_gfx_dl_validate_vec4(
    const PspGfxDlContext* ctx,
    const Gfx* gfx,
    u32 batchVertexIndex,
    const char* outputName,
    const n64psp_vec4f* reference,
    const n64psp_vec4f* actual,
    float absoluteTolerance,
    float relativeTolerance
) {
    const float referenceValues[4] = {
        reference->x,
        reference->y,
        reference->z,
        reference->w
    };

    const float actualValues[4] = {
        actual->x,
        actual->y,
        actual->z,
        actual->w
    };

    static const char* componentNames[4] = {
        "x", "y", "z", "w"
    };

    u32 component;

    for (component = 0; component < 4; component++) {
        float difference;

        if (psp_gfx_dl_float_matches(
                referenceValues[component],
                actualValues[component],
                absoluteTolerance,
                relativeTolerance)) {
            continue;
        }

        difference =
            fabsf(referenceValues[component] -
                   actualValues[component]);

        if (!sLoggedN64PspBatchTransformMismatch) {
            char line[256];

            snprintf(
                line,
                sizeof(line),
                "[n64psp-batch] mismatch "
                "task=%lu gfx=%p w0=%08lx w1=%08lx "
                "vertex=%lu output=%s component=%s "
                "old=%g new=%g diff=%g "
                "modelview=%d projection=%d projectionSerial=%lu",
                (unsigned long) ctx->taskIndex,
                (const void*) gfx,
                (unsigned long) gfx->words.w0,
                (unsigned long) gfx->words.w1,
                (unsigned long) batchVertexIndex,
                outputName,
                componentNames[component],
                (double) referenceValues[component],
                (double) actualValues[component],
                (double) difference,
                ctx->hasModelview,
                ctx->hasProjection,
                (unsigned long) ctx->projectionSerial
            );

            PspPlatform_LogLine(line);
            sLoggedN64PspBatchTransformMismatch = 1;
        }

        return 0;
    }

    return 1;
}

static int psp_gfx_dl_validate_projection_semantics(
    const PspGfxDlContext* ctx,
    const Gfx* gfx,
    u32 batchVertexIndex,
    const n64psp_vec4f* reference,
    const n64psp_vec4f* actual
) {
    int referenceNearZeroW;
    int actualNearZeroW;
    int referenceBehindEye;
    int actualBehindEye;
    u8 referenceClipCode;
    u8 actualClipCode;
    n64psp_vec4f referenceNdc;
    n64psp_vec4f actualNdc;

    referenceNearZeroW =
        (reference->w > -0.001f) &&
        (reference->w < 0.001f);

    actualNearZeroW =
        (actual->w > -0.001f) &&
        (actual->w < 0.001f);

    if (referenceNearZeroW != actualNearZeroW) {
        if (!sLoggedN64PspBatchTransformMismatch) {
            char line[192];

            snprintf(
                line,
                sizeof(line),
                "[n64psp-batch] semantic mismatch "
                "task=%lu gfx=%p vertex=%lu "
                "near-zero-w old=%d new=%d "
                "old-w=%g new-w=%g",
                (unsigned long) ctx->taskIndex,
                (const void*) gfx,
                (unsigned long) batchVertexIndex,
                referenceNearZeroW,
                actualNearZeroW,
                (double) reference->w,
                (double) actual->w
            );

            PspPlatform_LogLine(line);
            sLoggedN64PspBatchTransformMismatch = 1;
        }

        return 0;
    }

    if (referenceNearZeroW) {
        return 1;
    }

    referenceBehindEye = reference->w < 0.0f;
    actualBehindEye = actual->w < 0.0f;

    if (referenceBehindEye != actualBehindEye) {
        if (!sLoggedN64PspBatchTransformMismatch) {
            PspPlatform_LogLine(
                "[n64psp-batch] semantic mismatch: "
                "behind-eye classification"
            );
            sLoggedN64PspBatchTransformMismatch = 1;
        }

        return 0;
    }

    referenceClipCode =
        psp_gfx_dl_clip_code(
            reference->x,
            reference->y,
            reference->z,
            reference->w
        );

    actualClipCode =
        psp_gfx_dl_clip_code(
            actual->x,
            actual->y,
            actual->z,
            actual->w
        );

    if (referenceClipCode != actualClipCode) {
        if (!sLoggedN64PspBatchTransformMismatch) {
            char line[192];

            snprintf(
                line,
                sizeof(line),
                "[n64psp-batch] semantic mismatch "
                "task=%lu gfx=%p vertex=%lu "
                "clip-code old=%02x new=%02x",
                (unsigned long) ctx->taskIndex,
                (const void*) gfx,
                (unsigned long) batchVertexIndex,
                referenceClipCode,
                actualClipCode
            );

            PspPlatform_LogLine(line);
            sLoggedN64PspBatchTransformMismatch = 1;
        }

        return 0;
    }

    referenceNdc.x = reference->x / reference->w;
    referenceNdc.y = reference->y / reference->w;
    referenceNdc.z = reference->z / reference->w;
    referenceNdc.w = 1.0f;

    actualNdc.x = actual->x / actual->w;
    actualNdc.y = actual->y / actual->w;
    actualNdc.z = actual->z / actual->w;
    actualNdc.w = 1.0f;

    return psp_gfx_dl_validate_vec4(
        ctx,
        gfx,
        batchVertexIndex,
        "ndc",
        &referenceNdc,
        &actualNdc,
        0.0005f,
        0.0005f
    );
}

static void psp_gfx_dl_validate_position_pair(
    const PspGfxDlContext* ctx,
    const Gfx* gfx,
    const Vtx* input,
    u32 batchVertexIndex,
    const n64psp_vec4f_pair* actual
) {
    n64psp_vec4f_pair reference;

    psp_gfx_dl_reference_position_pair(
        ctx,
        input,
        &reference
    );

    if (!psp_gfx_dl_validate_vec4(
            ctx,
            gfx,
            batchVertexIndex,
            "modelview",
            &reference.first,
            &actual->first,
            0.0001f,
            0.00001f)) {
        return;
    }

    psp_gfx_dl_validate_vec4(
        ctx,
        gfx,
        batchVertexIndex,
        "projection-modelview",
        &reference.second,
        &actual->second,
        0.0005f,
        0.001f
    );

    if (ctx->hasProjection) {
        psp_gfx_dl_validate_projection_semantics(
            ctx,
            gfx,
            batchVertexIndex,
            &reference.second,
            &actual->second
        );
}

    if (ctx->hasProjection) {
    int oldNearZeroW =
        (reference.second.w > -0.001f) &&
        (reference.second.w < 0.001f);

    int newNearZeroW =
        (actual->second.w > -0.001f) &&
        (actual->second.w < 0.001f);

    if (oldNearZeroW != newNearZeroW) {
        PspPlatform_LogLine(
            "[n64psp-batch] near-zero W classification mismatch"
        );
        return;
    }

    if (!oldNearZeroW) {
        u8 oldClipCode =
            psp_gfx_dl_clip_code(
                reference.second.x,
                reference.second.y,
                reference.second.z,
                reference.second.w
            );

        u8 newClipCode =
            psp_gfx_dl_clip_code(
                actual->second.x,
                actual->second.y,
                actual->second.z,
                actual->second.w
            );

        if (oldClipCode != newClipCode) {
            PspPlatform_LogLine(
                "[n64psp-batch] clip-code mismatch"
            );
        }
    }
}
}

#endif

static void psp_gfx_dl_handle_mtx(PspGfxDlContext* ctx, const Gfx* gfx) {
    const Mtx* src = (const Mtx*) psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
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
    PspProfiler_CountMatrixCommand((flags & G_MTX_PROJECTION) != 0,
                                   ((flags & G_MTX_LOAD) == 0) && *hasTarget);

    psp_gfx_dl_mtx_l2f(loaded, src);
    if ((flags & G_MTX_LOAD) != 0) {
        psp_gfx_dl_mtx_copy(target, loaded);
    } else if (*hasTarget) {
        psp_gfx_dl_mtx_mul(target, loaded, target);
    } else {
        psp_gfx_dl_mtx_copy(target, loaded);
    }
    *hasTarget = 1;
    if ((flags & G_MTX_PROJECTION) != 0) {
        ctx->projectionSerial++;
        if (ctx->projectionSerial == 0) {
            ctx->projectionSerial = 1;
        }
    }
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

static void psp_gfx_dl_load_directional_light(PspGfxDlLight* dst, const Light* src) {
    float x;
    float y;
    float z;
    float lengthSquared;

    dst->r = src->l.col[0];
    dst->g = src->l.col[1];
    dst->b = src->l.col[2];

    x = (float) (s8) src->l.dir[0];
    y = (float) (s8) src->l.dir[1];
    z = (float) (s8) src->l.dir[2];

    lengthSquared = (x * x) + (y * y) + (z * z);

    if (lengthSquared > 0.000001f) {
        float inverseLength = 1.0f / sqrtf(lengthSquared);

        x *= inverseLength;
        y *= inverseLength;
        z *= inverseLength;
    } else {
        x = 0.0f;
        y = 0.0f;
        z = 0.0f;
    }

    dst->x = x;
    dst->y = y;
    dst->z = z;
}

static void psp_gfx_dl_load_ambient_light(PspGfxDlContext* ctx, const Light* src) {
    ctx->ambientR = src->l.col[0];
    ctx->ambientG = src->l.col[1];
    ctx->ambientB = src->l.col[2];
}

static void psp_gfx_dl_handle_movemem(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 index = (gfx->words.w0 >> 16) & 0xFF;
    const Vp* viewport;
    const Light* light;
    u32 lightSlot;

    if (index == G_MV_VIEWPORT) {
        viewport = (const Vp*) psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
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

    light = (const Light*) psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
    if (light == NULL) {
        return;
    }

    lightSlot = (index - G_MV_L0) >> 1;

    if (lightSlot < ctx->lightCount) {
        psp_gfx_dl_load_directional_light(
            &ctx->lights[lightSlot],
            light
        );
    } else if (lightSlot == ctx->lightCount) {
        psp_gfx_dl_load_ambient_light(ctx, light);
    }
}

static void psp_gfx_dl_flush_reason(PspGfxDlContext* ctx, PspProfileFlushReason reason) {
    if (ctx->batchCount == 0) {
        return;
    }
    (void) reason;
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_FLUSH);
    PspGfxPspgl_DrawColoredTriangles(sPspGfxDlBatch, ctx->batchCount, ctx->batchTextureId, ctx->batchTextureEnv,
                                     ctx->batchWrapS, ctx->batchWrapT, ctx->batchAlphaTest, ctx->batchBlend,
                                     ctx->batchPremultiplied, ctx->batchDepthTest, ctx->batchDepthWrite, ctx->batchFog,
                                     ctx->batchFogColor, ctx->batchFogStart, ctx->batchFogEnd,
                                     &ctx->batchProjection[0][0], ctx->batchPretransformed);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_FLUSH);
    PspProfiler_CountBatchFlush(reason, ctx->batchCount);
    ctx->stats.drawVertexCount += ctx->batchCount;
    ctx->batchCount = 0;
}

static void psp_gfx_dl_flush_texture_change(PspGfxDlContext* ctx, PspProfileTextureFlushSource source) {
    (void) source;
    if (ctx->batchCount != 0) {
        PspProfiler_CountTextureFlushSource(source);
    }
    psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_TEXTURE_CHANGE);
}

static void psp_gfx_dl_set_batch_texture(PspGfxDlContext* ctx, u32 textureId, PspGfxPspglTextureEnv textureEnv,
                                         PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT, int alphaTest,
                                         int blend, int premultiplied) {
    int textureIdChanged = ctx->batchTextureId != textureId;
    int textureEnvChanged = ctx->batchTextureEnv != textureEnv;
    int wrapSChanged = ctx->batchWrapS != wrapS;
    int wrapTChanged = ctx->batchWrapT != wrapT;
    int alphaTestChanged = ctx->batchAlphaTest != alphaTest;
    int blendChanged = ctx->batchBlend != blend;
    int premultipliedChanged = ctx->batchPremultiplied != premultiplied;

    if ((ctx->batchCount != 0) &&
        (textureIdChanged || textureEnvChanged || wrapSChanged || wrapTChanged || alphaTestChanged || blendChanged ||
         premultipliedChanged)) {
        PspProfiler_CountBatchStateTransitions(textureIdChanged, textureEnvChanged, wrapSChanged, wrapTChanged,
                                               alphaTestChanged, blendChanged, premultipliedChanged);
        if (textureIdChanged || textureEnvChanged || wrapSChanged || wrapTChanged) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_MATERIAL_KEY);
        } else {
            psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
        }
    }
    ctx->batchTextureId = textureId;
    ctx->batchTextureEnv = textureEnv;
    ctx->batchWrapS = wrapS;
    ctx->batchWrapT = wrapT;
    ctx->batchAlphaTest = alphaTest;
    ctx->batchBlend = blend;
    ctx->batchPremultiplied = premultiplied;
}

static void psp_gfx_dl_set_batch_depth(PspGfxDlContext* ctx, int depthTest, int depthWrite) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchDepthTest != depthTest) || (ctx->batchDepthWrite != depthWrite))) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->batchDepthTest = depthTest;
    ctx->batchDepthWrite = depthWrite;
}

static void psp_gfx_dl_set_batch_fog(PspGfxDlContext* ctx, int fog, const float projection[4][4]) {
    float color[4];
    float start = 0.0f;
    float end = 0.0f;

    fog = fog && (ctx->fogMul != 0);
    color[0] = (float) ctx->fogR / 255.0f;
    color[1] = (float) ctx->fogG / 255.0f;
    color[2] = (float) ctx->fogB / 255.0f;
    color[3] = (float) ctx->fogA / 255.0f;
    if (fog) {
        float startNdc = -(float) ctx->fogOffset / (float) ctx->fogMul;
        float endNdc = (255.0f - (float) ctx->fogOffset) / (float) ctx->fogMul;

        start = psp_gfx_dl_fog_distance(projection, startNdc);
        end = psp_gfx_dl_fog_distance(projection, endNdc);
        if ((start < 0.0f) || (end <= start)) {
            fog = 0;
        }
    }
    if ((ctx->batchCount != 0) &&
        ((ctx->batchFog != fog) || (ctx->batchFogColor[0] != color[0]) ||
         (ctx->batchFogColor[1] != color[1]) || (ctx->batchFogColor[2] != color[2]) ||
         (ctx->batchFogColor[3] != color[3]) || (ctx->batchFogStart != start) ||
         (ctx->batchFogEnd != end))) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->batchFog = fog;
    ctx->batchFogColor[0] = color[0];
    ctx->batchFogColor[1] = color[1];
    ctx->batchFogColor[2] = color[2];
    ctx->batchFogColor[3] = color[3];
    ctx->batchFogStart = start;
    ctx->batchFogEnd = end;
}

static void psp_gfx_dl_set_batch_transform(PspGfxDlContext* ctx, int pretransformed, u32 projectionSerial,
                                           const float projection[4][4]) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchPretransformed != pretransformed) ||
         (!pretransformed && (ctx->batchProjectionSerial != projectionSerial)))) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_TRANSFORM_CHANGE);
    }
    if (!ctx->batchTransformSet || (ctx->batchPretransformed != pretransformed) ||
        (!pretransformed && (ctx->batchProjectionSerial != projectionSerial))) {
        ctx->batchPretransformed = pretransformed;
        ctx->batchProjectionSerial = projectionSerial;
        if (!pretransformed && (projection != NULL)) {
            psp_gfx_dl_mtx_copy(ctx->batchProjection, projection);
        } else {
            psp_gfx_dl_identity(ctx->batchProjection);
        }
        ctx->batchTransformSet = 1;
    }
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
    dst->viewX = src->viewX;
    dst->viewY = src->viewY;
    dst->viewZ = src->viewZ;
    dst->viewW = src->viewW;
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
    dst->generated = 0;
}

static void psp_gfx_dl_emit_clip_vertex(PspGfxDlContext* ctx, const PspGfxDlClipVertex* src) {
    PspGfxPspglColorVertex* dst;
    float r;
    float g;
    float b;
    float a;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &sPspGfxDlBatch[ctx->batchCount++];

    if (ctx->batchPretransformed) {
        float inverseW = 1.0f / src->w;

        dst->x = src->x * inverseW;
        dst->y = src->y * inverseW;
        dst->z = src->z * inverseW;
    } else {
        dst->x = src->viewX;
        dst->y = src->viewY;
        dst->z = src->viewZ;
    }

    r = src->r;
    g = src->g;
    b = src->b;
    a = src->a;

    if (ctx->batchPremultiplied) {
        r *= a;
        g *= a;
        b *= a;
    }

    dst->color = psp_gfx_dl_pack_rgba(r, g, b, a);
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
    out->viewX = from->viewX + ((to->viewX - from->viewX) * t);
    out->viewY = from->viewY + ((to->viewY - from->viewY) * t);
    out->viewZ = from->viewZ + ((to->viewZ - from->viewZ) * t);
    out->viewW = from->viewW + ((to->viewW - from->viewW) * t);
    out->r = from->r + ((to->r - from->r) * t);
    out->g = from->g + ((to->g - from->g) * t);
    out->b = from->b + ((to->b - from->b) * t);
    out->a = from->a + ((to->a - from->a) * t);
    out->u = from->u + ((to->u - from->u) * t);
    out->v = from->v + ((to->v - from->v) * t);
    out->generated = 1;
}

static float psp_gfx_dl_triangle_w_ratio(const PspGfxDlClipVertex* a, const PspGfxDlClipVertex* b,
                                         const PspGfxDlClipVertex* c) {
    float minW = a->w;
    float maxW = a->w;

    if (b->w < minW) {
        minW = b->w;
    }
    if (c->w < minW) {
        minW = c->w;
    }
    if (b->w > maxW) {
        maxW = b->w;
    }
    if (c->w > maxW) {
        maxW = c->w;
    }
    if (minW <= 0.0f) {
        return 1.0f;
    }
    return maxW / minW;
}

static u32 psp_gfx_dl_emit_perspective_triangle(PspGfxDlContext* ctx, const PspGfxDlClipVertex* a,
                                                const PspGfxDlClipVertex* b, const PspGfxDlClipVertex* c,
                                                u32 depth) {
    const PspGfxDlClipVertex* low = a;
    const PspGfxDlClipVertex* high = a;
    PspGfxDlClipVertex midpoint;

    if ((depth >= PSP_GFX_DL_PERSPECTIVE_MAX_DEPTH) ||
        (psp_gfx_dl_triangle_w_ratio(a, b, c) <= PSP_GFX_DL_PERSPECTIVE_W_RATIO)) {
        psp_gfx_dl_emit_clip_vertex(ctx, a);
        psp_gfx_dl_emit_clip_vertex(ctx, b);
        psp_gfx_dl_emit_clip_vertex(ctx, c);
        ctx->stats.perspectiveTriangleCount++;
        return 1;
    }

    if (b->w < low->w) {
        low = b;
    }
    if (c->w < low->w) {
        low = c;
    }
    if (b->w > high->w) {
        high = b;
    }
    if (c->w > high->w) {
        high = c;
    }
    psp_gfx_dl_interpolate_clip_vertex(&midpoint, low, high, 0.5f);
    ctx->stats.perspectiveSplitCount++;

    if ((low == a) && (high == b)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1) +
               psp_gfx_dl_emit_perspective_triangle(ctx, &midpoint, b, c, depth + 1);
    }
    if ((low == b) && (high == a)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1) +
               psp_gfx_dl_emit_perspective_triangle(ctx, &midpoint, b, c, depth + 1);
    }
    if ((low == b) && (high == c)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, &midpoint, depth + 1) +
               psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1);
    }
    if ((low == c) && (high == b)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, &midpoint, depth + 1) +
               psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1);
    }
    return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, &midpoint, depth + 1) +
           psp_gfx_dl_emit_perspective_triangle(ctx, &midpoint, b, c, depth + 1);
}

static u32 psp_gfx_dl_emit_textured_triangle(PspGfxDlContext* ctx, const PspGfxDlClipVertex* a,
                                             const PspGfxDlClipVertex* b, const PspGfxDlClipVertex* c) {
    if (ctx->batchPretransformed) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, c, 0);
    }
    psp_gfx_dl_emit_clip_vertex(ctx, a);
    psp_gfx_dl_emit_clip_vertex(ctx, b);
    psp_gfx_dl_emit_clip_vertex(ctx, c);
    ctx->stats.perspectiveTriangleCount++;
    return 1;
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
                                            const PspGfxDlVertex* b, const PspGfxDlVertex* c, int textured) {
    PspGfxDlClipVertex buffers[2][PSP_GFX_DL_MAX_CLIP_VERTICES];
    PspGfxDlClipVertex* input = buffers[0];
    PspGfxDlClipVertex* output = buffers[1];
    PspGfxDlClipVertex* swap;
    u32 vertexCount = 3;
    u32 generatedCount = 0;
    u32 plane;
    u32 i;

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_CLIPPING);
    psp_gfx_dl_build_clip_vertex(ctx, a, &input[0]);
    psp_gfx_dl_build_clip_vertex(ctx, b, &input[1]);
    psp_gfx_dl_build_clip_vertex(ctx, c, &input[2]);
    for (plane = 0; plane < PSP_GFX_DL_CLIP_PLANES; plane++) {
        vertexCount = psp_gfx_dl_clip_polygon_plane(input, vertexCount, output, plane);
        if (vertexCount < 3) {
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_CLIPPING);
            return 0;
        }
        swap = input;
        input = output;
        output = swap;
    }

    if (vertexCount > ctx->stats.clipMaxPolygonVertexCount) {
        ctx->stats.clipMaxPolygonVertexCount = vertexCount;
    }
    if (textured) {
        float minW = input[0].w;
        float maxW = input[0].w;
        float minX = input[0].x / input[0].w;
        float maxX = minX;
        float minY = input[0].y / input[0].w;
        float maxY = minY;
        float minZ = input[0].z / input[0].w;
        float maxZ = minZ;
        float minU = input[0].u;
        float maxU = minU;
        float minV = input[0].v;
        float maxV = minV;

        for (i = 0; i < vertexCount; i++) {
            float ndcX = input[i].x / input[i].w;
            float ndcY = input[i].y / input[i].w;
            float ndcZ = input[i].z / input[i].w;

            if (input[i].generated) {
                generatedCount++;
            }
            if (input[i].w < minW) {
                minW = input[i].w;
            }
            if (input[i].w > maxW) {
                maxW = input[i].w;
            }
            if (ndcX < minX) {
                minX = ndcX;
            }
            if (ndcX > maxX) {
                maxX = ndcX;
            }
            if (ndcY < minY) {
                minY = ndcY;
            }
            if (ndcY > maxY) {
                maxY = ndcY;
            }
            if (ndcZ < minZ) {
                minZ = ndcZ;
            }
            if (ndcZ > maxZ) {
                maxZ = ndcZ;
            }
            if (input[i].u < minU) {
                minU = input[i].u;
            }
            if (input[i].u > maxU) {
                maxU = input[i].u;
            }
            if (input[i].v < minV) {
                minV = input[i].v;
            }
            if (input[i].v > maxV) {
                maxV = input[i].v;
            }
        }
        ctx->stats.clipGeneratedVertexCount += generatedCount;
        if (minW > 0.0f) {
            float wRatio = maxW / minW;

            if (wRatio > ctx->clipLargestWRatio) {
                ctx->clipLargestWRatio = wRatio;
            }
        }
        if (!ctx->hasClipSample) {
            ctx->hasClipSample = 1;
            ctx->clipSampleVertexCount = vertexCount;
            ctx->clipSampleGeneratedCount = generatedCount;
            ctx->clipSampleMinW = minW;
            ctx->clipSampleMaxW = maxW;
            ctx->clipSampleMinX = minX;
            ctx->clipSampleMaxX = maxX;
            ctx->clipSampleMinY = minY;
            ctx->clipSampleMaxY = maxY;
            ctx->clipSampleMinZ = minZ;
            ctx->clipSampleMaxZ = maxZ;
            ctx->clipSampleMinU = minU;
            ctx->clipSampleMaxU = maxU;
            ctx->clipSampleMinV = minV;
            ctx->clipSampleMaxV = maxV;
        }
    } else {
        for (i = 0; i < vertexCount; i++) {
            if (input[i].generated) {
                generatedCount++;
            }
        }
        ctx->stats.clipGeneratedVertexCount += generatedCount;
    }

    for (i = 1; i + 1 < vertexCount; i++) {
        if (textured) {
            psp_gfx_dl_emit_textured_triangle(ctx, &input[0], &input[i], &input[i + 1]);
        } else {
            psp_gfx_dl_emit_clip_vertex(ctx, &input[0]);
            psp_gfx_dl_emit_clip_vertex(ctx, &input[i]);
            psp_gfx_dl_emit_clip_vertex(ctx, &input[i + 1]);
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_CLIPPING);
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
    int pretransformed;

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        ctx->stats.invalidTriangleCount++;
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
        return;
    }

    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    textureId = ctx->textureEnabled ? ctx->textureId : 0;

    va = &ctx->vertices[a];
    vb = &ctx->vertices[b];
    vc = &ctx->vertices[c];
    pretransformed = !ctx->hasProjection || (va->projectionSerial == 0) ||
                     (va->projectionSerial != vb->projectionSerial) ||
                     (va->projectionSerial != vc->projectionSerial);
    if (pretransformed) {
        ctx->stats.pretransformedTriangleCount++;
    } else {
        ctx->stats.projectedTriangleCount++;
    }
    psp_gfx_dl_set_batch_transform(ctx, pretransformed, va->projectionSerial, va->projection);
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
    psp_gfx_dl_set_batch_texture(ctx, textureId, textureEnv,
                                 psp_gfx_dl_texture_wrap(ctx->textureCms, ctx->textureMaskS),
                                 psp_gfx_dl_texture_wrap(ctx->textureCmt, ctx->textureMaskT),
                                 psp_gfx_dl_alpha_test_enabled(ctx), psp_gfx_dl_blend_enabled(ctx),
                                 psp_gfx_dl_premultiplied_blend_enabled(ctx));
    psp_gfx_dl_set_batch_depth(ctx, (ctx->geometryMode & G_ZBUFFER) != 0, (ctx->otherModeL & Z_UPD) != 0);
    psp_gfx_dl_set_batch_fog(ctx, !pretransformed && ((ctx->otherModeL >> 30) == G_BL_CLR_FOG), va->projection);
    if (ctx->batchDepthTest) {
        ctx->stats.depthTestTriangleCount++;
    }
    if (ctx->batchDepthWrite) {
        ctx->stats.depthWriteTriangleCount++;
    }
    if (ctx->batchFog) {
        const PspGfxDlVertex* fogVertices[3] = { va, vb, vc };
        u32 fogVertexIndex;

        ctx->stats.fogTriangleCount++;
        for (fogVertexIndex = 0; fogVertexIndex < 3; fogVertexIndex++) {
            float fogDepth = -fogVertices[fogVertexIndex]->viewZ;

            if (fogVertices[fogVertexIndex]->viewW != 0.0f) {
                fogDepth /= fogVertices[fogVertexIndex]->viewW;
            }
            if (!ctx->hasFogDepthRange) {
                ctx->hasFogDepthRange = 1;
                ctx->fogRangeStart = ctx->batchFogStart;
                ctx->fogRangeEnd = ctx->batchFogEnd;
                ctx->fogDepthMin = fogDepth;
                ctx->fogDepthMax = fogDepth;
            } else {
                if (fogDepth < ctx->fogDepthMin) {
                    ctx->fogDepthMin = fogDepth;
                }
                if (fogDepth > ctx->fogDepthMax) {
                    ctx->fogDepthMax = fogDepth;
                }
            }
        }
    }
    if (sharedClipCode != 0) {
        emittedTriangles = 0;
        ctx->stats.clipRejectedTriangleCount++;
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
    } else if (combinedClipCode != 0) {
        u32 oldGeneratedVertices = ctx->stats.clipGeneratedVertexCount;
        (void) oldGeneratedVertices;
        ctx->stats.clippedTriangleCount++;
        if (textureId != 0) {
            ctx->stats.texturedClippedTriangleCount++;
        } else {
            ctx->stats.untexturedClippedTriangleCount++;
        }
        if ((combinedClipCode & (1U << 4)) != 0) {
            ctx->stats.nearPlaneClippedTriangleCount++;
        }
        emittedTriangles = psp_gfx_dl_emit_clipped_triangle(ctx, va, vb, vc, textureId != 0);
        if (emittedTriangles == 0) {
            ctx->stats.clipRejectedTriangleCount++;
            PspProfiler_CountTriangleResult(0, 1, 1, ctx->stats.clipGeneratedVertexCount - oldGeneratedVertices, 0);
        } else {
            ctx->stats.clipGeneratedTriangleCount += emittedTriangles;
            PspProfiler_CountTriangleResult(0, 0, 1, ctx->stats.clipGeneratedVertexCount - oldGeneratedVertices,
                                            emittedTriangles);
        }
    } else {
        PspGfxDlClipVertex vertices[3];

        psp_gfx_dl_build_clip_vertex(ctx, va, &vertices[0]);
        psp_gfx_dl_build_clip_vertex(ctx, vb, &vertices[1]);
        psp_gfx_dl_build_clip_vertex(ctx, vc, &vertices[2]);
        if (textureId != 0) {
            psp_gfx_dl_emit_textured_triangle(ctx, &vertices[0], &vertices[1], &vertices[2]);
        } else {
            psp_gfx_dl_emit_clip_vertex(ctx, &vertices[0]);
            psp_gfx_dl_emit_clip_vertex(ctx, &vertices[1]);
            psp_gfx_dl_emit_clip_vertex(ctx, &vertices[2]);
        }
        emittedTriangles = 1;
        PspProfiler_CountTriangleResult(1, 0, 0, 0, 1);
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
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
}

static void psp_gfx_dl_emit_rect_vertex(PspGfxDlContext* ctx,
                                        float x,
                                        float y,
                                        float u,
                                        float v) {
    PspGfxPspglColorVertex* dst;
    u32 r;
    u32 g;
    u32 b;
    u32 a;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &sPspGfxDlBatch[ctx->batchCount++];

    dst->u = u / (float) ctx->textureUploadWidth;
    dst->v = v / (float) ctx->textureUploadHeight;

    r = ctx->primitiveR;
    g = ctx->primitiveG;
    b = ctx->primitiveB;
    a = ctx->primitiveA;

    if (ctx->batchPremultiplied) {
        r = ((r * a) + 127U) / 255U;
        g = ((g * a) + 127U) / 255U;
        b = ((b * a) + 127U) / 255U;
    }

    /*
     * In memory on PSP:
     * byte 0 = red
     * byte 1 = green
     * byte 2 = blue
     * byte 3 = alpha
     */
    dst->color = r |
                 (g << 8) |
                 (b << 16) |
                 (a << 24);

    dst->x = (x / 160.0f) - 1.0f;
    dst->y = 1.0f - (y / 120.0f);
    dst->z = 0.0f;
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
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    if ((psp_gfx_dl_opcode(half1) != PSP_GFX_OP_F3D_RDPHALF_1) ||
        (psp_gfx_dl_opcode(half2) != PSP_GFX_OP_F3D_RDPHALF_2) || (ctx->textureId == 0) ||
        (ctx->textureUploadWidth == 0) || (ctx->textureUploadHeight == 0)) {
        ctx->stats.textureRectangleRejected++;
        return;
    }

    if (flip) {
        s1 = s0 + ((y1 - y0) * dsdx);
        t1 = t0 + ((x1 - x0) * dtdy);
    } else {
        s1 = s0 + ((x1 - x0) * dsdx);
        t1 = t0 + ((y1 - y0) * dtdy);
    }

    psp_gfx_dl_set_batch_texture(
        ctx, ctx->textureId, PSP_GFX_PSPGL_TEX_MODULATE,
        psp_gfx_dl_texture_wrap(ctx->textureCms, ctx->textureMaskS),
        psp_gfx_dl_texture_wrap(ctx->textureCmt, ctx->textureMaskT),
        psp_gfx_dl_alpha_test_enabled(ctx), psp_gfx_dl_blend_enabled(ctx),
        psp_gfx_dl_premultiplied_blend_enabled(ctx));
    psp_gfx_dl_set_batch_depth(ctx, 0, 0);
    psp_gfx_dl_set_batch_fog(ctx, 0, ctx->projection);
    psp_gfx_dl_set_batch_transform(ctx, 1, 0, NULL);

    if (flip) {
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y0, s0, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y1, s1, t0);
    } else {
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

static void psp_gfx_dl_handle_set_fog_color(PspGfxDlContext* ctx, const Gfx* gfx) {
    if (ctx->batchCount != 0) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->fogR = (u8) (gfx->words.w1 >> 24);
    ctx->fogG = (u8) (gfx->words.w1 >> 16);
    ctx->fogB = (u8) (gfx->words.w1 >> 8);
    ctx->fogA = (u8) gfx->words.w1;
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
    const Vtx* src = (const Vtx*) psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
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

PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_G_VTX);
PspProfiler_CountGvtx(count, (ctx->geometryMode & G_LIGHTING) != 0);

#if (USE_N64PSP_BATCH_TRANSFORM + 0)
    {
        n64psp_mat4f modelview;
        n64psp_mat4f projectionModelview;

        for (i = 0; i < count; i++) {
            const Vtx* in = &src[i];

            sPspGfxDlTransformInput[i].x =
                (float) in->v.ob[0];

            sPspGfxDlTransformInput[i].y =
                (float) in->v.ob[1];

            sPspGfxDlTransformInput[i].z =
                (float) in->v.ob[2];

            sPspGfxDlTransformInput[i].w = 1.0f;
        }

        psp_gfx_dl_prepare_batch_matrices(
            ctx,
            &modelview,
            &projectionModelview
        );

        n64psp_mat4f_transform_vec4_2mat_batch(
            sPspGfxDlTransformOutput,
            &modelview,
            &projectionModelview,
            sPspGfxDlTransformInput,
            count
        );
    }
#endif

    for (i = 0; i < count; i++) {
        const Vtx* in = &src[i];
        PspGfxDlVertex* out = &ctx->vertices[v0 + i];

    #if (USE_N64PSP_BATCH_TRANSFORM + 0)

    #if (PSP_VALIDATE_N64PSP_BATCH_TRANSFORM + 0)
            psp_gfx_dl_validate_position_pair(
                ctx,
                gfx,
                in,
                i,
                &sPspGfxDlTransformOutput[i]
            );
    #endif

            out->valid =
                psp_gfx_dl_store_transformed_vertex(
                    ctx,
                    out,
                    &sPspGfxDlTransformOutput[i].first,
                    &sPspGfxDlTransformOutput[i].second
                );
    #else
            out->valid =
                psp_gfx_dl_transform_vertex(
                    ctx,
                    in,
                    out
                );
    #endif
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
{
                float lengthSquared = (nx * nx) + (ny * ny) + (nz * nz);

                if (lengthSquared > 0.000001f) {
                    float inverseLength = 1.0f / sqrtf(lengthSquared);

                    nx *= inverseLength;
                    ny *= inverseLength;
                    nz *= inverseLength;
                }
            }

            for (lightIndex = 0; lightIndex < ctx->lightCount; lightIndex++) {
                const PspGfxDlLight* light = &ctx->lights[lightIndex];
                float dot;

                dot = (nx * light->x) +
                    (ny * light->y) +
                    (nz * light->z);

                if (dot > 0.0f) {
                    r += (float) light->r * dot;
                    g += (float) light->g * dot;
                    b += (float) light->b * dot;
                }
            }
            out->r = psp_gfx_dl_remap_lighting(r);
            out->g = psp_gfx_dl_remap_lighting(g);
            out->b = psp_gfx_dl_remap_lighting(b);
            ctx->lightingVertexCount++;
            if (!ctx->hasLightingRange) {
                ctx->lightingRawMin = fminf(r, fminf(g, b));
                ctx->lightingRawMax = fmaxf(r, fmaxf(g, b));
                ctx->lightingMappedMin = out->r;
                ctx->lightingMappedMax = out->r;
                ctx->hasLightingRange = 1;
            } else {
                ctx->lightingRawMin = fminf(ctx->lightingRawMin, fminf(r, fminf(g, b)));
                ctx->lightingRawMax = fmaxf(ctx->lightingRawMax, fmaxf(r, fmaxf(g, b)));
            }
            if (out->g < ctx->lightingMappedMin) {
                ctx->lightingMappedMin = out->g;
            }
            if (out->b < ctx->lightingMappedMin) {
                ctx->lightingMappedMin = out->b;
            }
            if (out->r > ctx->lightingMappedMax) {
                ctx->lightingMappedMax = out->r;
            }
            if (out->g > ctx->lightingMappedMax) {
                ctx->lightingMappedMax = out->g;
            }
            if (out->b > ctx->lightingMappedMax) {
                ctx->lightingMappedMax = out->b;
            }
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
    PspProfiler_CountTransformWork(count,
                                   (ctx->geometryMode & G_LIGHTING) != 0 ? count : 0,
                                   (ctx->geometryMode & G_LIGHTING) != 0 ? count : 0,
                                   (ctx->geometryMode & G_LIGHTING) != 0 ? count : 0,
                                   count,
                                   ctx->hasProjection ? count : 0);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_G_VTX);
}

static void psp_gfx_dl_handle_move_word(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 offset = (gfx->words.w0 >> 8) & 0xFFFF;
    u32 index = gfx->words.w0 & 0xFF;
    u32 encodedCount;

    if ((index == G_MW_SEGMENT) && ((offset & 3U) == 0)) {
        u32 segment = offset >> 2;

        if (segment < ARRAY_COUNT(ctx->segments)) {
            ctx->segments[segment] = gfx->words.w1;
        }
        return;
    }
    if ((index == G_MW_FOG) && (offset == G_MWO_FOG)) {
        if (ctx->batchCount != 0) {
            psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
        }
        ctx->fogMul = (s16) (gfx->words.w1 >> 16);
        ctx->fogOffset = (s16) gfx->words.w1;
        return;
    }
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
        (((ctx->otherModeL ^ gfx->words.w1) & mask &
          (0xC0000000U | 3U | CVG_X_ALPHA | FORCE_BL | Z_UPD)) != 0)) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->otherModeL = (ctx->otherModeL & ~mask) | (gfx->words.w1 & mask);
}

static void psp_gfx_dl_handle_geometry_mode(PspGfxDlContext* ctx, const Gfx* gfx, int set) {
    u32 nextGeometryMode;

    if (set) {
        nextGeometryMode = ctx->geometryMode | gfx->words.w1;
    } else {
        nextGeometryMode = ctx->geometryMode & ~gfx->words.w1;
    }
    ctx->geometryMode = nextGeometryMode;
}

static void psp_gfx_dl_handle_texture(PspGfxDlContext* ctx, const Gfx* gfx) {
    int enabled = (gfx->words.w0 & 0xFF) != G_OFF;

    if ((ctx->batchCount != 0) && (ctx->textureEnabled != enabled)) {
        psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_TEXTURE_ENABLE);
    }
    ctx->textureEnabled = enabled;
}

static void psp_gfx_dl_handle_set_texture_image(PspGfxDlContext* ctx, const Gfx* gfx) {
    psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_SET_TEXTURE_IMAGE);
    ctx->textureFormat = (gfx->words.w0 >> 21) & 0x7;
    ctx->textureSize = (gfx->words.w0 >> 19) & 0x3;
    ctx->textureImage = psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
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
    ctx->textureCmt = (gfx->words.w1 >> 18) & 0x3;
    ctx->textureMaskT = (gfx->words.w1 >> 14) & 0xF;
    ctx->textureCms = (gfx->words.w1 >> 8) & 0x3;
    ctx->textureMaskS = (gfx->words.w1 >> 4) & 0xF;
    ctx->textureUploadAttempted = 0;
}

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred, int premultiply) {
    int result;

    if ((ctx->textureId != 0) || ctx->textureUploadAttempted || (ctx->textureImage == NULL) ||
        (ctx->textureWidth == 0) || (ctx->textureHeight == 0)) {
        return ctx->textureId != 0;
    }
    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->texturePalette == NULL)) {
        return 0;
    }

    ctx->textureUploadAttempted = 1;
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
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
                                        premultiply, &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b)) {
        ctx->textureId =
            PspGfxPspgl_GetIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                     &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        ctx->textureId =
            PspGfxPspgl_GetIa16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                      &ctx->textureUploadWidth, &ctx->textureUploadHeight);
    } else {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
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
        result = 1;
    } else {
        ctx->stats.textureRejected++;
        result = 0;
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
    return result;
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
    psp_gfx_dl_prepare_texture(ctx, 0, psp_gfx_dl_premultiplied_blend_enabled(ctx));
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
        PspProfiler_CountOpcode(opcode);

        if (psp_gfx_dl_is_end(opcode)) {
            return 1;
        }

        if (opcode == PSP_GFX_OP_F3D_DL) {
            const Gfx* child = (const Gfx*) psp_gfx_dl_resolve_ptr(ctx, cmd->words.w1);
            int noPush = ((cmd->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH;
            int childHasEnd = (child != NULL) && psp_gfx_dl_has_bounded_end(child);

            if (!childHasEnd) {
                ctx->stats.nestedDlRejected++;
                ctx->stats.displayListPointerRejected++;
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
                if (sLoggedRejectedDlTargets < 8) {
                    char line[192];

                    snprintf(line, sizeof(line),
                             "[pspgl-dl] rejected target task=%lu depth=%lu cmd=%p w0=%08lx w1=%08lx target=%p",
                             (unsigned long) ctx->taskIndex, (unsigned long) depth, (const void*) cmd,
                             (unsigned long) cmd->words.w0, (unsigned long) cmd->words.w1,
                             (const void*) child);
                    PspPlatform_LogLine(line);
                    sLoggedRejectedDlTargets++;
                }
#endif
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

        if (opcode == G_SETFOGCOLOR) {
            psp_gfx_dl_handle_set_fog_color(ctx, cmd);
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
            PspProfiler_CountTriangleCommand(1, 1, 0);
            PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF),
                                psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF),
                                psp_gfx_dl_decode_tri_index(w1 & 0xFF));
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TRIANGLE);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_TRI2) {
            u32 w0 = cmd->words.w0;
            u32 w1 = cmd->words.w1;

            PspProfiler_CountTriangleCommand(2, 0, 1);
            PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w0 >> 16) & 0xFF),
                                psp_gfx_dl_decode_tri_index((w0 >> 8) & 0xFF),
                                psp_gfx_dl_decode_tri_index(w0 & 0xFF));
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF),
                                psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF),
                                psp_gfx_dl_decode_tri_index(w1 & 0xFF));
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TRIANGLE);
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
    ctx->fogA = 255;
    ctx->combineUsesTextureAlpha = 1;
}

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats) {
    PspGfxDlContext* ctx = &sPspGfxDlContext;
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
    char line[512];
#endif

    psp_gfx_dl_init_lighting_remap();
    psp_gfx_dl_reset_context(ctx);
    ctx->taskIndex = taskIndex;
    PspProfiler_CountDisplayListTask();
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_DL_TRAVERSAL);
    psp_gfx_dl_run_internal(ctx, dl, 0);
    psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_END_OF_TASK);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_DL_TRAVERSAL);

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
        if (ctx->hasFogDepthRange) {
            snprintf(line, sizeof(line),
                     "[pspgl-fog] task=%lu color=%u,%u,%u,%u factor=%d,%d range=%.2f..%.2f depth=%.2f..%.2f",
                     (unsigned long) taskIndex, ctx->fogR, ctx->fogG, ctx->fogB, ctx->fogA, ctx->fogMul,
                     ctx->fogOffset, ctx->fogRangeStart, ctx->fogRangeEnd, ctx->fogDepthMin, ctx->fogDepthMax);
            PspPlatform_LogLine(line);
        }
    }

    if (((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
         (ctx->stats.depthLimitHit != 0)) &&
        ((ctx->stats.vertexCount != 0) || (ctx->stats.invalidTriangleCount != 0) ||
         (ctx->stats.displayListPointerRejected != 0))) {
        snprintf(line, sizeof(line),
                 "[pspgl-geom] task=%lu nearW=%lu behindVtx=%lu invalidTri=%lu sharedClipTri=%lu "
                 "eyeCrossTri=%lu behindTri=%lu clippedTri=%lu nearClipTri=%lu clipRejectTri=%lu "
                 "clipGenTri=%lu clipTex=%lu/%lu clipGenVtx=%lu clipPolyMax=%lu wRatioMax=%.2f "
                 "perspSplit=%lu perspTri=%lu gpuProjTri=%lu preXformTri=%lu "
                 "degenerateTri=%lu depthTestTri=%lu depthWriteTri=%lu fogTri=%lu deferTex=%lu "
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
                 (unsigned long) ctx->stats.texturedClippedTriangleCount,
                 (unsigned long) ctx->stats.untexturedClippedTriangleCount,
                 (unsigned long) ctx->stats.clipGeneratedVertexCount,
                 (unsigned long) ctx->stats.clipMaxPolygonVertexCount,
                 ctx->clipLargestWRatio,
                 (unsigned long) ctx->stats.perspectiveSplitCount,
                 (unsigned long) ctx->stats.perspectiveTriangleCount,
                 (unsigned long) ctx->stats.projectedTriangleCount,
                 (unsigned long) ctx->stats.pretransformedTriangleCount,
                 (unsigned long) ctx->stats.degenerateTriangleCount,
                 (unsigned long) ctx->stats.depthTestTriangleCount,
                 (unsigned long) ctx->stats.depthWriteTriangleCount,
                 (unsigned long) ctx->stats.fogTriangleCount,
                 (unsigned long) ctx->stats.deferredTextureCount,
                 (unsigned long) ctx->stats.matrixPointerRejected,
                 (unsigned long) ctx->stats.vertexPointerRejected,
                 (unsigned long) ctx->stats.displayListPointerRejected,
                 (unsigned long) ctx->stats.maxDepthReached);
        PspPlatform_LogLine(line);
    }

    if (!sLoggedTexturedClipSample && ctx->hasClipSample) {
        snprintf(line, sizeof(line),
                 "[pspgl-clip-sample] task=%lu verts=%lu generated=%lu w=%.3f..%.3f "
                 "ndc=%.3f..%.3f,%.3f..%.3f,%.3f..%.3f uv=%.3f..%.3f,%.3f..%.3f",
                 (unsigned long) taskIndex, (unsigned long) ctx->clipSampleVertexCount,
                 (unsigned long) ctx->clipSampleGeneratedCount, ctx->clipSampleMinW, ctx->clipSampleMaxW,
                 ctx->clipSampleMinX, ctx->clipSampleMaxX, ctx->clipSampleMinY, ctx->clipSampleMaxY,
                 ctx->clipSampleMinZ, ctx->clipSampleMaxZ, ctx->clipSampleMinU, ctx->clipSampleMaxU,
                 ctx->clipSampleMinV, ctx->clipSampleMaxV);
        PspPlatform_LogLine(line);
        sLoggedTexturedClipSample = 1;
    }

    if ((ctx->lightingVertexCount != 0) &&
        (!sLoggedFirstLightingTask || (taskIndex < 4) || ((taskIndex % 30) == 0))) {
        snprintf(line, sizeof(line),
                 "[pspgl-light] task=%lu vertices=%lu lights=%lu ambient=%u,%u,%u "
                 "linear=%.1f..%.1f mapped=%u..%u",
                 (unsigned long) taskIndex, (unsigned long) ctx->lightingVertexCount,
                 (unsigned long) ctx->lightCount, ctx->ambientR, ctx->ambientG, ctx->ambientB,
                 ctx->lightingRawMin, ctx->lightingRawMax, ctx->lightingMappedMin,
                 ctx->lightingMappedMax);
        PspPlatform_LogLine(line);
        sLoggedFirstLightingTask = 1;
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
