#include "src/psp/gfx/gfx_psp_dl.h"

#include "buffers.h"
#include "macros.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#if SF64_PSP_PROFILE_COMPONENTS
#include "src/psp/render_component.h"
#endif

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

#ifndef USE_N64PSP_VERTEX_CHAIN2
#define USE_N64PSP_VERTEX_CHAIN2 0
#endif

#ifndef N64PSP_VERTEX_CHAIN2_VALIDATE
#define N64PSP_VERTEX_CHAIN2_VALIDATE 0
#endif

#ifndef N64PSP_VERTEX_BATCH_DIAGNOSTICS
#define N64PSP_VERTEX_BATCH_DIAGNOSTICS 0
#endif

static int sPspGfxDlBackgroundFeedbackPrimed = 0;
static u32 sPspGfxDlBackgroundFeedbackSeedColor = 0xFF000000u;

#ifndef USE_N64PSP_BATCH_LIGHTING
#define USE_N64PSP_BATCH_LIGHTING 0
#endif

#ifndef PSP_VALIDATE_N64PSP_BATCH_LIGHTING
#define PSP_VALIDATE_N64PSP_BATCH_LIGHTING 0
#endif

#if (USE_N64PSP_MATH + 0)
#include <n64psp/math.h>
#include <n64psp/lighting.h>
#endif

#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#ifndef SF64_PSP_DIRECT_TRI_FASTPATH
#define SF64_PSP_DIRECT_TRI_FASTPATH 1
#endif

#ifndef SF64_PSP_TRI2_PAIR_FASTPATH
#define SF64_PSP_TRI2_PAIR_FASTPATH 0
#endif

#ifndef SF64_PSP_TRI2_PAIR_VALIDATE
#define SF64_PSP_TRI2_PAIR_VALIDATE 0
#endif

#ifndef SF64_PSP_PROFILE_TRIVIAL_REJECTS
#define SF64_PSP_PROFILE_TRIVIAL_REJECTS 0
#endif

#ifndef SF64_PSP_PROFILE_VERTEX_REUSE
#define SF64_PSP_PROFILE_VERTEX_REUSE 0
#endif

#ifndef SF64_PSP_BATCH_STATE_CACHE
#define SF64_PSP_BATCH_STATE_CACHE 1
#endif

#ifndef SF64_PSP_EARLY_TRIVIAL_REJECT
#define SF64_PSP_EARLY_TRIVIAL_REJECT 0
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
    float x;
    float y;
    float z;
    float w;
} PspGfxDlVec4;

typedef struct {
    PspGfxDlVec4 view;
    PspGfxDlVec4 clip;
} PspGfxDlPositionPair;

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
    PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA,
    PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND,
} PspGfxDlCombineMode;

typedef struct {
    u32 textureId;
    PspGfxPspglTextureRef textureRef;
    PspGfxPspglTextureEnv textureEnv;
    u32 textureEnvColor;
    PspGfxPspglTextureWrap wrapS;
    PspGfxPspglTextureWrap wrapT;
    int alphaTest;
    int blend;
    int premultiplied;
    int valid;
    int dirty;
} PspGfxDlEffectiveMaterialState;

typedef struct {
    int depthTest;
    int depthWrite;
    int valid;
    int dirty;
} PspGfxDlEffectiveDepthState;

typedef struct {
    int fog;
    float color[4];
    float start;
    float end;
    int pretransformed;
    u32 projectionSerial;
    int valid;
    int dirty;
} PspGfxDlEffectiveFogState;

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
#if SF64_PSP_PROFILE_COMPONENTS
    u32 batchComponentMask;
#endif
#if (USE_N64PSP_VERTEX_CHAIN2 + 0) || \
    (USE_N64PSP_BATCH_LIGHTING + 0)
    n64psp_mat4f alignedModelview;
    n64psp_mat4f alignedProjection;
    u32 modelviewSerial;
    u32 cachedModelviewSerial;
    u32 cachedProjectionSerial;
    int alignedMatricesValid;
#endif
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
    u32 textureScaleS;
    u32 textureScaleT;
    u32 textureWidth;
    u32 textureHeight;
    u32 textureUploadWidth;
    u32 textureUploadHeight;
    u32 textureTileUls;
    u32 textureTileUlt;
    u32 textureCms;
    u32 textureCmt;
    u32 textureMaskS;
    u32 textureMaskT;
    u32 textureId;
    PspGfxPspglTextureRef textureRef;
    int textureUploadAttempted;
    u32 batchTextureId;
    PspGfxPspglTextureRef batchTextureRef;
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
    u8 environmentR;
    u8 environmentG;
    u8 environmentB;
    u8 environmentA;
    u32 fillColor;
    const void* colorImage;
    u32 colorImageFormat;
    u32 colorImageSize;
    u32 colorImageWidth;
    int colorImageIsDisplay;
    PspGfxDlCombineMode combineMode;
    PspGfxDlCombineMode batchCombineMode;
    PspGfxPspglTextureEnv batchTextureEnv;
    u32 batchTextureEnvColor;
    u32 batchPrimitiveColor;
    u32 batchEnvironmentColor;
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
    u32 otherModeH;
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
    PspGfxDlEffectiveMaterialState effectiveMaterial;
    PspGfxDlEffectiveDepthState effectiveDepth;
    PspGfxDlEffectiveFogState effectiveFog;
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    int trivialRejectDiagnosticActive;
#endif
#if SF64_PSP_PROFILE_VERTEX_REUSE
    u32 batchProvenanceCount;
#endif
#if PSP_RENDERER_DIAGNOSTICS
    u32 vtxCommandCount;
    u32 vtxBatchSizeHistogram[PSP_GFX_DL_MAX_VERTICES + 1];
    u32 vtxLightCountHistogram[8];
    u32 litVertexCount;
    u32 unlitVertexCount;
#endif
#if N64PSP_VERTEX_BATCH_DIAGNOSTICS
    u32 vtxDiagCommands;
    u32 vtxDiagVertices;
    u32 vtxDiagMinBatch;
    u32 vtxDiagMaxBatch;
    u32 vtxDiagBuckets[6];
#endif
} PspGfxDlContext;

static PspGfxDlContext sPspGfxDlContext;

static PspGfxPspglColorVertex
    sPspGfxDlBatch[PSP_GFX_DL_BATCH_VERTICES]
    __attribute__((aligned(16)));

#if SF64_PSP_PROFILE_VERTEX_REUSE
static PspProfileVertexReuseSource
    sPspGfxDlBatchProvenance[PSP_GFX_DL_BATCH_VERTICES]
    __attribute__((aligned(16)));
#endif

#if !(USE_N64PSP_VERTEX_CHAIN2 + 0)
static PspGfxDlPositionPair
    sPspGfxDlScalarTransformOutput[PSP_GFX_DL_MAX_VERTICES];
#endif

#if (USE_N64PSP_VERTEX_CHAIN2 + 0)
static n64psp_vec4f
    sPspGfxDlTransformInput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));

static n64psp_vec4f_pair
    sPspGfxDlTransformOutput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));
#endif

#if (USE_N64PSP_VERTEX_CHAIN2 + 0) && \
    (N64PSP_VERTEX_CHAIN2_VALIDATE + 0)
static int sLoggedN64PspBatchTransformMismatch;
static int sLoggedN64PspBatchTransformDetail;
#endif

#if (USE_N64PSP_BATCH_LIGHTING + 0)
static n64psp_snorm8x4
    sPspGfxDlLightingNormals[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));

static n64psp_vec4f
    sPspGfxDlLightingOutput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));

static n64psp_vec4f
    sPspGfxDlLightingAmbient
    __attribute__((aligned(16)));

static n64psp_directional_lightf
    sPspGfxDlLightingLights[7]
    __attribute__((aligned(16)));
#endif

#if (USE_N64PSP_BATCH_LIGHTING + 0) && \
    (PSP_VALIDATE_N64PSP_BATCH_LIGHTING + 0)
static n64psp_vec4f
    sPspGfxDlLightingReference[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));

static int sLoggedN64PspBatchLightingMismatch;
#endif

static void psp_gfx_dl_mark_effective_material_dirty(PspGfxDlContext* ctx) {
    ctx->effectiveMaterial.dirty = 1;
}

static void psp_gfx_dl_mark_effective_depth_dirty(PspGfxDlContext* ctx) {
    ctx->effectiveDepth.dirty = 1;
}

static void psp_gfx_dl_mark_effective_fog_dirty(PspGfxDlContext* ctx) {
    ctx->effectiveFog.dirty = 1;
}

static void psp_gfx_dl_mark_effective_state_dirty(PspGfxDlContext* ctx) {
    psp_gfx_dl_mark_effective_material_dirty(ctx);
    psp_gfx_dl_mark_effective_depth_dirty(ctx);
    psp_gfx_dl_mark_effective_fog_dirty(ctx);
}

#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
static int sLoggedFirstDrawableTask;
static int sLoggedFirstLightingTask;
static int sLoggedTexturedClipSample;
static u32 sLoggedRejectedDlTargets;
#endif

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred, int premultiply);

/*
 * Calculated lighting always goes through the square-root transfer LUT
 * (gPspGfxColorTransferLut), independent of SF64_PSP_COLOR_TRANSFER; the
 * combine inputs it feeds must not be transformed again downstream.
 */
static u8 psp_gfx_dl_remap_lighting(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 255.0f) {
        return 255;
    }

    return gPspGfxColorTransferLut[(u8) value];
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

static u32 psp_gfx_dl_pack_rgba_u8(u32 r, u32 g, u32 b, u32 a, int premultiplied) {
    if (premultiplied) {
        r = ((r * a) + 127U) / 255U;
        g = ((g * a) + 127U) / 255U;
        b = ((b * a) + 127U) / 255U;
    }
    return r | (g << 8) | (b << 16) | (a << 24);
}

/* Fill-rectangle colours only; RGB carries the transfer policy, alpha raw. */
static u32 psp_gfx_dl_rgba5551_to_rgba8888(u16 color) {
    return psp_gfx_rgba5551_to_abgr8888(color);
}

static u32 psp_gfx_dl_primitive_color(const PspGfxDlContext* ctx) {
    return psp_gfx_dl_pack_rgba_u8(ctx->primitiveR, ctx->primitiveG, ctx->primitiveB, ctx->primitiveA, 0);
}

static u32 psp_gfx_dl_primitive_rgb_texture_env_color(const PspGfxDlContext* ctx) {
    return psp_gfx_dl_pack_rgba_u8(ctx->primitiveR, ctx->primitiveG, ctx->primitiveB, 255U, 0);
}

static u32 psp_gfx_dl_environment_color(const PspGfxDlContext* ctx) {
    return psp_gfx_dl_pack_rgba_u8(ctx->environmentR, ctx->environmentG, ctx->environmentB, ctx->environmentA, 0);
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

static float psp_gfx_dl_normalize_s10_5_scaled(s16 coord, u32 uploadSize, u32 tileOrigin, u32 scale) {
    float scaledCoord = ((float) coord * (float) scale) / 65536.0f;

    if (uploadSize == 0) {
        return 0.0f;
    }
    return (scaledCoord - ((float) tileOrigin * 8.0f)) / (32.0f * (float) uploadSize);
}

static float psp_gfx_dl_normalize_s10_5_s(const PspGfxDlContext* ctx, s16 coord, u32 uploadSize, u32 tileOrigin) {
    return psp_gfx_dl_normalize_s10_5_scaled(coord, uploadSize, tileOrigin, ctx->textureScaleS);
}

static float psp_gfx_dl_normalize_s10_5_t(const PspGfxDlContext* ctx, s16 coord, u32 uploadSize, u32 tileOrigin) {
    return psp_gfx_dl_normalize_s10_5_scaled(coord, uploadSize, tileOrigin, ctx->textureScaleT);
}

static float psp_gfx_dl_normalize_texel_coord(const PspGfxDlContext* ctx, float coord, u32 uploadSize,
                                              u32 tileOrigin) {
    (void) ctx;

    if (uploadSize == 0) {
        return 0.0f;
    }
    return (coord - ((float) tileOrigin * 0.25f)) / (float) uploadSize;
}

static int psp_gfx_dl_alpha_test_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha &&
           (((ctx->otherModeL & 3U) != G_AC_NONE) || ((ctx->otherModeL & CVG_X_ALPHA) != 0));
}

static int psp_gfx_dl_blend_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha && ((ctx->otherModeL & FORCE_BL) != 0);
}

static int psp_gfx_dl_premultiplied_blend_enabled(PspGfxDlContext* ctx) {
    if (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        return 0;
    }
    return psp_gfx_dl_blend_enabled(ctx) && ((ctx->otherModeL & CVG_DST_SAVE) == CVG_DST_SAVE) &&
           (ctx->textureFormat == G_IM_FMT_RGBA) &&
           ((ctx->textureSize == G_IM_SIZ_16b) || (ctx->textureSize == G_IM_SIZ_32b));
}

static int psp_gfx_dl_baked_env_blend_texture_enabled(const PspGfxDlContext* ctx) {
    return (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) &&
           (ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_32b);
}

static PspGfxPspglTextureEnv psp_gfx_dl_texture_env_for_combine(const PspGfxDlContext* ctx) {
    if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
        return PSP_GFX_PSPGL_TEX_REPLACE;
    }
    if (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        return PSP_GFX_PSPGL_TEX_BLEND;
    }
    if ((ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA)) {
        return PSP_GFX_PSPGL_TEX_MODULATE;
    }
    return PSP_GFX_PSPGL_TEX_REPLACE;
}

static u32 psp_gfx_dl_texture_env_color_for_combine(const PspGfxDlContext* ctx) {
    if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
        return 0;
    }
    if (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        return psp_gfx_dl_primitive_rgb_texture_env_color(ctx);
    }
    return 0;
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

static int psp_gfx_dl_is_display_color_image(const void* image) {
    u32 i;

    if (image == NULL) {
        return 1;
    }
    if ((gFrameBuffer != NULL) && ((image == gFrameBuffer) || (image == gFrameBuffer->data))) {
        return 1;
    }
    for (i = 0; i < 3U; i++) {
        if ((image == &gFrameBuffers[i]) || (image == gFrameBuffers[i].data)) {
            return 1;
        }
    }
    return 0;
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

#if (USE_N64PSP_VERTEX_CHAIN2 + 0) || \
    (USE_N64PSP_BATCH_LIGHTING + 0)
static void psp_gfx_dl_bump_serial(u32* serial) {
    (*serial)++;
    if (*serial == 0) {
        *serial = 1;
    }
}

static void psp_gfx_dl_prepare_batch_matrices(PspGfxDlContext* ctx) {
    int modelviewChanged =
        !ctx->alignedMatricesValid ||
        (ctx->cachedModelviewSerial != ctx->modelviewSerial);

    if (modelviewChanged) {
        if (ctx->hasModelview) {
            psp_gfx_dl_mtx_copy(
                ctx->alignedModelview.m,
                ctx->modelview
            );
        } else {
            psp_gfx_dl_identity(ctx->alignedModelview.m);
        }

        ctx->cachedModelviewSerial = ctx->modelviewSerial;
    }

    if (ctx->alignedMatricesValid &&
        !modelviewChanged &&
        (ctx->cachedProjectionSerial == ctx->projectionSerial)) {
        return;
    }

    if (ctx->hasProjection) {
        psp_gfx_dl_mtx_copy(ctx->alignedProjection.m, ctx->projection);
    } else {
        psp_gfx_dl_identity(ctx->alignedProjection.m);
    }

    ctx->cachedProjectionSerial = ctx->projectionSerial;
    ctx->alignedMatricesValid = 1;
}
#endif

static int psp_gfx_dl_store_transformed_vertex(
    PspGfxDlContext* ctx,
    PspGfxDlVertex* out,
    const PspGfxDlVec4* view,
    const PspGfxDlVec4* clip
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

static void psp_gfx_dl_transform(const float mtx[4][4], float inX, float inY, float inZ, float* outX, float* outY,
                                 float* outZ, float* outW) {
    *outX = (mtx[0][0] * inX) + (mtx[1][0] * inY) + (mtx[2][0] * inZ) + mtx[3][0];
    *outY = (mtx[0][1] * inX) + (mtx[1][1] * inY) + (mtx[2][1] * inZ) + mtx[3][1];
    *outZ = (mtx[0][2] * inX) + (mtx[1][2] * inY) + (mtx[2][2] * inZ) + mtx[3][2];
    *outW = (mtx[0][3] * inX) + (mtx[1][3] * inY) + (mtx[2][3] * inZ) + mtx[3][3];
}

#if !(USE_N64PSP_VERTEX_CHAIN2 + 0)
static void psp_gfx_dl_transform_position_pair(
    PspGfxDlContext* ctx,
    const Vtx* in,
    PspGfxDlPositionPair* out
) {
    float x = in->v.ob[0];
    float y = in->v.ob[1];
    float z = in->v.ob[2];
    float w = 1.0f;

    if (ctx->hasModelview) {
        psp_gfx_dl_transform(ctx->modelview, x, y, z, &x, &y, &z, &w);
    }

    out->view.x = x;
    out->view.y = y;
    out->view.z = z;
    out->view.w = w;

    if (ctx->hasProjection) {
        psp_gfx_dl_transform(
            ctx->projection,
            x,
            y,
            z,
            &out->clip.x,
            &out->clip.y,
            &out->clip.z,
            &out->clip.w
        );
    } else {
        out->clip = out->view;
    }
}
#endif

#if (USE_N64PSP_VERTEX_CHAIN2 + 0) && \
    (N64PSP_VERTEX_CHAIN2_VALIDATE + 0)
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

#if ((USE_N64PSP_VERTEX_CHAIN2 + 0) && \
     (N64PSP_VERTEX_CHAIN2_VALIDATE + 0)) || \
    ((USE_N64PSP_BATCH_LIGHTING + 0) && \
     (PSP_VALIDATE_N64PSP_BATCH_LIGHTING + 0))
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

#if (USE_N64PSP_VERTEX_CHAIN2 + 0) && \
    (N64PSP_VERTEX_CHAIN2_VALIDATE + 0)

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
                "[n64psp-chain2] mismatch "
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
                "[n64psp-chain2] semantic mismatch "
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
                "[n64psp-chain2] semantic mismatch: "
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
                "[n64psp-chain2] semantic mismatch "
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

static void psp_gfx_dl_log_chain2_mismatch_detail(
    const PspGfxDlContext* ctx,
    const Vtx* input,
    const n64psp_vec4f_pair* reference,
    const n64psp_vec4f_pair* actual
) {
    char line[512];
    u32 row;

    if (sLoggedN64PspBatchTransformDetail) {
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "[n64psp-chain2] detail src=%d,%d,%d "
        "scalarFirst=%.6g,%.6g,%.6g,%.6g "
        "chainFirst=%.6g,%.6g,%.6g,%.6g "
        "scalarSecond=%.6g,%.6g,%.6g,%.6g "
        "chainSecond=%.6g,%.6g,%.6g,%.6g",
        (int) input->v.ob[0],
        (int) input->v.ob[1],
        (int) input->v.ob[2],
        reference->first.x,
        reference->first.y,
        reference->first.z,
        reference->first.w,
        actual->first.x,
        actual->first.y,
        actual->first.z,
        actual->first.w,
        reference->second.x,
        reference->second.y,
        reference->second.z,
        reference->second.w,
        actual->second.x,
        actual->second.y,
        actual->second.z,
        actual->second.w
    );
    PspPlatform_LogLine(line);

    for (row = 0; row < 4; row++) {
        snprintf(
            line,
            sizeof(line),
            "[n64psp-chain2] mtx row=%lu modelview=%.6g,%.6g,%.6g,%.6g "
            "projection=%.6g,%.6g,%.6g,%.6g",
            (unsigned long) row,
            ctx->modelview[row][0],
            ctx->modelview[row][1],
            ctx->modelview[row][2],
            ctx->modelview[row][3],
            ctx->projection[row][0],
            ctx->projection[row][1],
            ctx->projection[row][2],
            ctx->projection[row][3]
        );
        PspPlatform_LogLine(line);
    }

    sLoggedN64PspBatchTransformDetail = 1;
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
        psp_gfx_dl_log_chain2_mismatch_detail(
            ctx,
            input,
            &reference,
            actual
        );
        return;
    }

    if (!psp_gfx_dl_validate_vec4(
            ctx,
            gfx,
            batchVertexIndex,
            "projection",
            &reference.second,
            &actual->second,
            0.0005f,
            0.001f)) {
        psp_gfx_dl_log_chain2_mismatch_detail(
            ctx,
            input,
            &reference,
            actual
        );
        return;
    }

    if (ctx->hasProjection) {
        if (!psp_gfx_dl_validate_projection_semantics(
                ctx,
                gfx,
                batchVertexIndex,
                &reference.second,
                &actual->second)) {
            psp_gfx_dl_log_chain2_mismatch_detail(
                ctx,
                input,
                &reference,
                actual
            );
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
#if (USE_N64PSP_VERTEX_CHAIN2 + 0) || \
    (USE_N64PSP_BATCH_LIGHTING + 0)
        psp_gfx_dl_bump_serial(&ctx->projectionSerial);
#else
        ctx->projectionSerial++;
        if (ctx->projectionSerial == 0) {
            ctx->projectionSerial = 1;
        }
#endif
        psp_gfx_dl_mark_effective_fog_dirty(ctx);
#if (USE_N64PSP_VERTEX_CHAIN2 + 0) || \
    (USE_N64PSP_BATCH_LIGHTING + 0)
    } else {
        psp_gfx_dl_bump_serial(&ctx->modelviewSerial);
#endif
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
#if (USE_N64PSP_VERTEX_CHAIN2 + 0) || \
    (USE_N64PSP_BATCH_LIGHTING + 0)
    psp_gfx_dl_bump_serial(&ctx->modelviewSerial);
#endif
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

static void psp_gfx_dl_light_vertex_scalar(
    const PspGfxDlContext* ctx,
    const Vtx* input,
    float* outR,
    float* outG,
    float* outB
) {
    float nx = (float) (s8) input->v.cn[0];
    float ny = (float) (s8) input->v.cn[1];
    float nz = (float) (s8) input->v.cn[2];
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

    *outR = r;
    *outG = g;
    *outB = b;
}

#if (USE_N64PSP_BATCH_LIGHTING + 0)
static void psp_gfx_dl_stage_lighting_batch(
    PspGfxDlContext* ctx,
    const Vtx* src,
    u32 count
) {
    u32 i;

    for (i = 0; i < count; i++) {
        sPspGfxDlLightingNormals[i].x = (int8_t) src[i].v.cn[0];
        sPspGfxDlLightingNormals[i].y = (int8_t) src[i].v.cn[1];
        sPspGfxDlLightingNormals[i].z = (int8_t) src[i].v.cn[2];
        sPspGfxDlLightingNormals[i].w = 0;
    }

    sPspGfxDlLightingAmbient.x = (float) ctx->ambientR;
    sPspGfxDlLightingAmbient.y = (float) ctx->ambientG;
    sPspGfxDlLightingAmbient.z = (float) ctx->ambientB;
    sPspGfxDlLightingAmbient.w = 0.0f;

    for (i = 0; i < ctx->lightCount; i++) {
        sPspGfxDlLightingLights[i].direction.x = ctx->lights[i].x;
        sPspGfxDlLightingLights[i].direction.y = ctx->lights[i].y;
        sPspGfxDlLightingLights[i].direction.z = ctx->lights[i].z;
        sPspGfxDlLightingLights[i].direction.w = 0.0f;
        sPspGfxDlLightingLights[i].color.x = (float) ctx->lights[i].r;
        sPspGfxDlLightingLights[i].color.y = (float) ctx->lights[i].g;
        sPspGfxDlLightingLights[i].color.z = (float) ctx->lights[i].b;
        sPspGfxDlLightingLights[i].color.w = 0.0f;
    }
}
#endif

#if (USE_N64PSP_BATCH_LIGHTING + 0) && \
    (PSP_VALIDATE_N64PSP_BATCH_LIGHTING + 0)
static int psp_gfx_dl_validate_lighting_batch(
    const PspGfxDlContext* ctx,
    const Vtx* src,
    u32 batchVertexIndex,
    const n64psp_vec4f* actual
) {
    float scalarR;
    float scalarG;
    float scalarB;
    u8 scalarMappedR;
    u8 scalarMappedG;
    u8 scalarMappedB;
    u8 actualMappedR;
    u8 actualMappedG;
    u8 actualMappedB;

    psp_gfx_dl_light_vertex_scalar(
        ctx,
        src,
        &scalarR,
        &scalarG,
        &scalarB
    );

    sPspGfxDlLightingReference[batchVertexIndex].x = scalarR;
    sPspGfxDlLightingReference[batchVertexIndex].y = scalarG;
    sPspGfxDlLightingReference[batchVertexIndex].z = scalarB;
    sPspGfxDlLightingReference[batchVertexIndex].w = 0.0f;

    scalarMappedR = psp_gfx_dl_remap_lighting(scalarR);
    scalarMappedG = psp_gfx_dl_remap_lighting(scalarG);
    scalarMappedB = psp_gfx_dl_remap_lighting(scalarB);
    actualMappedR = psp_gfx_dl_remap_lighting(actual->x);
    actualMappedG = psp_gfx_dl_remap_lighting(actual->y);
    actualMappedB = psp_gfx_dl_remap_lighting(actual->z);

    if (psp_gfx_dl_float_matches(scalarR, actual->x, 0.0001f, 0.0001f) &&
        psp_gfx_dl_float_matches(scalarG, actual->y, 0.0001f, 0.0001f) &&
        psp_gfx_dl_float_matches(scalarB, actual->z, 0.0001f, 0.0001f) &&
        (scalarMappedR == actualMappedR) &&
        (scalarMappedG == actualMappedG) &&
        (scalarMappedB == actualMappedB)) {
        return 1;
    }

    if (!sLoggedN64PspBatchLightingMismatch) {
        char line[512];

        snprintf(
            line,
            sizeof(line),
            "[n64psp-light] task=%lu vtx=%lu lights=%lu normal=%d,%d,%d "
            "scalar=%.4f,%.4f,%.4f batch=%.4f,%.4f,%.4f mapped=%u,%u,%u/%u,%u,%u",
            (unsigned long) ctx->taskIndex,
            (unsigned long) batchVertexIndex,
            (unsigned long) ctx->lightCount,
            (int) (s8) src->v.cn[0],
            (int) (s8) src->v.cn[1],
            (int) (s8) src->v.cn[2],
            scalarR,
            scalarG,
            scalarB,
            actual->x,
            actual->y,
            actual->z,
            scalarMappedR,
            scalarMappedG,
            scalarMappedB,
            actualMappedR,
            actualMappedG,
            actualMappedB
        );
        PspPlatform_LogLine(line);
        sLoggedN64PspBatchLightingMismatch = 1;
    }

    return 0;
}
#endif

static void psp_gfx_dl_load_ambient_light(PspGfxDlContext* ctx, const Light* src) {
    ctx->ambientR = src->l.col[0];
    ctx->ambientG = src->l.col[1];
    ctx->ambientB = src->l.col[2];
}

#if SF64_PSP_PROFILE_COMPONENTS
static u32 psp_gfx_dl_component_bit(u32 component) {
    if (component >= PSP_PROFILE_COMPONENT_COUNT) {
        component = PSP_PROFILE_COMPONENT_UNATTRIBUTED;
    }
    return 1UL << component;
}

static u32 psp_gfx_dl_component_popcount(u32 mask) {
    u32 count = 0;

    while (mask != 0) {
        count += mask & 1U;
        mask >>= 1;
    }
    return count;
}

static void psp_gfx_dl_mark_batch_component(PspGfxDlContext* ctx) {
    ctx->batchComponentMask |= psp_gfx_dl_component_bit(PspProfiler_ComponentCurrentId());
}

static u32 psp_gfx_dl_batch_owner_component(const PspGfxDlContext* ctx) {
    u32 mask = ctx->batchComponentMask;
    u32 component;

    if (mask == 0) {
        return PSP_PROFILE_COMPONENT_UNATTRIBUTED;
    }
    if (psp_gfx_dl_component_popcount(mask) != 1) {
        return PSP_PROFILE_COMPONENT_MIXED_BATCH;
    }
    for (component = 0; component < PSP_PROFILE_COMPONENT_COUNT; component++) {
        if ((mask & psp_gfx_dl_component_bit(component)) != 0) {
            return component;
        }
    }
    return PSP_PROFILE_COMPONENT_UNATTRIBUTED;
}
#else
#define psp_gfx_dl_mark_batch_component(ctx) ((void) 0)
#endif

#if SF64_PSP_PROFILE_VERTEX_REUSE
#define psp_gfx_dl_mark_vertex_reuse_source(ctx, source)                                \
    do {                                                                                \
        if ((ctx)->batchProvenanceCount < PSP_GFX_DL_BATCH_VERTICES) {                  \
            sPspGfxDlBatchProvenance[(ctx)->batchProvenanceCount++] = (source);         \
        }                                                                               \
    } while (0)
#define psp_gfx_dl_clipped_vertex_source(src)                                            \
    ((src)->generated ? PSP_PROFILE_VERTEX_REUSE_SOURCE_CLIPPED_GENERATED                \
                      : PSP_PROFILE_VERTEX_REUSE_SOURCE_CLIPPED_ORIGINAL)
#define psp_gfx_dl_resolve_vertex_reuse_source(src, source)                              \
    (((source) == PSP_PROFILE_VERTEX_REUSE_SOURCE_CLIPPED_ORIGINAL)                      \
         ? psp_gfx_dl_clipped_vertex_source(src)                                         \
         : (source))
#define psp_gfx_dl_reset_vertex_reuse_batch(ctx)                                        \
    do {                                                                                \
        (ctx)->batchProvenanceCount = 0;                                                \
    } while (0)
#else
#define psp_gfx_dl_mark_vertex_reuse_source(ctx, source) ((void) 0)
#define psp_gfx_dl_clipped_vertex_source(src) 0
#define psp_gfx_dl_resolve_vertex_reuse_source(src, source) 0
#define psp_gfx_dl_reset_vertex_reuse_batch(ctx) ((void) 0)
#endif

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
#if SF64_PSP_PROFILE_COMPONENTS
    u32 ownerComponent;
    u32 ownerMask;
#elif SF64_PSP_PROFILE_VERTEX_REUSE
    u32 ownerComponent = 0;
    u32 ownerMask = 0;
#endif

    if (ctx->batchCount == 0) {
#if SF64_PSP_PROFILE_COMPONENTS
        ctx->batchComponentMask = 0;
#endif
        psp_gfx_dl_reset_vertex_reuse_batch(ctx);
        return;
    }
#if SF64_PSP_PROFILE_COMPONENTS
    ownerMask = ctx->batchComponentMask;
    ownerComponent = psp_gfx_dl_batch_owner_component(ctx);
    PspProfiler_ComponentScopeBegin(ownerComponent);
#endif
#if SF64_PSP_PROFILE_VERTEX_REUSE
    PspProfiler_AnalyzeRendererBatchVertexReuse(sPspGfxDlBatch, ctx->batchCount, ownerComponent, ownerMask,
                                                sPspGfxDlBatchProvenance, ctx->batchProvenanceCount);
#endif
    (void) reason;
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_FLUSH);
    PspGfxPspgl_DrawColoredTriangles(sPspGfxDlBatch, ctx->batchCount, ctx->batchTextureId, ctx->batchTextureRef,
                                     ctx->batchTextureEnv, ctx->batchTextureEnvColor, ctx->batchWrapS,
                                     ctx->batchWrapT, ctx->batchAlphaTest, ctx->batchBlend,
                                     ctx->batchPremultiplied, ctx->batchDepthTest,
                                     ctx->batchDepthWrite, ctx->batchFog, ctx->batchFogColor, ctx->batchFogStart,
                                     ctx->batchFogEnd,
                                     &ctx->batchProjection[0][0], ctx->batchPretransformed);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_FLUSH);
    PspProfiler_CountBatchFlush(reason, ctx->batchCount);
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    if (ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectFlush(reason, ctx->batchCount);
    }
#endif
#if SF64_PSP_PROFILE_COMPONENTS
    PspProfiler_CountBatchComponentOwnership(ownerComponent, ownerMask, ctx->batchCount);
    PspProfiler_ComponentScopeEnd();
#endif
    ctx->stats.drawVertexCount += ctx->batchCount;
    ctx->batchCount = 0;
#if SF64_PSP_PROFILE_COMPONENTS
    ctx->batchComponentMask = 0;
#endif
    psp_gfx_dl_reset_vertex_reuse_batch(ctx);
}

static void psp_gfx_dl_flush_texture_change(PspGfxDlContext* ctx, PspProfileTextureFlushSource source) {
    (void) source;
    if (ctx->batchCount != 0) {
        PspProfiler_CountTextureFlushSource(source);
    }
    psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_TEXTURE_CHANGE);
}

static PspGfxPspglTextureRef psp_gfx_dl_null_texture_ref(void) {
    PspGfxPspglTextureRef ref;

    ref.state = NULL;
    ref.texture = 0;
    ref.generation = 0;
    return ref;
}

static int psp_gfx_dl_texture_ref_equal(PspGfxPspglTextureRef a, PspGfxPspglTextureRef b) {
    return (a.state == b.state) && (a.texture == b.texture) && (a.generation == b.generation);
}

static void psp_gfx_dl_set_batch_texture(PspGfxDlContext* ctx, u32 textureId, PspGfxPspglTextureRef textureRef,
                                         PspGfxPspglTextureEnv textureEnv, u32 textureEnvColor,
                                         PspGfxDlCombineMode combineMode, u32 primitiveColor, u32 environmentColor,
                                         PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT, int alphaTest,
                                         int blend, int premultiplied) {
    int textureIdChanged = (ctx->batchTextureId != textureId) ||
                           !psp_gfx_dl_texture_ref_equal(ctx->batchTextureRef, textureRef);
    int textureEnvChanged = ctx->batchTextureEnv != textureEnv;
    int textureEnvColorChanged = ctx->batchTextureEnvColor != textureEnvColor;
    int wrapSChanged = ctx->batchWrapS != wrapS;
    int wrapTChanged = ctx->batchWrapT != wrapT;
    int alphaTestChanged = ctx->batchAlphaTest != alphaTest;
    int blendChanged = ctx->batchBlend != blend;
    int premultipliedChanged = ctx->batchPremultiplied != premultiplied;

    if ((ctx->batchCount != 0) &&
        (textureIdChanged || textureEnvChanged || textureEnvColorChanged || wrapSChanged || wrapTChanged ||
         alphaTestChanged || blendChanged || premultipliedChanged)) {
        PspProfiler_CountBatchStateTransitions(textureIdChanged, textureEnvChanged || textureEnvColorChanged,
                                               wrapSChanged, wrapTChanged,
                                               alphaTestChanged, blendChanged, premultipliedChanged);
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            if (textureIdChanged) {
                PspProfiler_CountTrivialRejectStateTransition(
                    PSP_PROFILE_TRIVIAL_REJECT_STATE_TEXTURE_ID_OR_REF);
            }
            if (textureEnvChanged || textureEnvColorChanged) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_TEXTURE_ENV);
            }
            if (wrapSChanged) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_WRAP_S);
            }
            if (wrapTChanged) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_WRAP_T);
            }
            if (alphaTestChanged) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_ALPHA_TEST);
            }
            if (blendChanged) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_BLEND);
            }
            if (premultipliedChanged) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_PREMULTIPLIED);
            }
        }
#endif
        if (textureIdChanged || textureEnvChanged || textureEnvColorChanged || wrapSChanged || wrapTChanged) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_MATERIAL_KEY);
        } else {
            psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
        }
    }
    ctx->batchTextureId = textureId;
    ctx->batchTextureRef = textureRef;
    ctx->batchTextureEnv = textureEnv;
    ctx->batchTextureEnvColor = textureEnvColor;
    ctx->batchCombineMode = combineMode;
    ctx->batchPrimitiveColor = primitiveColor;
    ctx->batchEnvironmentColor = environmentColor;
    ctx->batchWrapS = wrapS;
    ctx->batchWrapT = wrapT;
    ctx->batchAlphaTest = alphaTest;
    ctx->batchBlend = blend;
    ctx->batchPremultiplied = premultiplied;
}

static void psp_gfx_dl_set_batch_depth(PspGfxDlContext* ctx, int depthTest, int depthWrite) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchDepthTest != depthTest) || (ctx->batchDepthWrite != depthWrite))) {
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            if (ctx->batchDepthTest != depthTest) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_TEST);
            }
            if (ctx->batchDepthWrite != depthWrite) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_WRITE);
            }
        }
#endif
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->batchDepthTest = depthTest;
    ctx->batchDepthWrite = depthWrite;
}

static void psp_gfx_dl_set_batch_fog_resolved(PspGfxDlContext* ctx, int fog, const float color[4], float start,
                                              float end) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchFog != fog) || (ctx->batchFogColor[0] != color[0]) ||
         (ctx->batchFogColor[1] != color[1]) || (ctx->batchFogColor[2] != color[2]) ||
         (ctx->batchFogColor[3] != color[3]) || (ctx->batchFogStart != start) ||
         (ctx->batchFogEnd != end))) {
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            PspProfiler_CountTrivialRejectStateTransition(
                PSP_PROFILE_TRIVIAL_REJECT_STATE_FOG_ENABLE_OR_PARAMETERS);
        }
#endif
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

static void psp_gfx_dl_resolve_fog_values(PspGfxDlContext* ctx, int fog, const float projection[4][4],
                                          float color[4], float* start, float* end) {
    *start = 0.0f;
    *end = 0.0f;
    fog = fog && (ctx->fogMul != 0);
    color[0] = (float) ctx->fogR / 255.0f;
    color[1] = (float) ctx->fogG / 255.0f;
    color[2] = (float) ctx->fogB / 255.0f;
    color[3] = (float) ctx->fogA / 255.0f;
    if (fog) {
        float startNdc = -(float) ctx->fogOffset / (float) ctx->fogMul;
        float endNdc = (255.0f - (float) ctx->fogOffset) / (float) ctx->fogMul;

        *start = psp_gfx_dl_fog_distance(projection, startNdc);
        *end = psp_gfx_dl_fog_distance(projection, endNdc);
        if ((*start < 0.0f) || (*end <= *start)) {
            *start = 0.0f;
            *end = 0.0f;
        }
    }
}

static int psp_gfx_dl_resolve_fog_state_values(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex,
                                               int pretransformed, float color[4], float* start, float* end) {
    int requestedFog = !pretransformed && ((ctx->otherModeL >> 30) == G_BL_CLR_FOG);

    psp_gfx_dl_resolve_fog_values(ctx, requestedFog, vertex->projection, color, start, end);
    return requestedFog && (ctx->fogMul != 0) && (*end > *start) && (*start >= 0.0f);
}

static void psp_gfx_dl_set_batch_fog(PspGfxDlContext* ctx, int fog, const float projection[4][4]) {
    float color[4];
    float start;
    float end;

    psp_gfx_dl_resolve_fog_values(ctx, fog, projection, color, &start, &end);
    fog = fog && (ctx->fogMul != 0) && (end > start) && (start >= 0.0f);
    psp_gfx_dl_set_batch_fog_resolved(ctx, fog, color, start, end);
}

static void psp_gfx_dl_set_batch_transform(PspGfxDlContext* ctx, int pretransformed, u32 projectionSerial,
                                           const float projection[4][4]) {
    if ((ctx->batchCount != 0) &&
        ((ctx->batchPretransformed != pretransformed) ||
         (!pretransformed && (ctx->batchProjectionSerial != projectionSerial)))) {
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            PspProfiler_CountTrivialRejectStateTransition(
                PSP_PROFILE_TRIVIAL_REJECT_STATE_TRANSFORM_OR_PROJECTION);
        }
#endif
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

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred, int premultiply);

#if SF64_PSP_BATCH_STATE_CACHE
static int psp_gfx_dl_resolve_effective_material_state(PspGfxDlContext* ctx) {
    PspGfxDlEffectiveMaterialState* material = &ctx->effectiveMaterial;
    int premultiplied;

    if (material->valid && !material->dirty) {
        return 0;
    }

    premultiplied = psp_gfx_dl_premultiplied_blend_enabled(ctx);
    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, premultiplied);
    }

    material->textureId = ctx->textureEnabled ? ctx->textureId : 0;
    material->textureRef = ctx->textureEnabled ? ctx->textureRef : psp_gfx_dl_null_texture_ref();
    /* Frontend intent (gsSPTexture on/off), not derived from textureId. */
    material->textureEnv = psp_gfx_dl_texture_env_for_combine(ctx);
    material->textureEnvColor = psp_gfx_dl_texture_env_color_for_combine(ctx);
    material->wrapS = psp_gfx_dl_texture_wrap(ctx->textureCms, ctx->textureMaskS);
    material->wrapT = psp_gfx_dl_texture_wrap(ctx->textureCmt, ctx->textureMaskT);
    material->alphaTest = psp_gfx_dl_alpha_test_enabled(ctx);
    material->blend = psp_gfx_dl_blend_enabled(ctx);
    material->premultiplied = premultiplied;
    material->valid = 1;
    material->dirty = 0;
    return 1;
}

static int psp_gfx_dl_resolve_effective_depth_state(PspGfxDlContext* ctx) {
    PspGfxDlEffectiveDepthState* depth = &ctx->effectiveDepth;

    if (depth->valid && !depth->dirty) {
        return 0;
    }
    depth->depthTest = (ctx->geometryMode & G_ZBUFFER) != 0;
    depth->depthWrite = (ctx->otherModeL & Z_UPD) != 0;
    depth->valid = 1;
    depth->dirty = 0;
    return 1;
}

static int psp_gfx_dl_resolve_effective_fog_state(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex,
                                                  int pretransformed) {
    PspGfxDlEffectiveFogState* fog = &ctx->effectiveFog;
    u32 projectionSerial = pretransformed ? 0 : vertex->projectionSerial;

    if (fog->valid && !fog->dirty && (fog->pretransformed == pretransformed) &&
        (fog->projectionSerial == projectionSerial)) {
        return 0;
    }

    fog->fog = psp_gfx_dl_resolve_fog_state_values(ctx, vertex, pretransformed,
                                                   fog->color, &fog->start, &fog->end);
    fog->pretransformed = pretransformed;
    fog->projectionSerial = projectionSerial;
    fog->valid = 1;
    fog->dirty = 0;
    return 1;
}

static void psp_gfx_dl_resolve_effective_state(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex,
                                               int pretransformed, int* materialResolved, int* depthResolved,
                                               int* fogResolved) {
    *materialResolved = psp_gfx_dl_resolve_effective_material_state(ctx);
    *depthResolved = psp_gfx_dl_resolve_effective_depth_state(ctx);
    *fogResolved = psp_gfx_dl_resolve_effective_fog_state(ctx, vertex, pretransformed);
}

static u32 psp_gfx_dl_apply_effective_batch_state(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex,
                                                  int pretransformed) {
    int materialResolved;
    int depthResolved;
    int fogResolved;
    int resolved;

    psp_gfx_dl_resolve_effective_state(ctx, vertex, pretransformed, &materialResolved, &depthResolved, &fogResolved);
    psp_gfx_dl_set_batch_transform(ctx, pretransformed, vertex->projectionSerial, vertex->projection);
    psp_gfx_dl_set_batch_texture(ctx, ctx->effectiveMaterial.textureId, ctx->effectiveMaterial.textureRef,
                                 ctx->effectiveMaterial.textureEnv, ctx->effectiveMaterial.textureEnvColor,
                                 ctx->combineMode, psp_gfx_dl_primitive_color(ctx),
                                 psp_gfx_dl_environment_color(ctx),
                                 ctx->effectiveMaterial.wrapS, ctx->effectiveMaterial.wrapT,
                                 ctx->effectiveMaterial.alphaTest, ctx->effectiveMaterial.blend,
                                 ctx->effectiveMaterial.premultiplied);
    psp_gfx_dl_set_batch_depth(ctx, ctx->effectiveDepth.depthTest, ctx->effectiveDepth.depthWrite);
    psp_gfx_dl_set_batch_fog_resolved(ctx, ctx->effectiveFog.fog, ctx->effectiveFog.color,
                                      ctx->effectiveFog.start, ctx->effectiveFog.end);
    resolved = materialResolved || depthResolved || fogResolved;
    PspProfiler_CountEffectiveState(resolved ? 1 : 0, resolved ? 0 : 1, materialResolved, depthResolved,
                                    fogResolved);
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    if (ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_EFFECTIVE_STATE_CALLS, 1);
        PspProfiler_CountTrivialRejectCost(resolved ? PSP_PROFILE_TRIVIAL_REJECT_COST_EFFECTIVE_STATE_RESOLVES
                                                    : PSP_PROFILE_TRIVIAL_REJECT_COST_EFFECTIVE_STATE_REUSES,
                                           1);
        if (ctx->effectiveMaterial.textureId != 0) {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_TEXTURED);
        } else {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_UNTEXTURED);
        }
        if (ctx->effectiveMaterial.alphaTest) {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_ALPHA_TEST);
        }
        if (ctx->effectiveMaterial.blend) {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_BLEND);
        }
        if (ctx->effectiveDepth.depthTest) {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_DEPTH_TEST);
        }
        if (ctx->effectiveDepth.depthWrite) {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_DEPTH_WRITE);
        }
        if (ctx->effectiveFog.fog) {
            PspProfiler_CountTrivialRejectRenderState(PSP_PROFILE_TRIVIAL_REJECT_RENDER_FOG);
        }
    }
#endif
    (void) resolved;
    return ctx->effectiveMaterial.textureId;
}
#endif

static int psp_gfx_dl_vertex_is_valid(PspGfxDlContext* ctx, u8 index) {
    return (index < PSP_GFX_DL_MAX_VERTICES) && ctx->vertices[index].valid;
}

#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
static PspProfileTriOutcome psp_gfx_dl_classify_triangle_outcome(PspGfxDlContext* ctx, u8 a, u8 b, u8 c) {
    const PspGfxDlVertex* va;
    const PspGfxDlVertex* vb;
    const PspGfxDlVertex* vc;
    u8 sharedClipCode;
    u8 combinedClipCode;

    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        return PSP_PROFILE_TRI_OUTCOME_INVALID;
    }

    va = &ctx->vertices[a];
    vb = &ctx->vertices[b];
    vc = &ctx->vertices[c];
    sharedClipCode = va->clipCode & vb->clipCode & vc->clipCode;
    if (sharedClipCode != 0) {
        return PSP_PROFILE_TRI_OUTCOME_TRIVIAL_REJECT;
    }
    combinedClipCode = va->clipCode | vb->clipCode | vc->clipCode;
    return (combinedClipCode != 0) ? PSP_PROFILE_TRI_OUTCOME_PARTIAL_CLIP : PSP_PROFILE_TRI_OUTCOME_DIRECT;
}

#if !SF64_PSP_EARLY_TRIVIAL_REJECT
static void psp_gfx_dl_trivial_reject_scope_begin(PspGfxDlContext* ctx) {
    if (ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_INVALID_NESTING, 1);
    }
    ctx->trivialRejectDiagnosticActive = 1;
    PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_BEGINS, 1);
}

static void psp_gfx_dl_trivial_reject_scope_end(PspGfxDlContext* ctx) {
    if (!ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_INVALID_NESTING, 1);
        return;
    }
    ctx->trivialRejectDiagnosticActive = 0;
    PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_ENDS, 1);
}
#endif

static void psp_gfx_dl_trivial_reject_scope_clear_for_task(PspGfxDlContext* ctx) {
    if (ctx->trivialRejectDiagnosticActive) {
        ctx->trivialRejectDiagnosticActive = 0;
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_LEAKS, 1);
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_ENDS, 1);
    }
}
#endif

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
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA) {
        dst->r = ((float) src->r * (float) ctx->primitiveR) / (255.0f * 255.0f);
        dst->g = ((float) src->g * (float) ctx->primitiveG) / (255.0f * 255.0f);
        dst->b = ((float) src->b * (float) ctx->primitiveB) / (255.0f * 255.0f);
        dst->a = (float) ctx->primitiveA / 255.0f;
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        dst->r = (float) ctx->environmentR / 255.0f;
        dst->g = (float) ctx->environmentG / 255.0f;
        dst->b = (float) ctx->environmentB / 255.0f;
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
        dst->u = psp_gfx_dl_normalize_s10_5_s(ctx, src->s, ctx->textureUploadWidth, ctx->textureTileUls);
        dst->v = psp_gfx_dl_normalize_s10_5_t(ctx, src->t, ctx->textureUploadHeight, ctx->textureTileUlt);
    } else {
        dst->u = 0.0f;
        dst->v = 0.0f;
    }
    dst->generated = 0;
}

static void psp_gfx_dl_emit_clip_vertex_with_source(PspGfxDlContext* ctx, const PspGfxDlClipVertex* src, u32 source) {
    PspGfxPspglColorVertex* dst;
    float r;
    float g;
    float b;
    float a;

    (void) source;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &sPspGfxDlBatch[ctx->batchCount++];
    psp_gfx_dl_mark_batch_component(ctx);
    psp_gfx_dl_mark_vertex_reuse_source(ctx, psp_gfx_dl_resolve_vertex_reuse_source(src, source));

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

static void psp_gfx_dl_emit_clip_vertex(PspGfxDlContext* ctx, const PspGfxDlClipVertex* src) {
    psp_gfx_dl_emit_clip_vertex_with_source(ctx, src, psp_gfx_dl_clipped_vertex_source(src));
}

static void psp_gfx_dl_count_fog_depth_vertex(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex,
                                              float fogStart, float fogEnd) {
    float fogDepth = -vertex->viewZ;

    if (vertex->viewW != 0.0f) {
        fogDepth /= vertex->viewW;
    }
    if (!ctx->hasFogDepthRange) {
        ctx->hasFogDepthRange = 1;
        ctx->fogRangeStart = fogStart;
        ctx->fogRangeEnd = fogEnd;
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

static void psp_gfx_dl_count_fog_triangle_stats(PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                                const PspGfxDlVertex* b, const PspGfxDlVertex* c,
                                                float fogStart, float fogEnd) {
    ctx->stats.fogTriangleCount++;
    psp_gfx_dl_count_fog_depth_vertex(ctx, a, fogStart, fogEnd);
    psp_gfx_dl_count_fog_depth_vertex(ctx, b, fogStart, fogEnd);
    psp_gfx_dl_count_fog_depth_vertex(ctx, c, fogStart, fogEnd);
}

#if SF64_PSP_DIRECT_TRI_FASTPATH
static void psp_gfx_dl_vertex_color_u8(PspGfxDlContext* ctx, const PspGfxDlVertex* src, u32* r, u32* g, u32* b,
                                       u32* a) {
    if ((ctx->combineMode == PSP_GFX_DL_COMBINE_PRIMITIVE) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA) ||
        ((ctx->geometryMode & G_SHADE) == 0)) {
        *r = ctx->primitiveR;
        *g = ctx->primitiveG;
        *b = ctx->primitiveB;
        *a = ctx->primitiveA;
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA) {
        *r = (src->r * ctx->primitiveR) / 255U;
        *g = (src->g * ctx->primitiveG) / 255U;
        *b = (src->b * ctx->primitiveB) / 255U;
        *a = ctx->primitiveA;
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        *r = ctx->environmentR;
        *g = ctx->environmentG;
        *b = ctx->environmentB;
        *a = ctx->primitiveA;
    } else if ((ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGB) ||
               (ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGBA)) {
        *r = 255;
        *g = 255;
        *b = 255;
        *a = src->a;
    } else {
        *r = src->r;
        *g = src->g;
        *b = src->b;
        *a = src->a;
    }
}

static void psp_gfx_dl_emit_direct_vertex(PspGfxDlContext* ctx, const PspGfxDlVertex* src, float uScale,
                                          float vScale) {
    PspGfxPspglColorVertex* dst;
    u32 r;
    u32 g;
    u32 b;
    u32 a;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &sPspGfxDlBatch[ctx->batchCount++];
    psp_gfx_dl_mark_batch_component(ctx);
    psp_gfx_dl_mark_vertex_reuse_source(ctx, PSP_PROFILE_VERTEX_REUSE_SOURCE_DIRECT);

    if (ctx->batchPretransformed) {
        float inverseW = 1.0f / src->clipW;

        dst->x = src->clipX * inverseW;
        dst->y = src->clipY * inverseW;
        dst->z = src->clipZ * inverseW;
    } else {
        dst->x = src->viewX;
        dst->y = src->viewY;
        dst->z = src->viewZ;
    }

    psp_gfx_dl_vertex_color_u8(ctx, src, &r, &g, &b, &a);
    dst->color = psp_gfx_dl_pack_rgba_u8(r, g, b, a, ctx->batchPremultiplied);
    (void) uScale;
    (void) vScale;

    dst->u = psp_gfx_dl_normalize_s10_5_s(ctx, src->s, ctx->textureUploadWidth, ctx->textureTileUls);
    dst->v = psp_gfx_dl_normalize_s10_5_t(ctx, src->t, ctx->textureUploadHeight, ctx->textureTileUlt);
}

static void psp_gfx_dl_emit_direct_triangle(PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                            const PspGfxDlVertex* b, const PspGfxDlVertex* c) {
    float uScale = 0.0f;
    float vScale = 0.0f;

    if ((ctx->textureUploadWidth != 0) && (ctx->textureUploadHeight != 0)) {
        uScale = 1.0f / (32.0f * (float) ctx->textureUploadWidth);
        vScale = 1.0f / (32.0f * (float) ctx->textureUploadHeight);
    }

    psp_gfx_dl_emit_direct_vertex(ctx, a, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex(ctx, b, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex(ctx, c, uScale, vScale);
}

#if SF64_PSP_TRI2_PAIR_FASTPATH
#define PSP_GFX_DL_TRI2_VALIDATE_U 0x00000001U
#define PSP_GFX_DL_TRI2_VALIDATE_V 0x00000002U
#define PSP_GFX_DL_TRI2_VALIDATE_COLOR 0x00000004U
#define PSP_GFX_DL_TRI2_VALIDATE_X 0x00000008U
#define PSP_GFX_DL_TRI2_VALIDATE_Y 0x00000010U
#define PSP_GFX_DL_TRI2_VALIDATE_Z 0x00000020U
#define PSP_GFX_DL_TRI2_VALIDATE_BATCH_DELTA 0x00000040U

static int psp_gfx_dl_triangle_pretransformed(const PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                              const PspGfxDlVertex* b, const PspGfxDlVertex* c) {
    return !ctx->hasProjection || (a->projectionSerial == 0) ||
           (a->projectionSerial != b->projectionSerial) ||
           (a->projectionSerial != c->projectionSerial);
}

static void psp_gfx_dl_build_direct_vertex(PspGfxDlContext* ctx, const PspGfxDlVertex* src, float uScale,
                                           float vScale, PspGfxPspglColorVertex* dst) {
    u32 r;
    u32 g;
    u32 b;
    u32 a;

    if (ctx->batchPretransformed) {
        float inverseW = 1.0f / src->clipW;

        dst->x = src->clipX * inverseW;
        dst->y = src->clipY * inverseW;
        dst->z = src->clipZ * inverseW;
    } else {
        dst->x = src->viewX;
        dst->y = src->viewY;
        dst->z = src->viewZ;
    }

    psp_gfx_dl_vertex_color_u8(ctx, src, &r, &g, &b, &a);
    dst->color = psp_gfx_dl_pack_rgba_u8(r, g, b, a, ctx->batchPremultiplied);
    (void) uScale;
    (void) vScale;

    dst->u = psp_gfx_dl_normalize_s10_5_s(ctx, src->s, ctx->textureUploadWidth, ctx->textureTileUls);
    dst->v = psp_gfx_dl_normalize_s10_5_t(ctx, src->t, ctx->textureUploadHeight, ctx->textureTileUlt);
}

static void psp_gfx_dl_emit_direct_vertex_unchecked(PspGfxDlContext* ctx, const PspGfxDlVertex* src,
                                                    float uScale, float vScale) {
    PspGfxPspglColorVertex* dst = &sPspGfxDlBatch[ctx->batchCount++];

    psp_gfx_dl_mark_batch_component(ctx);
    psp_gfx_dl_mark_vertex_reuse_source(ctx, PSP_PROFILE_VERTEX_REUSE_SOURCE_DIRECT);
    psp_gfx_dl_build_direct_vertex(ctx, src, uScale, vScale, dst);
}

static void psp_gfx_dl_count_tri2_pair_triangle_stats(PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                                      const PspGfxDlVertex* b, const PspGfxDlVertex* c) {
    float area;

    if ((a->clipW < 0.0f) && (b->clipW < 0.0f) && (c->clipW < 0.0f)) {
        ctx->stats.behindEyeTriangleCount++;
    } else if ((a->clipW < 0.0f) || (b->clipW < 0.0f) || (c->clipW < 0.0f)) {
        ctx->stats.eyePlaneCrossingTriangleCount++;
    }
    area = ((b->x - a->x) * (c->y - a->y)) - ((b->y - a->y) * (c->x - a->x));
    if ((area > -0.000001f) && (area < 0.000001f)) {
        ctx->stats.degenerateTriangleCount++;
    }
}

static void psp_gfx_dl_count_tri2_pair_fog_stats(PspGfxDlContext* ctx, const PspGfxDlVertex* const vertices[6]) {
    u32 i;

    ctx->stats.fogTriangleCount += 2;
    for (i = 0; i < 6; i++) {
        psp_gfx_dl_count_fog_depth_vertex(ctx, vertices[i], ctx->batchFogStart, ctx->batchFogEnd);
    }
}

#if SF64_PSP_TRI2_PAIR_VALIDATE
static u32 psp_gfx_dl_compare_direct_vertex(const PspGfxPspglColorVertex* expected,
                                            const PspGfxPspglColorVertex* actual) {
    u32 mask = 0;

    if (expected->u != actual->u) {
        mask |= PSP_GFX_DL_TRI2_VALIDATE_U;
    }
    if (expected->v != actual->v) {
        mask |= PSP_GFX_DL_TRI2_VALIDATE_V;
    }
    if (expected->color != actual->color) {
        mask |= PSP_GFX_DL_TRI2_VALIDATE_COLOR;
    }
    if (expected->x != actual->x) {
        mask |= PSP_GFX_DL_TRI2_VALIDATE_X;
    }
    if (expected->y != actual->y) {
        mask |= PSP_GFX_DL_TRI2_VALIDATE_Y;
    }
    if (expected->z != actual->z) {
        mask |= PSP_GFX_DL_TRI2_VALIDATE_Z;
    }
    return mask;
}
#endif

static int psp_gfx_dl_try_emit_tri2_direct_pair(PspGfxDlContext* ctx, u8 a0, u8 b0, u8 c0,
                                                u8 a1, u8 b1, u8 c1) {
    const PspGfxDlVertex* va0;
    const PspGfxDlVertex* vb0;
    const PspGfxDlVertex* vc0;
    const PspGfxDlVertex* va1;
    const PspGfxDlVertex* vb1;
    const PspGfxDlVertex* vc1;
    const PspGfxDlVertex* vertices[6];
    u8 combined0;
    u8 combined1;
    int pretransformed0;
    int pretransformed1;
    u32 textureId;
    float uScale = 0.0f;
    float vScale = 0.0f;
    u32 bufferPreflush = 0;
#if SF64_PSP_TRI2_PAIR_VALIDATE
    PspGfxPspglColorVertex reference[6];
    u32 beforeCount;
    u32 mismatch = 0;
    u32 i;
#endif

    if (!psp_gfx_dl_vertex_is_valid(ctx, a0) || !psp_gfx_dl_vertex_is_valid(ctx, b0) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c0) || !psp_gfx_dl_vertex_is_valid(ctx, a1) ||
        !psp_gfx_dl_vertex_is_valid(ctx, b1) || !psp_gfx_dl_vertex_is_valid(ctx, c1)) {
        PspProfiler_CountTri2PairFastpath(0, 1, 0, 0, 0, 0, 0);
        return 0;
    }

    va0 = &ctx->vertices[a0];
    vb0 = &ctx->vertices[b0];
    vc0 = &ctx->vertices[c0];
    va1 = &ctx->vertices[a1];
    vb1 = &ctx->vertices[b1];
    vc1 = &ctx->vertices[c1];
    combined0 = va0->clipCode | vb0->clipCode | vc0->clipCode;
    combined1 = va1->clipCode | vb1->clipCode | vc1->clipCode;
    if ((combined0 != 0) || (combined1 != 0)) {
        PspProfiler_CountTri2PairFastpath(0, 0, 1, 0, 0, 0, 0);
        return 0;
    }

    pretransformed0 = psp_gfx_dl_triangle_pretransformed(ctx, va0, vb0, vc0);
    pretransformed1 = psp_gfx_dl_triangle_pretransformed(ctx, va1, vb1, vc1);
    if (pretransformed0 != pretransformed1) {
        PspProfiler_CountTri2PairFastpath(0, 0, 0, 1, 0, 0, 0);
        return 0;
    }
    if (!pretransformed0) {
        if ((va0->projectionSerial == 0) || (vb0->projectionSerial == 0) || (vc0->projectionSerial == 0) ||
            (va1->projectionSerial == 0) || (vb1->projectionSerial == 0) || (vc1->projectionSerial == 0) ||
            (va0->projectionSerial != va1->projectionSerial)) {
            PspProfiler_CountTri2PairFastpath(0, 0, 0, 1, 0, 0, 0);
            return 0;
        }
    }
    if (ctx->textureEnabled && pretransformed0) {
        PspProfiler_CountTri2PairFastpath(0, 0, 0, 0, 1, 0, 0);
        return 0;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
    if (pretransformed0) {
        ctx->stats.pretransformedTriangleCount += 2;
    } else {
        ctx->stats.projectedTriangleCount += 2;
    }
    psp_gfx_dl_count_tri2_pair_triangle_stats(ctx, va0, vb0, vc0);
    psp_gfx_dl_count_tri2_pair_triangle_stats(ctx, va1, vb1, vc1);

#if SF64_PSP_BATCH_STATE_CACHE
    textureId = psp_gfx_dl_apply_effective_batch_state(ctx, va0, pretransformed0);
#else
    psp_gfx_dl_set_batch_transform(ctx, pretransformed0, va0->projectionSerial, va0->projection);
    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    textureId = ctx->textureEnabled ? ctx->textureId : 0;
    psp_gfx_dl_set_batch_texture(ctx, textureId, ctx->textureEnabled ? ctx->textureRef : psp_gfx_dl_null_texture_ref(),
                                 psp_gfx_dl_texture_env_for_combine(ctx),
                                 psp_gfx_dl_texture_env_color_for_combine(ctx), ctx->combineMode,
                                 psp_gfx_dl_primitive_color(ctx), psp_gfx_dl_environment_color(ctx),
                                 psp_gfx_dl_texture_wrap(ctx->textureCms, ctx->textureMaskS),
                                 psp_gfx_dl_texture_wrap(ctx->textureCmt, ctx->textureMaskT),
                                 psp_gfx_dl_alpha_test_enabled(ctx), psp_gfx_dl_blend_enabled(ctx),
                                 psp_gfx_dl_premultiplied_blend_enabled(ctx));
    psp_gfx_dl_set_batch_depth(ctx, (ctx->geometryMode & G_ZBUFFER) != 0, (ctx->otherModeL & Z_UPD) != 0);
    psp_gfx_dl_set_batch_fog(ctx, !pretransformed0 && ((ctx->otherModeL >> 30) == G_BL_CLR_FOG), va0->projection);
#endif

    if (ctx->batchDepthTest) {
        ctx->stats.depthTestTriangleCount += 2;
    }
    if (ctx->batchDepthWrite) {
        ctx->stats.depthWriteTriangleCount += 2;
    }
    vertices[0] = va0;
    vertices[1] = vb0;
    vertices[2] = vc0;
    vertices[3] = va1;
    vertices[4] = vb1;
    vertices[5] = vc1;
    if (ctx->batchFog) {
        psp_gfx_dl_count_tri2_pair_fog_stats(ctx, vertices);
    }
    if (ctx->batchCount + 6 > PSP_GFX_DL_BATCH_VERTICES) {
        bufferPreflush = 1;
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }
    if ((ctx->textureUploadWidth != 0) && (ctx->textureUploadHeight != 0)) {
        uScale = 1.0f / (32.0f * (float) ctx->textureUploadWidth);
        vScale = 1.0f / (32.0f * (float) ctx->textureUploadHeight);
    }

#if SF64_PSP_TRI2_PAIR_VALIDATE
    beforeCount = ctx->batchCount;
    for (i = 0; i < 6; i++) {
        psp_gfx_dl_build_direct_vertex(ctx, vertices[i], uScale, vScale, &reference[i]);
    }
#endif
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, va0, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, vb0, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, vc0, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, va1, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, vb1, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, vc1, uScale, vScale);
#if SF64_PSP_TRI2_PAIR_VALIDATE
    if ((ctx->batchCount - beforeCount) != 6) {
        mismatch |= PSP_GFX_DL_TRI2_VALIDATE_BATCH_DELTA;
    }
    for (i = 0; i < 6; i++) {
        u32 vertexMask = psp_gfx_dl_compare_direct_vertex(&reference[i], &sPspGfxDlBatch[beforeCount + i]);

        if ((vertexMask != 0) && (mismatch == 0)) {
            PspProfiler_RecordTri2PairValidationMismatch(i, vertexMask,
                                                         ctx->batchCount - beforeCount);
        }
        mismatch |= vertexMask;
    }
    if (mismatch & PSP_GFX_DL_TRI2_VALIDATE_BATCH_DELTA) {
        PspProfiler_RecordTri2PairValidationMismatch(0, PSP_GFX_DL_TRI2_VALIDATE_BATCH_DELTA,
                                                     ctx->batchCount - beforeCount);
    }
#endif
    PspProfiler_CountTriangleResult(2, 0, 0, 0, 2);
    PspProfiler_CountTrianglePath(2, 0, 0, 0, 6);
    PspProfiler_CountTri2PairFastpath(1, 0, 0, 0, 0, bufferPreflush,
#if SF64_PSP_TRI2_PAIR_VALIDATE
                                      mismatch != 0
#else
                                      0
#endif
    );
    (void) bufferPreflush;
    ctx->stats.triangleCount += 2;
    if (textureId != 0) {
        ctx->stats.texturedTriangleCount += 2;
        if (ctx->batchAlphaTest) {
            ctx->stats.alphaTestTriangleCount += 2;
        }
        if (ctx->batchBlend) {
            ctx->stats.blendTriangleCount += 2;
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
    return 1;
}
#endif
#endif

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
                                                u32 depth, u32 source) {
    const PspGfxDlClipVertex* low = a;
    const PspGfxDlClipVertex* high = a;
    PspGfxDlClipVertex midpoint;

    if ((depth >= PSP_GFX_DL_PERSPECTIVE_MAX_DEPTH) ||
        (psp_gfx_dl_triangle_w_ratio(a, b, c) <= PSP_GFX_DL_PERSPECTIVE_W_RATIO)) {
        psp_gfx_dl_emit_clip_vertex_with_source(ctx, a, source);
        psp_gfx_dl_emit_clip_vertex_with_source(ctx, b, source);
        psp_gfx_dl_emit_clip_vertex_with_source(ctx, c, source);
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
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1, source) +
               psp_gfx_dl_emit_perspective_triangle(ctx, &midpoint, b, c, depth + 1, source);
    }
    if ((low == b) && (high == a)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1, source) +
               psp_gfx_dl_emit_perspective_triangle(ctx, &midpoint, b, c, depth + 1, source);
    }
    if ((low == b) && (high == c)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, &midpoint, depth + 1, source) +
               psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1, source);
    }
    if ((low == c) && (high == b)) {
        return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, &midpoint, depth + 1, source) +
               psp_gfx_dl_emit_perspective_triangle(ctx, a, &midpoint, c, depth + 1, source);
    }
    return psp_gfx_dl_emit_perspective_triangle(ctx, a, b, &midpoint, depth + 1, source) +
           psp_gfx_dl_emit_perspective_triangle(ctx, &midpoint, b, c, depth + 1, source);
}

static u32 psp_gfx_dl_emit_textured_triangle(PspGfxDlContext* ctx, const PspGfxDlClipVertex* a,
                                             const PspGfxDlClipVertex* b, const PspGfxDlClipVertex* c, u32 source) {
    if (ctx->batchPretransformed) {
        u32 emitted;

        emitted = psp_gfx_dl_emit_perspective_triangle(ctx, a, b, c, 0, source);
        return emitted;
    }
    psp_gfx_dl_emit_clip_vertex_with_source(ctx, a, source);
    psp_gfx_dl_emit_clip_vertex_with_source(ctx, b, source);
    psp_gfx_dl_emit_clip_vertex_with_source(ctx, c, source);
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
            psp_gfx_dl_emit_textured_triangle(ctx, &input[0], &input[i], &input[i + 1],
                                              PSP_PROFILE_VERTEX_REUSE_SOURCE_CLIPPED_ORIGINAL);
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
    u32 textureId = 0;
#if SF64_PSP_EARLY_TRIVIAL_REJECT || !SF64_PSP_BATCH_STATE_CACHE
    int depthTest;
    int depthWrite;
#endif
#if SF64_PSP_EARLY_TRIVIAL_REJECT
    int fogEnabled;
    float fogColor[4];
    float fogStart;
    float fogEnd;
#endif
#if !SF64_PSP_BATCH_STATE_CACHE
    PspGfxPspglTextureEnv textureEnv = PSP_GFX_PSPGL_TEX_REPLACE;
#endif
    int pretransformed;

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        ctx->stats.invalidTriangleCount++;
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
        return;
    }

#if !SF64_PSP_BATCH_STATE_CACHE
#if !SF64_PSP_EARLY_TRIVIAL_REJECT
    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    textureId = ctx->textureEnabled ? ctx->textureId : 0;
#endif
#endif

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
#if !SF64_PSP_BATCH_STATE_CACHE
#if !SF64_PSP_EARLY_TRIVIAL_REJECT
    psp_gfx_dl_set_batch_transform(ctx, pretransformed, va->projectionSerial, va->projection);
#endif
#endif

    sharedClipCode = va->clipCode & vb->clipCode & vc->clipCode;
    combinedClipCode = va->clipCode | vb->clipCode | vc->clipCode;
#if SF64_PSP_EARLY_TRIVIAL_REJECT || !SF64_PSP_BATCH_STATE_CACHE
    depthTest = (ctx->geometryMode & G_ZBUFFER) != 0;
    depthWrite = (ctx->otherModeL & Z_UPD) != 0;
#endif
#if SF64_PSP_EARLY_TRIVIAL_REJECT
    fogEnabled = psp_gfx_dl_resolve_fog_state_values(ctx, va, pretransformed, fogColor, &fogStart, &fogEnd);
#endif
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

#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    if (sharedClipCode != 0) {
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TRIANGLES, 1);
        if (ctx->batchCount == 0) {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_EMPTY_BEFORE_STATE, 1);
        } else {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_NONEMPTY_BEFORE_STATE, 1);
        }
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_VERTICES_BEFORE_STATE,
                                           ctx->batchCount);
#if SF64_PSP_EARLY_TRIVIAL_REJECT
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_EARLY_REJECT_TAKEN, 1);
#else
        psp_gfx_dl_trivial_reject_scope_begin(ctx);
#endif
    }
#endif
#if SF64_PSP_EARLY_TRIVIAL_REJECT
    if (sharedClipCode != 0) {
        if (depthTest) {
            ctx->stats.depthTestTriangleCount++;
        }
        if (depthWrite) {
            ctx->stats.depthWriteTriangleCount++;
        }
        if (fogEnabled) {
            psp_gfx_dl_count_fog_triangle_stats(ctx, va, vb, vc, fogStart, fogEnd);
        }
        ctx->stats.clipRejectedTriangleCount++;
        ctx->stats.triangleCount++;
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
        return;
    }
#endif
#if SF64_PSP_BATCH_STATE_CACHE
    textureId = psp_gfx_dl_apply_effective_batch_state(ctx, va, pretransformed);
#else
#if SF64_PSP_EARLY_TRIVIAL_REJECT
    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    textureId = ctx->textureEnabled ? ctx->textureId : 0;
    psp_gfx_dl_set_batch_transform(ctx, pretransformed, va->projectionSerial, va->projection);
#endif
    textureEnv = psp_gfx_dl_texture_env_for_combine(ctx);
    psp_gfx_dl_set_batch_texture(ctx, textureId, ctx->textureEnabled ? ctx->textureRef : psp_gfx_dl_null_texture_ref(),
                                 textureEnv, psp_gfx_dl_texture_env_color_for_combine(ctx), ctx->combineMode,
                                 psp_gfx_dl_primitive_color(ctx), psp_gfx_dl_environment_color(ctx),
                                 psp_gfx_dl_texture_wrap(ctx->textureCms, ctx->textureMaskS),
                                 psp_gfx_dl_texture_wrap(ctx->textureCmt, ctx->textureMaskT),
                                 psp_gfx_dl_alpha_test_enabled(ctx), psp_gfx_dl_blend_enabled(ctx),
                                 psp_gfx_dl_premultiplied_blend_enabled(ctx));
    psp_gfx_dl_set_batch_depth(ctx, depthTest, depthWrite);
    psp_gfx_dl_set_batch_fog(ctx, !pretransformed && ((ctx->otherModeL >> 30) == G_BL_CLR_FOG), va->projection);
#endif
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
#if !SF64_PSP_EARLY_TRIVIAL_REJECT
    if (sharedClipCode != 0) {
        psp_gfx_dl_trivial_reject_scope_end(ctx);
    }
#endif
#endif
    if (ctx->batchDepthTest) {
        ctx->stats.depthTestTriangleCount++;
    }
    if (ctx->batchDepthWrite) {
        ctx->stats.depthWriteTriangleCount++;
    }
    if (ctx->batchFog) {
        psp_gfx_dl_count_fog_triangle_stats(ctx, va, vb, vc, ctx->batchFogStart, ctx->batchFogEnd);
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
        PspProfiler_CountTrianglePath(0, 0, 0, 1, 0);
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
#if SF64_PSP_DIRECT_TRI_FASTPATH
        if ((textureId == 0) || !pretransformed) {
            psp_gfx_dl_emit_direct_triangle(ctx, va, vb, vc);
            emittedTriangles = 1;
            PspProfiler_CountTrianglePath(1, 0, 0, 0, 3);
        } else
#endif
        {
            PspGfxDlClipVertex vertices[3];

            psp_gfx_dl_build_clip_vertex(ctx, va, &vertices[0]);
            psp_gfx_dl_build_clip_vertex(ctx, vb, &vertices[1]);
            psp_gfx_dl_build_clip_vertex(ctx, vc, &vertices[2]);
            if (textureId != 0) {
                psp_gfx_dl_emit_textured_triangle(ctx, &vertices[0], &vertices[1], &vertices[2],
                                                  PSP_PROFILE_VERTEX_REUSE_SOURCE_GENERIC_UNCLIPPED);
            } else {
                psp_gfx_dl_emit_clip_vertex_with_source(ctx, &vertices[0],
                                                        PSP_PROFILE_VERTEX_REUSE_SOURCE_GENERIC_UNCLIPPED);
                psp_gfx_dl_emit_clip_vertex_with_source(ctx, &vertices[1],
                                                        PSP_PROFILE_VERTEX_REUSE_SOURCE_GENERIC_UNCLIPPED);
                psp_gfx_dl_emit_clip_vertex_with_source(ctx, &vertices[2],
                                                        PSP_PROFILE_VERTEX_REUSE_SOURCE_GENERIC_UNCLIPPED);
            }
            emittedTriangles = 1;
            PspProfiler_CountTrianglePath(0, 1, ((textureId != 0) && pretransformed) ? 1 : 0, 0, 0);
        }
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
    psp_gfx_dl_mark_batch_component(ctx);
    psp_gfx_dl_mark_vertex_reuse_source(ctx, PSP_PROFILE_VERTEX_REUSE_SOURCE_RECTANGLE);

    dst->u = psp_gfx_dl_normalize_texel_coord(ctx, u, ctx->textureUploadWidth, ctx->textureTileUls);
    dst->v = psp_gfx_dl_normalize_texel_coord(ctx, v, ctx->textureUploadHeight, ctx->textureTileUlt);

    r = ctx->primitiveR;
    g = ctx->primitiveG;
    b = ctx->primitiveB;
    a = ctx->primitiveA;

    dst->color = psp_gfx_dl_pack_rgba_u8(r, g, b, a, ctx->batchPremultiplied);

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
        ctx, ctx->textureId, ctx->textureRef, PSP_GFX_PSPGL_TEX_MODULATE,
        0, ctx->combineMode, psp_gfx_dl_primitive_color(ctx), psp_gfx_dl_environment_color(ctx),
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
    /* Stored pre-transformed (RGB only) so every consumer -- vertex colour
     * builders, texenv constant, baked env-blend inputs -- sees the transfer
     * exactly once. Transform before the dirty compare below. */
    u8 r = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 24));
    u8 g = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 16));
    u8 b = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 8));
    u8 a = (u8) gfx->words.w1;

    if ((ctx->primitiveR != r) || (ctx->primitiveG != g) || (ctx->primitiveB != b) || (ctx->primitiveA != a)) {
        psp_gfx_dl_mark_effective_material_dirty(ctx);
        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            ctx->textureId = 0;
            ctx->textureUploadAttempted = 0;
        }
    }
    ctx->primitiveR = r;
    ctx->primitiveG = g;
    ctx->primitiveB = b;
    ctx->primitiveA = a;
}

static void psp_gfx_dl_handle_set_environment_color(PspGfxDlContext* ctx, const Gfx* gfx) {
    u8 r = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 24));
    u8 g = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 16));
    u8 b = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 8));
    u8 a = (u8) gfx->words.w1;

    if ((ctx->environmentR != r) || (ctx->environmentG != g) || (ctx->environmentB != b) ||
        (ctx->environmentA != a)) {
        psp_gfx_dl_mark_effective_material_dirty(ctx);
        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            ctx->textureId = 0;
            ctx->textureUploadAttempted = 0;
        }
    }
    ctx->environmentR = r;
    ctx->environmentG = g;
    ctx->environmentB = b;
    ctx->environmentA = a;
}

static void psp_gfx_dl_handle_set_fill_color(PspGfxDlContext* ctx, const Gfx* gfx) {
    u16 color = (u16) (gfx->words.w1 >> 16);

    ctx->fillColor = psp_gfx_dl_rgba5551_to_rgba8888(color);
}

static int psp_gfx_dl_is_fill_cycle(const PspGfxDlContext* ctx) {
    return (ctx->otherModeH & G_CYC_FILL) == G_CYC_FILL;
}

static int psp_gfx_dl_fill_uses_primitive_color(const PspGfxDlContext* ctx) {
    return !psp_gfx_dl_is_fill_cycle(ctx) &&
           (ctx->combineMode == PSP_GFX_DL_COMBINE_PRIMITIVE);
}

static int psp_gfx_dl_is_active_background_rect(u32 ulx, u32 uly, u32 lrx, u32 lry) {
    return (ulx == SCREEN_MARGIN) && (uly == SCREEN_MARGIN) &&
           (lrx == (SCREEN_WIDTH - SCREEN_MARGIN)) &&
           (lry == (SCREEN_HEIGHT - SCREEN_MARGIN + 1U));
}

#if PSP_RENDERER_DIAGNOSTICS
static void psp_gfx_dl_log_active_background_fill(const PspGfxDlContext* ctx, u32 color, int primitiveFill, int blend) {
    char line[192];

    snprintf(line, sizeof(line),
             "[pspgl-dl] active-bg-fill prim=%d blend=%d cycleFill=%d color=%08lx prim=%02x%02x%02x%02x "
             "fill=%08lx seed=%08lx otherL=%08lx otherH=%08lx",
             primitiveFill, blend, psp_gfx_dl_is_fill_cycle(ctx), (unsigned long) color,
             ctx->primitiveR, ctx->primitiveG, ctx->primitiveB, ctx->primitiveA,
             (unsigned long) ctx->fillColor, (unsigned long) sPspGfxDlBackgroundFeedbackSeedColor,
             (unsigned long) ctx->otherModeL, (unsigned long) ctx->otherModeH);
    PspPlatform_LogLine(line);
}
#endif

static void psp_gfx_dl_handle_fill_rectangle(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 w0 = gfx->words.w0;
    u32 w1 = gfx->words.w1;
    u32 lrxInt = ((w0 >> 14) & 0x3FF) + 1U;
    u32 lryInt = ((w0 >> 2) & 0x3FF) + 1U;
    u32 ulxInt = (w1 >> 14) & 0x3FF;
    u32 ulyInt = (w1 >> 2) & 0x3FF;
    float lrx = (float) lrxInt;
    float lry = (float) lryInt;
    float ulx = (float) ulxInt;
    float uly = (float) ulyInt;
    u32 color = ctx->fillColor;
    int blend = 0;
    int primitiveFill = psp_gfx_dl_fill_uses_primitive_color(ctx);
    int activeBackgroundRect = psp_gfx_dl_is_active_background_rect(ulxInt, ulyInt, lrxInt, lryInt);

    if (lrxInt <= ulxInt || lryInt <= ulyInt) {
        return;
    }

    if (primitiveFill) {
        color = psp_gfx_dl_pack_rgba_u8(ctx->primitiveR, ctx->primitiveG, ctx->primitiveB,
                                        ctx->primitiveA, 0);
        blend = ((ctx->otherModeL & FORCE_BL) != 0) && (ctx->primitiveA != 255);
        ctx->stats.fillRectanglePrimitiveColorCount++;
    } else if (!psp_gfx_dl_is_fill_cycle(ctx)) {
        ctx->stats.fillRectangleUnsupportedCount++;
    }

    if (!ctx->colorImageIsDisplay) {
#if PSP_RENDERER_DIAGNOSTICS
        if (activeBackgroundRect) {
            psp_gfx_dl_log_active_background_fill(ctx, color, primitiveFill, blend);
        }
#endif
        ctx->stats.fillRectangleCount++;
        return;
    }

    psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
#if PSP_RENDERER_DIAGNOSTICS
    if (activeBackgroundRect) {
        psp_gfx_dl_log_active_background_fill(ctx, color, primitiveFill, blend);
    }
#endif
    if (primitiveFill && blend && activeBackgroundRect && !sPspGfxDlBackgroundFeedbackPrimed) {
        PspGfxPspgl_DrawSolidRect(ulx, uly, lrx, lry, sPspGfxDlBackgroundFeedbackSeedColor, 0);
        sPspGfxDlBackgroundFeedbackPrimed = 1;
    }
    PspGfxPspgl_DrawSolidRect(ulx, uly, lrx, lry, color, blend);
    if (psp_gfx_dl_is_fill_cycle(ctx) && activeBackgroundRect) {
        sPspGfxDlBackgroundFeedbackSeedColor = ctx->fillColor | 0xFF000000u;
        sPspGfxDlBackgroundFeedbackPrimed = 0;
    }
    ctx->stats.fillRectangleCount++;
}

static void psp_gfx_dl_handle_set_fog_color(PspGfxDlContext* ctx, const Gfx* gfx) {
    if (ctx->batchCount != 0) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->fogR = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 24));
    ctx->fogG = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 16));
    ctx->fogB = psp_gfx_color_transfer_u8((u8) (gfx->words.w1 >> 8));
    ctx->fogA = (u8) gfx->words.w1;
    psp_gfx_dl_mark_effective_fog_dirty(ctx);
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

static int psp_gfx_dl_combine_cycle1_matches(u32 mux0, u32 mux1, u32 a, u32 b, u32 c, u32 d, u32 aa, u32 ab,
                                             u32 ac, u32 ad) {
    u32 ca = (mux0 >> 5) & 0xF;
    u32 cc = mux0 & 0x1F;
    u32 cb = (mux1 >> 24) & 0xF;
    u32 cd = (mux1 >> 6) & 0x7;
    u32 caa = (mux1 >> 21) & 0x7;
    u32 cac = (mux1 >> 18) & 0x7;
    u32 cab = (mux1 >> 3) & 0x7;
    u32 cad = mux1 & 0x7;

    return (ca == (a & 0xF)) && (cb == (b & 0xF)) && (cc == (c & 0x1F)) && (cd == (d & 0x7)) &&
           (caa == (aa & 0x7)) && (cab == (ab & 0x7)) && (cac == (ac & 0x7)) && (cad == (ad & 0x7));
}

static void psp_gfx_dl_handle_set_combine(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 mux0 = gfx->words.w0 & 0x00FFFFFF;
    u32 mux1 = gfx->words.w1;
    PspGfxDlCombineMode oldCombineMode = ctx->combineMode;

    if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                          G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0) &&
        psp_gfx_dl_combine_cycle1_matches(mux0, mux1, G_CCMUX_COMBINED, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                          G_CCMUX_0, G_ACMUX_COMBINED, G_ACMUX_0, G_ACMUX_PRIMITIVE,
                                          G_ACMUX_0)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA;
        ctx->combineUsesTextureAlpha = 1;
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_SHADE,
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
    } else if (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_PRIMITIVE, G_CCMUX_ENVIRONMENT,
                                                 G_CCMUX_TEXEL0, G_CCMUX_ENVIRONMENT, G_ACMUX_TEXEL0,
                                                 G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0) &&
               psp_gfx_dl_combine_cycle1_matches(mux0, mux1, G_CCMUX_PRIMITIVE, G_CCMUX_ENVIRONMENT,
                                                 G_CCMUX_TEXEL0, G_CCMUX_ENVIRONMENT, G_ACMUX_TEXEL0,
                                                 G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0)) {
        ctx->combineMode = PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND;
        ctx->combineUsesTextureAlpha = 1;
    } else {
        ctx->combineMode = PSP_GFX_DL_COMBINE_UNKNOWN;
        ctx->combineUsesTextureAlpha = 1;
    }
    if ((oldCombineMode != ctx->combineMode) && psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
        ctx->textureId = 0;
        ctx->textureUploadAttempted = 0;
    }
    psp_gfx_dl_mark_effective_material_dirty(ctx);
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

#if PSP_RENDERER_DIAGNOSTICS
    ctx->vtxCommandCount++;
    ctx->vtxBatchSizeHistogram[count]++;
    ctx->vtxLightCountHistogram[ctx->lightCount <= 7 ? ctx->lightCount : 7]++;
    if ((ctx->geometryMode & G_LIGHTING) != 0) {
        ctx->litVertexCount += count;
    } else {
        ctx->unlitVertexCount += count;
    }
#endif

#if N64PSP_VERTEX_BATCH_DIAGNOSTICS
    ctx->vtxDiagCommands++;
    ctx->vtxDiagVertices += count;
    if ((ctx->vtxDiagMinBatch == 0) || (count < ctx->vtxDiagMinBatch)) {
        ctx->vtxDiagMinBatch = count;
    }
    if (count > ctx->vtxDiagMaxBatch) {
        ctx->vtxDiagMaxBatch = count;
    }
    if (count <= 4) {
        ctx->vtxDiagBuckets[0]++;
    } else if (count <= 8) {
        ctx->vtxDiagBuckets[1]++;
    } else if (count <= 16) {
        ctx->vtxDiagBuckets[2]++;
    } else if (count <= 24) {
        ctx->vtxDiagBuckets[3]++;
    } else if (count <= 32) {
        ctx->vtxDiagBuckets[4]++;
    } else {
        ctx->vtxDiagBuckets[5]++;
    }
#endif

#if (USE_N64PSP_VERTEX_CHAIN2 + 0)
    {
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

        psp_gfx_dl_prepare_batch_matrices(ctx);
    }

    {
        n64psp_mat4f_transform_vec4_chain2_batch(
            sPspGfxDlTransformOutput,
            &ctx->alignedModelview,
            &ctx->alignedProjection,
            sPspGfxDlTransformInput,
            count
        );
    }
#else
    for (i = 0; i < count; i++) {
        psp_gfx_dl_transform_position_pair(
            ctx,
            &src[i],
            &sPspGfxDlScalarTransformOutput[i]
        );
    }
#endif

    for (i = 0; i < count; i++) {
        PspGfxDlVertex* out = &ctx->vertices[v0 + i];

#if (USE_N64PSP_VERTEX_CHAIN2 + 0)
        PspGfxDlVec4 view;
        PspGfxDlVec4 clip;

#if (N64PSP_VERTEX_CHAIN2_VALIDATE + 0)
        const Vtx* in = &src[i];

        psp_gfx_dl_validate_position_pair(
            ctx,
            gfx,
            in,
            i,
            &sPspGfxDlTransformOutput[i]
        );
#endif

        view.x = sPspGfxDlTransformOutput[i].first.x;
        view.y = sPspGfxDlTransformOutput[i].first.y;
        view.z = sPspGfxDlTransformOutput[i].first.z;
        view.w = sPspGfxDlTransformOutput[i].first.w;

        clip.x = sPspGfxDlTransformOutput[i].second.x;
        clip.y = sPspGfxDlTransformOutput[i].second.y;
        clip.z = sPspGfxDlTransformOutput[i].second.z;
        clip.w = sPspGfxDlTransformOutput[i].second.w;

        out->valid =
            psp_gfx_dl_store_transformed_vertex(
                ctx,
                out,
                &view,
                &clip
            );
#else
        out->valid =
            psp_gfx_dl_store_transformed_vertex(
                ctx,
                out,
                &sPspGfxDlScalarTransformOutput[i].view,
                &sPspGfxDlScalarTransformOutput[i].clip
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
    }

#if (USE_N64PSP_BATCH_LIGHTING + 0)
    if ((ctx->geometryMode & G_LIGHTING) != 0) {
        psp_gfx_dl_stage_lighting_batch(ctx, src, count);
        psp_gfx_dl_prepare_batch_matrices(ctx);
        n64psp_directional_light_snorm8_batch(
            sPspGfxDlLightingOutput,
            &ctx->alignedModelview,
            sPspGfxDlLightingNormals,
            ctx->lightCount != 0 ? sPspGfxDlLightingLights : NULL,
            &sPspGfxDlLightingAmbient,
            ctx->lightCount,
            count
        );
    }
#endif

    for (i = 0; i < count; i++) {
        const Vtx* in = &src[i];
        PspGfxDlVertex* out = &ctx->vertices[v0 + i];

        if ((ctx->geometryMode & G_LIGHTING) != 0) {
            float r;
            float g;
            float b;

#if (USE_N64PSP_BATCH_LIGHTING + 0)
            r = sPspGfxDlLightingOutput[i].x;
            g = sPspGfxDlLightingOutput[i].y;
            b = sPspGfxDlLightingOutput[i].z;
#if (PSP_VALIDATE_N64PSP_BATCH_LIGHTING + 0)
            if (!psp_gfx_dl_validate_lighting_batch(
                    ctx,
                    in,
                    i,
                    &sPspGfxDlLightingOutput[i])) {
                r = sPspGfxDlLightingReference[i].x;
                g = sPspGfxDlLightingReference[i].y;
                b = sPspGfxDlLightingReference[i].z;
            }
#endif
#else
            psp_gfx_dl_light_vertex_scalar(ctx, in, &r, &g, &b);
#endif
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
            /* Unlit shade RGB gets the same transfer the lit path applies via
             * psp_gfx_dl_remap_lighting(); every triangle path (direct, tri2,
             * generic, clipped) consumes these already-transformed values. */
            out->r = psp_gfx_color_transfer_u8(in->v.cn[0]);
            out->g = psp_gfx_color_transfer_u8(in->v.cn[1]);
            out->b = psp_gfx_color_transfer_u8(in->v.cn[2]);
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
        psp_gfx_dl_mark_effective_fog_dirty(ctx);
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
    psp_gfx_dl_mark_effective_state_dirty(ctx);
}

static void psp_gfx_dl_handle_other_mode_h(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 shift = (gfx->words.w0 >> 8) & 0xFF;
    u32 length = gfx->words.w0 & 0xFF;
    u32 mask;

    if ((length == 0) || (shift >= 32) || (length > (32 - shift))) {
        psp_gfx_dl_count_unsupported(ctx, PSP_GFX_OP_F3D_SETOTHERMODE_H);
        return;
    }
    mask = (length == 32) ? 0xFFFFFFFFU : (((1U << length) - 1U) << shift);
    ctx->otherModeH = (ctx->otherModeH & ~mask) | (gfx->words.w1 & mask);
}

static void psp_gfx_dl_handle_geometry_mode(PspGfxDlContext* ctx, const Gfx* gfx, int set) {
    u32 nextGeometryMode;

    if (set) {
        nextGeometryMode = ctx->geometryMode | gfx->words.w1;
    } else {
        nextGeometryMode = ctx->geometryMode & ~gfx->words.w1;
    }
    ctx->geometryMode = nextGeometryMode;
    psp_gfx_dl_mark_effective_material_dirty(ctx);
    psp_gfx_dl_mark_effective_depth_dirty(ctx);
}

static void psp_gfx_dl_handle_texture(PspGfxDlContext* ctx, const Gfx* gfx) {
    int enabled = (gfx->words.w0 & 0xFF) != G_OFF;

    if ((ctx->batchCount != 0) && (ctx->textureEnabled != enabled)) {
        psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_TEXTURE_ENABLE);
    }
    ctx->textureEnabled = enabled;
    ctx->textureScaleS = (gfx->words.w1 >> 16) & 0xFFFF;
    ctx->textureScaleT = gfx->words.w1 & 0xFFFF;
    psp_gfx_dl_mark_effective_material_dirty(ctx);
}

static void psp_gfx_dl_handle_set_texture_image(PspGfxDlContext* ctx, const Gfx* gfx) {
    ctx->textureFormat = (gfx->words.w0 >> 21) & 0x7;
    ctx->textureSize = (gfx->words.w0 >> 19) & 0x3;
    ctx->textureImage = psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
    ctx->textureId = 0;
    ctx->textureRef = psp_gfx_dl_null_texture_ref();
    ctx->textureUploadWidth = 0;
    ctx->textureUploadHeight = 0;
    ctx->textureUploadAttempted = 0;
    psp_gfx_dl_mark_effective_material_dirty(ctx);
}

static void psp_gfx_dl_handle_set_color_image(PspGfxDlContext* ctx, const Gfx* gfx) {
    const void* image = psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);

    if (ctx->batchCount != 0) {
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->colorImageFormat = (gfx->words.w0 >> 21) & 0x7;
    ctx->colorImageSize = (gfx->words.w0 >> 19) & 0x3;
    ctx->colorImageWidth = (gfx->words.w0 & 0xFFF) + 1U;
    ctx->colorImage = image;
    ctx->colorImageIsDisplay = psp_gfx_dl_is_display_color_image(image);
}

static void psp_gfx_dl_handle_load_tlut(PspGfxDlContext* ctx) {
    if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b) &&
        (ctx->textureImage != NULL)) {
        ctx->texturePalette = (const u16*) ctx->textureImage;
        ctx->textureUploadAttempted = 0;
        psp_gfx_dl_mark_effective_material_dirty(ctx);
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
    psp_gfx_dl_mark_effective_material_dirty(ctx);
}

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred, int premultiply) {
    int result;
    int hit = 0;
    int supported = 1;
    const u16* palette;

#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    if (ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_PREPARE_CALLS, 1);
    }
#endif
    if (ctx->textureId != 0) {
        return 1;
    }
    if (ctx->textureUploadAttempted) {
        /* Already attempted this generation; avoid retrying on every subsequent triangle. */
        return 0;
    }
    if (ctx->textureImage == NULL) {
        return 0;
    }
    if ((ctx->textureWidth == 0) || (ctx->textureHeight == 0)) {
        return 0;
    }
    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->texturePalette == NULL)) {
        return 0;
    }

    ctx->textureUploadAttempted = 1;
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_8b)) {
        hit = PspGfxPspgl_FindCi8Texture((const u8*) ctx->textureImage, ctx->texturePalette, ctx->textureWidth,
                                         ctx->textureHeight, &ctx->textureId, &ctx->textureRef,
                                         &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId =
                PspGfxPspgl_CreateCi8Texture((const u8*) ctx->textureImage, ctx->texturePalette, ctx->textureWidth,
                                             ctx->textureHeight, &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                             &ctx->textureRef);
        }
    } else if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b)) {
        palette = ctx->texturePalette + (ctx->texturePaletteIndex * 16);
        hit = PspGfxPspgl_FindCi4Texture((const u8*) ctx->textureImage, palette, ctx->textureWidth,
                                         ctx->textureHeight, &ctx->textureId, &ctx->textureRef,
                                         &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId = PspGfxPspgl_CreateCi4Texture((const u8*) ctx->textureImage, palette, ctx->textureWidth,
                                                          ctx->textureHeight, &ctx->textureUploadWidth,
                                                          &ctx->textureUploadHeight, &ctx->textureRef);
        }
    } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        hit = PspGfxPspgl_FindRgba16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                            premultiply, &ctx->textureId, &ctx->textureRef,
                                            &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId = PspGfxPspgl_CreateRgba16Texture((const u16*) ctx->textureImage, ctx->textureWidth,
                                                             ctx->textureHeight, premultiply,
                                                             &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                                             &ctx->textureRef);
        }
    } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_32b)) {
        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            hit = PspGfxPspgl_FindRgba32EnvBlendTexture(ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                                        psp_gfx_dl_primitive_color(ctx),
                                                        psp_gfx_dl_environment_color(ctx), &ctx->textureId,
                                                        &ctx->textureRef, &ctx->textureUploadWidth,
                                                        &ctx->textureUploadHeight);
        } else {
            hit = PspGfxPspgl_FindRgba32Texture(ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                                premultiply, &ctx->textureId, &ctx->textureRef,
                                                &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        }
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
                ctx->textureId = PspGfxPspgl_CreateRgba32EnvBlendTexture(
                    ctx->textureImage, ctx->textureWidth, ctx->textureHeight, psp_gfx_dl_primitive_color(ctx),
                    psp_gfx_dl_environment_color(ctx), &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                    &ctx->textureRef);
            } else {
                ctx->textureId = PspGfxPspgl_CreateRgba32Texture(ctx->textureImage, ctx->textureWidth,
                                                                 ctx->textureHeight, premultiply,
                                                                 &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                                                 &ctx->textureRef);
            }
        }
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b)) {
        hit = PspGfxPspgl_FindIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                         &ctx->textureId, &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                         &ctx->textureRef);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId = PspGfxPspgl_CreateIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth,
                                                          ctx->textureHeight, &ctx->textureUploadWidth,
                                                          &ctx->textureUploadHeight, &ctx->textureRef);
        }
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        hit = PspGfxPspgl_FindIa16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                          &ctx->textureId, &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                          &ctx->textureRef);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId = PspGfxPspgl_CreateIa16Texture((const u16*) ctx->textureImage, ctx->textureWidth,
                                                           ctx->textureHeight, &ctx->textureUploadWidth,
                                                           &ctx->textureUploadHeight, &ctx->textureRef);
        }
    } else {
        supported = 0;
    }

    if (supported && (ctx->textureId != 0)) {
        ctx->stats.textureCount++;
        if (deferred) {
            ctx->stats.deferredTextureCount++;
        }
        if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b)) {
            ctx->stats.ci4TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b)) {
            ctx->stats.rgba16TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_32b)) {
            ctx->stats.rgba32TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b)) {
            ctx->stats.ia8TextureCount++;
        } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b)) {
            ctx->stats.ia16TextureCount++;
        }
        result = 1;
    } else {
        if (supported) {
            ctx->stats.textureRejected++;
        } else {
            ctx->stats.textureRejected++;
        }
        result = 0;
    }
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    if (ctx->trivialRejectDiagnosticActive && supported) {
        if (hit) {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_CACHE_HITS, 1);
        } else {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_CACHE_MISSES, 1);
            if (ctx->textureId != 0) {
                u32 bytesUploaded = ctx->textureUploadWidth * ctx->textureUploadHeight * 4U;

                PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_DECODES, 1);
                PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_UPLOADS, 1);
                PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TEXTURE_BYTES_UPLOADED,
                                                   bytesUploaded);
            }
        }
    }
#endif
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
    return result;
}

static void psp_gfx_dl_handle_set_tile_size(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 tile = (gfx->words.w1 >> 24) & 0x7;
    u32 uls;
    u32 ult;
    u32 lrs;
    u32 lrt;
    u32 widthQuarters;
    u32 heightQuarters;

    if (tile != G_TX_RENDERTILE) {
        return;
    }

    uls = (gfx->words.w0 >> 12) & 0xFFF;
    ult = gfx->words.w0 & 0xFFF;
    lrs = (gfx->words.w1 >> 12) & 0xFFF;
    lrt = gfx->words.w1 & 0xFFF;
    widthQuarters = (lrs >= uls) ? (lrs - uls) : 0;
    heightQuarters = (lrt >= ult) ? (lrt - ult) : 0;
    ctx->textureTileUls = uls;
    ctx->textureTileUlt = ult;
    ctx->textureWidth = (widthQuarters >> G_TEXTURE_IMAGE_FRAC) + 1;
    ctx->textureHeight = (heightQuarters >> G_TEXTURE_IMAGE_FRAC) + 1;
    ctx->textureUploadAttempted = 0;
    psp_gfx_dl_prepare_texture(ctx, 0, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    psp_gfx_dl_mark_effective_material_dirty(ctx);
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

        if ((opcode == G_NOOP) && PSP_RENDERER_DL_MARKER_MATCH(cmd->words.w1)) {
            if (PSP_RENDERER_DL_MARKER_ID(cmd->words.w1) == PSP_RENDERER_DL_MARKER_STARFIELD) {
                psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspRenderer_DrawPendingStarfield();
                continue;
            }
        }

#if SF64_PSP_PROFILE_COMPONENTS
        if ((opcode == G_NOOP) && PSP_PROFILE_DL_COMPONENT_TAG_MATCH(cmd->words.w1)) {
            PspProfiler_ComponentMarker(PSP_PROFILE_DL_COMPONENT_TAG_ID(cmd->words.w1));
            continue;
        }
#endif

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
            PspProfiler_CountNestedDisplayListCall();
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

        if (opcode == PSP_GFX_OP_F3D_SETOTHERMODE_H) {
            psp_gfx_dl_handle_other_mode_h(ctx, cmd);
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

        if (opcode == G_SETCIMG) {
            psp_gfx_dl_handle_set_color_image(ctx, cmd);
            continue;
        }

        if (opcode == G_SETPRIMCOLOR) {
            psp_gfx_dl_handle_set_primitive_color(ctx, cmd);
            continue;
        }

        if (opcode == G_SETENVCOLOR) {
            psp_gfx_dl_handle_set_environment_color(ctx, cmd);
            continue;
        }

        if (opcode == G_SETFILLCOLOR) {
            psp_gfx_dl_handle_set_fill_color(ctx, cmd);
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

        if (opcode == G_FILLRECT) {
            psp_gfx_dl_handle_fill_rectangle(ctx, cmd);
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
            u8 a0 = psp_gfx_dl_decode_tri_index((w0 >> 16) & 0xFF);
            u8 b0 = psp_gfx_dl_decode_tri_index((w0 >> 8) & 0xFF);
            u8 c0 = psp_gfx_dl_decode_tri_index(w0 & 0xFF);
            u8 a1 = psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF);
            u8 b1 = psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF);
            u8 c1 = psp_gfx_dl_decode_tri_index(w1 & 0xFF);

            PspProfiler_CountTriangleCommand(2, 0, 1);
            PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
            PspProfiler_CountTri2OutcomeMatrix(psp_gfx_dl_classify_triangle_outcome(ctx, a0, b0, c0),
                                               psp_gfx_dl_classify_triangle_outcome(ctx, a1, b1, c1));
#endif
#if SF64_PSP_TRI2_PAIR_FASTPATH && SF64_PSP_DIRECT_TRI_FASTPATH
            if (!psp_gfx_dl_try_emit_tri2_direct_pair(ctx, a0, b0, c0, a1, b1, c1)) {
                psp_gfx_dl_emit_tri(ctx, a0, b0, c0);
                psp_gfx_dl_emit_tri(ctx, a1, b1, c1);
            }
#else
            psp_gfx_dl_emit_tri(ctx, a0, b0, c0);
            psp_gfx_dl_emit_tri(ctx, a1, b1, c1);
#endif
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
    ctx->environmentR = 255;
    ctx->environmentG = 255;
    ctx->environmentB = 255;
    ctx->environmentA = 255;
    ctx->fillColor = psp_gfx_dl_pack_rgba_u8(0, 0, 0, 255, 0);
    ctx->colorImage = NULL;
    ctx->colorImageWidth = SCREEN_WIDTH;
    ctx->colorImageFormat = G_IM_FMT_RGBA;
    ctx->colorImageSize = G_IM_SIZ_16b;
    ctx->colorImageIsDisplay = 1;
    ctx->textureScaleS = 0xFFFF;
    ctx->textureScaleT = 0xFFFF;
    ctx->fogA = 255;
    ctx->combineUsesTextureAlpha = 1;
#if (USE_N64PSP_VERTEX_CHAIN2 + 0) || \
    (USE_N64PSP_BATCH_LIGHTING + 0)
    ctx->modelviewSerial = 1;
    ctx->projectionSerial = 1;
    ctx->cachedModelviewSerial = 0;
    ctx->cachedModelviewSerial = 0;
    ctx->cachedProjectionSerial = 0;
#endif
}

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats) {
    PspGfxDlContext* ctx = &sPspGfxDlContext;
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS || N64PSP_VERTEX_BATCH_DIAGNOSTICS
    char line[512];
#endif

    PspGfxPspgl_InitColorTransfer();
    psp_gfx_dl_reset_context(ctx);
    ctx->taskIndex = taskIndex;
    PspProfiler_CountDisplayListTask();
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_DL_TRAVERSAL);
    psp_gfx_dl_run_internal(ctx, dl, 0);
#if SF64_PSP_PROFILE_TRIVIAL_REJECTS
    psp_gfx_dl_trivial_reject_scope_clear_for_task(ctx);
#endif
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
                 "rgba16=%lu rgba32=%lu ci4=%lu ia8=%lu ia16=%lu texTri=%lu alphaTestTri=%lu blendTri=%lu "
                 "texRect=%lu rectReject=%lu fillRect=%lu fillPrim=%lu fillUnsup=%lu "
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
                 (unsigned long) ctx->stats.rgba32TextureCount,
                 (unsigned long) ctx->stats.ci4TextureCount, (unsigned long) ctx->stats.ia8TextureCount,
                 (unsigned long) ctx->stats.ia16TextureCount, (unsigned long) ctx->stats.texturedTriangleCount,
                 (unsigned long) ctx->stats.alphaTestTriangleCount,
                 (unsigned long) ctx->stats.blendTriangleCount,
                 (unsigned long) ctx->stats.textureRectangleCount,
                 (unsigned long) ctx->stats.textureRectangleRejected,
                 (unsigned long) ctx->stats.fillRectangleCount,
                 (unsigned long) ctx->stats.fillRectanglePrimitiveColorCount,
                 (unsigned long) ctx->stats.fillRectangleUnsupportedCount,
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

#if PSP_RENDERER_DIAGNOSTICS
    if ((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
        (ctx->stats.depthLimitHit != 0)) {
        u32 i;
        u32 lineUsed;

        snprintf(line, sizeof(line),
                 "[pspgl-vtx-hist] task=%lu cmds=%lu lit=%lu unlit=%lu",
                 (unsigned long) taskIndex,
                 (unsigned long) ctx->vtxCommandCount,
                 (unsigned long) ctx->litVertexCount,
                 (unsigned long) ctx->unlitVertexCount);
        PspPlatform_LogLine(line);

        lineUsed = (u32) snprintf(line, sizeof(line),
                                  "[pspgl-vtx-sizes] task=%lu",
                                  (unsigned long) taskIndex);
        for (i = 1; i <= PSP_GFX_DL_MAX_VERTICES; i++) {
            if (ctx->vtxBatchSizeHistogram[i] == 0) {
                continue;
            }
            if (lineUsed > 430) {
                PspPlatform_LogLine(line);
                lineUsed = (u32) snprintf(line, sizeof(line),
                                          "[pspgl-vtx-sizes] task=%lu",
                                          (unsigned long) taskIndex);
            }
            lineUsed += (u32) snprintf(line + lineUsed, sizeof(line) - lineUsed,
                                       " %lu:%lu",
                                       (unsigned long) i,
                                       (unsigned long) ctx->vtxBatchSizeHistogram[i]);
        }
        PspPlatform_LogLine(line);

        lineUsed = (u32) snprintf(line, sizeof(line),
                                  "[pspgl-vtx-lights] task=%lu",
                                  (unsigned long) taskIndex);
        for (i = 0; i <= 7; i++) {
            if (ctx->vtxLightCountHistogram[i] == 0) {
                continue;
            }
            lineUsed += (u32) snprintf(line + lineUsed, sizeof(line) - lineUsed,
                                       " %lu:%lu",
                                       (unsigned long) i,
                                       (unsigned long) ctx->vtxLightCountHistogram[i]);
        }
        PspPlatform_LogLine(line);
    }
#endif

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

#if N64PSP_VERTEX_BATCH_DIAGNOSTICS
    if ((ctx->vtxDiagCommands != 0) &&
        ((taskIndex < 4) || ((taskIndex % 30) == 0) ||
         (ctx->stats.commandLimitHit != 0) ||
         (ctx->stats.depthLimitHit != 0))) {
        float meanBatch =
            (float) ctx->vtxDiagVertices /
            (float) ctx->vtxDiagCommands;

        snprintf(line, sizeof(line),
                 "[pspgl-vtx-diag] task=%lu cmds=%lu vertices=%lu min=%lu max=%lu mean=%.2f "
                 "buckets=1-4:%lu 5-8:%lu 9-16:%lu 17-24:%lu 25-32:%lu over32:%lu",
                 (unsigned long) taskIndex,
                 (unsigned long) ctx->vtxDiagCommands,
                 (unsigned long) ctx->vtxDiagVertices,
                 (unsigned long) ctx->vtxDiagMinBatch,
                 (unsigned long) ctx->vtxDiagMaxBatch,
                 meanBatch,
                 (unsigned long) ctx->vtxDiagBuckets[0],
                 (unsigned long) ctx->vtxDiagBuckets[1],
                 (unsigned long) ctx->vtxDiagBuckets[2],
                 (unsigned long) ctx->vtxDiagBuckets[3],
                 (unsigned long) ctx->vtxDiagBuckets[4],
                 (unsigned long) ctx->vtxDiagBuckets[5]);
        PspPlatform_LogLine(line);
    }
#endif

    return ctx->stats.commandCount > 0;
}
