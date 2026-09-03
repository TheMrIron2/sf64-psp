#include "src/psp/gfx/gfx_psp_dl.h"

#include "buffers.h"
#include "macros.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/hw_counter_profile.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#if PROFILE_COMPONENTS
#include "src/psp/render_component.h"
#endif

#include <stdint.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

static int sPspGfxDlBackgroundFeedbackPrimed = 0;
static u32 sPspGfxDlBackgroundFeedbackSeedColor = 0xFF000000u;

extern u16 aTrBackdropBottomTex[];
extern u16 aTrBackdropTopTex[];

#include <n64psp/math.h>
#include <n64psp/fog.h>
#include <n64psp/lighting.h>
#include <n64psp/tnl.h>

#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#ifndef PSP_ORIGINAL_FOG
#define PSP_ORIGINAL_FOG 0
#endif

static const u8 sPspGfxDlPresentedFogAlpha[32] = {
    0, 4, 8, 12, 16, 21, 25, 30,
    34, 39, 44, 48, 53, 59, 64, 69,
    75, 80, 86, 92, 99, 105, 112, 120,
    128, 136, 145, 154, 165, 177, 191, 210,
};

#if PSP_ORIGINAL_FOG
static u8 psp_gfx_dl_present_fog_alpha(u8 alpha) {
    return sPspGfxDlPresentedFogAlpha[alpha >> 3];
}
#endif

#if PSP_RENDERER_DIAGNOSTICS
#include "assets/ast_map.h"
#include <pspctrl.h>

extern Gfx gMapVenomCloudRuntimeDL[];
#endif

#ifndef PROFILE_TRIVIAL_REJECTS
#define PROFILE_TRIVIAL_REJECTS 0
#endif




// Open batch pool, see docs/psp_counter_findings.md finding 9
// Keeps one open batch per texture material so a texture change no longer forces
// a draw, only the non material state changes do
// Measured 2026-07-28: title 177 -> 89 draws per frame, 24.2 -> 22.0 ms
#ifndef PSP_BATCH_POOL_SLOTS
#define PSP_BATCH_POOL_SLOTS 64
#endif
#ifndef PSP_BATCH_POOL_VERTICES
#define PSP_BATCH_POOL_VERTICES 192
#endif

#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS || PROFILE_GPROF || PROFILE_PHASES
#define PSP_GFX_DL_HOT_STATS 1
#else
#define PSP_GFX_DL_HOT_STATS 0
#endif

#define PSP_GFX_DL_MAX_DEPTH 8
#define PSP_GFX_DL_MAX_COMMANDS 8192
#define PSP_GFX_DL_MAX_NESTED_COMMANDS 2048
#define PSP_GFX_DL_MAX_VERTICES 64
#define PSP_GFX_DL_BATCH_VERTICES 3072
#define PSP_GFX_DL_MTX_STACK_DEPTH 4
#define PSP_GFX_DL_CLIP_PLANES 6
#define PSP_GFX_DL_MAX_CLIP_VERTICES 12
/* One spare snapshot allows acquisition before overwritten vertices release theirs */
#define PSP_GFX_DL_PROJECTION_SNAPSHOTS (PSP_GFX_DL_MAX_VERTICES + 1)
#define PSP_GFX_DL_NO_PROJECTION_SNAPSHOT PSP_GFX_DL_PROJECTION_SNAPSHOTS
#define PSP_GFX_DL_PERSPECTIVE_W_RATIO 1.5f
#define PSP_GFX_DL_PERSPECTIVE_MAX_DEPTH 5
#define PSP_GFX_DL_DEPTH_BIAS_NDC 0.0005f
#define PSP_GFX_DL_DEPTH_BIAS_VIEW 0.5f

#if PSP_RENDERER_DIAGNOSTICS
#define PSP_GFX_DL_TRACE_MAX_RECORDS 320

typedef struct {
    int valid;
    u32 distance;
    s16 objectX;
    s16 objectY;
    s16 objectZ;
    float modelview[4][4];
    float projection[4][4];
    float view[4];
    float clip[4];
    s16 fogMul;
    s16 fogOffset;
    u8 fogAlpha;
} PspGfxDlFogTransformSample;
#endif

#define PSP_GFX_OP_F3D_SPNOOP 0x00
#define PSP_GFX_OP_F3D_MTX 0x01
#define PSP_GFX_OP_PORT_MTXF PSP_RENDERER_DL_OP_MTXF
#define PSP_GFX_OP_PORT_INVALIDATE_RGBA16 PSP_RENDERER_DL_OP_INVALIDATE_RGBA16
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

typedef char PspGfxDlPackedVertexSizeCheck[
    (sizeof(Vtx) == sizeof(n64psp_packed_vertex)) ? 1 : -1
];
typedef char PspGfxDlPackedVertexAttributeOffsetCheck[
    (offsetof(Vtx_t, cn) == offsetof(n64psp_packed_vertex, attribute)) ? 1 : -1
];

typedef struct {
    float p22;
    float p23;
    float p32;
    float p33;
} PspGfxDlFogProjection;

typedef struct {
    float matrix[4][4];
    PspGfxDlFogProjection fogProjection;
    u32 serial;
    u32 refCount;
} PspGfxDlProjectionSnapshot;

typedef union {
    u32 raw;
    struct {
        u8 valid;
        u8 fogAlpha;
        u16 reserved;
    } fields;
} PspGfxDlVertexState;

typedef char PspGfxDlVertexStateSizeCheck[
    (sizeof(PspGfxDlVertexState) == 4) ? 1 : -1
];

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
    const float (*projection)[4];
    u32 projectionSerial;
    u32 projectionSnapshot;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    s16 s;
    s16 t;
    u32 clipCode;
    PspGfxDlVertexState state;
} PspGfxDlVertex;

typedef char PspGfxDlVertexSizeCheck[
    (sizeof(PspGfxDlVertex) == 72) ? 1 : -1
];

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
    u8 generated;
    u8 fogAlpha;
    u16 reserved;
} PspGfxDlClipVertex;

typedef char PspGfxDlClipVertexSizeCheck[
    (sizeof(PspGfxDlClipVertex) == 60) ? 1 : -1
];

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
    int pointFilter;
    int valid;
    int dirty;
} PspGfxDlEffectiveMaterialState;

typedef struct {
    int depthTest;
    int depthWrite;
    int depthBias;
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
    /* The GE projection is depth adapted while N64 fog retains the original depth mapping */
    float fogProjection[4][4];
    PspGfxDlProjectionSnapshot projectionSnapshots[PSP_GFX_DL_PROJECTION_SNAPSHOTS];
    float modelviewStack[PSP_GFX_DL_MTX_STACK_DEPTH][4][4];
    float batchProjection[4][4];
    u32 batchCount;
    u32 modelviewStackDepth;
    u32 projectionSerial;
    u32 currentProjectionSnapshot;
    u32 batchProjectionSerial;
#if PROFILE_COMPONENTS
    u32 batchComponentMask;
#endif
    n64psp_tnl_matrices alignedMatrices;
    u32 modelviewSerial;
    u32 cachedModelviewSerial;
    u32 cachedProjectionSerial;
    int alignedMatricesValid;
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
    s32 textureScaleS;
    s32 textureScaleT;
    u32 textureWidth;
    u32 textureHeight;
    u32 textureUploadWidth;
    u32 textureUploadHeight;
    u32 textureUploadX;
    u32 textureUploadY;
    int textureMirrorFallback;
    u32 textureMirrorFeasibilityKey;
    s32 textureTileUls;
    s32 textureTileUlt;
    u32 textureCms;
    u32 textureCmt;
    u32 textureMaskS;
    u32 textureMaskT;
#if PSP_RENDERER_DIAGNOSTICS
    u32 textureShiftS;
    u32 textureShiftT;
#endif
    u32 textureId;
    PspGfxPspglTextureRef textureRef;
    int textureUploadAttempted;
    u32 batchTextureId;
    PspGfxPspglTextureRef batchTextureRef;
    u32 geometryMode;
    u32 lightCount;
    PspGfxDlLight lights[7];
    int lightingStateDirty;
    u32 groupedLightCount;
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
#if PSP_RENDERER_DIAGNOSTICS
    u32 primitiveColorRaw;
    u32 environmentColorRaw;
    u32 fogColorRaw;
#endif
    u32 fillColor;
    const void* colorImage;
    u32 colorImageFormat;
    u32 colorImageSize;
    u32 colorImageWidth;
    int colorImageIsDisplay;
    PspGfxDlCombineMode combineMode;
#if PSP_RENDERER_DIAGNOSTICS
    u32 combineMux0;
    u32 combineMux1;
#endif
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
    int batchSprites;
    int batchDepthTest;
    int batchDepthWrite;
    int batchDepthBias;
    int batchFog;
    int batchOriginalFog;
    int batchPointFilter;
    PspGfxPspglVertexReservation batchReservation;
    int batchReserved;
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
#if PROFILE_TRIVIAL_REJECTS
    int trivialRejectDiagnosticActive;
#endif
#if PSP_RENDERER_DIAGNOSTICS
    int traceActive;
    u32 traceDrawIndex;
    u32 traceRecordCount;
    u32 traceDroppedCount;
    u32 fogInterpolationSampleCount;
    PspGfxDlFogTransformSample fogTransformSamples[3];
    u32 traceLastStateHash;
    int traceHasStateHash;
    u32 vtxCommandCount;
    u32 vtxBatchSizeHistogram[PSP_GFX_DL_MAX_VERTICES + 1];
    u32 vtxLightCountHistogram[8];
    u32 litVertexCount;
    u32 unlitVertexCount;
#endif
} PspGfxDlContext;

static PspGfxDlContext sPspGfxDlContext;

#if PSP_RENDERER_DIAGNOSTICS
static volatile int sPspGfxDlTraceArmed;
static u32 sPspGfxDlTracePreviousButtons;
static int sPspGfxDlTraceHintLogged;

// SF64 material corpus capture; measures how finite the effective material set is
// Storage is file static so it accumulates across tasks rather than per-task reset
#define PSP_GFX_DL_MATERIAL_CORPUS_ENTRIES 96
#define PSP_GFX_DL_MATERIAL_CORPUS_NONE 0xFFFFFFFFU

// geometry-mode bits this renderer actually consults
#define PSP_GFX_DL_MATERIAL_GEOMETRY_MASK \
    (G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN)

typedef struct {
    u32 key;
    u32 combineMode;
    u32 otherModeH;
    u32 otherModeL;
    u32 geometryMode;
    u32 textureFormat;
    u32 textureSize;
    u32 applyCount;
    u32 triangleCount;
    u8 textured;
    u8 textureEnv;
    u8 wrapS;
    u8 wrapT;
    u8 alphaTest;
    u8 blend;
    u8 premultiplied;
    u8 pointFilter;
    u8 depthTest;
    u8 depthWrite;
    u8 fog;
} PspGfxDlMaterialCorpusEntry;

static PspGfxDlMaterialCorpusEntry sPspGfxDlMaterialCorpus[PSP_GFX_DL_MATERIAL_CORPUS_ENTRIES];
static u32 sPspGfxDlMaterialCorpusCount;
static u32 sPspGfxDlMaterialCorpusOverflow;
static u32 sPspGfxDlMaterialCorpusCurrent = PSP_GFX_DL_MATERIAL_CORPUS_NONE;
static u32 sPspGfxDlMaterialCorpusTriangles;
static u32 sPspGfxDlMaterialCorpusUnattributed;
#endif

static PspGfxPspglColorVertex
    sPspGfxDlBatch[PSP_GFX_DL_BATCH_VERTICES]
    __attribute__((aligned(16)));

// the append sites write through these, so the current target is either a pool
// slot or the standalone buffer used for geometry that must not be reordered
static PspGfxPspglColorVertex* sPspGfxDlBatchCursor = sPspGfxDlBatch;
static u32 sPspGfxDlBatchCapacity = PSP_GFX_DL_BATCH_VERTICES;
#define PSP_GFX_DL_BATCH sPspGfxDlBatchCursor
#define PSP_GFX_DL_BATCH_CAP sPspGfxDlBatchCapacity

#if PSP_ORIGINAL_FOG
static u8 sPspGfxDlBatchFogAlpha[PSP_GFX_DL_BATCH_VERTICES] __attribute__((aligned(16)));
static u8* sPspGfxDlBatchFogAlphaCursor = sPspGfxDlBatchFogAlpha;
static PspGfxPspglFogVertex sPspGfxDlFogBatch[PSP_GFX_DL_BATCH_VERTICES] __attribute__((aligned(16)));
#define PSP_GFX_DL_BATCH_FOG_ALPHA sPspGfxDlBatchFogAlphaCursor
#endif

static n64psp_vec4f
    sPspGfxDlTransformInput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16), used));

static n64psp_vec4f_pair
    sPspGfxDlTransformOutput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16), used));

static n64psp_snorm8x4
    sPspGfxDlLightingNormals[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16), used));

static n64psp_vec4f
    sPspGfxDlLightingOutput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));

static n64psp_texcoord_s10_5
    sPspGfxDlTexgenOutput[PSP_GFX_DL_MAX_VERTICES]
    __attribute__((aligned(16)));

static n64psp_vec4f
    sPspGfxDlLightingAmbient
    __attribute__((aligned(16)));

static n64psp_directional_lightf
    sPspGfxDlLightingLights[7]
    __attribute__((aligned(16)));

static n64psp_directional_lightf
    sPspGfxDlGroupedLightingLights[2]
    __attribute__((aligned(16)));

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
static int psp_gfx_dl_blend_enabled(PspGfxDlContext* ctx);
static int psp_gfx_dl_baked_env_blend_texture_enabled(const PspGfxDlContext* ctx);

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

static void psp_gfx_dl_get_fog_projection(const float matrix[4][4],
                                          PspGfxDlFogProjection* projection) {
    projection->p22 = matrix[2][2];
    projection->p23 = matrix[2][3];
    projection->p32 = matrix[3][2];
    projection->p33 = matrix[3][3];
}

static float psp_gfx_dl_fog_distance(const PspGfxDlFogProjection* projection, float ndcZ) {
    float denominator = projection->p22 - (ndcZ * projection->p23);

    if ((denominator > -0.000001f) && (denominator < 0.000001f)) {
        return 0.0f;
    }
    return (projection->p32 - (ndcZ * projection->p33)) / denominator;
}

#if !PSP_ORIGINAL_FOG
typedef struct {
    float projection22;
    float projection23;
    float projection32;
    float projection33;
    float start;
    float end;
    s16 fogMul;
    s16 fogOffset;
    int valid;
} PspGfxDlApproxFogFit;

static PspGfxDlApproxFogFit sPspGfxDlApproxFogFit;

static int psp_gfx_dl_black_fog_minimax_feasible(const float distances[23], const float values[23],
                                                 float error, float* slope, float* intercept) {
    float slopeLow = 0.000001f;
    float slopeHigh = 1000000.0f;
    float interceptLow = -1000000.0f;
    float interceptHigh = 0.0f;
    u32 i;
    u32 j;

    for (i = 0; i < 23; i++) {
        float minimumSlope = (values[i] - error) / distances[i];

        if (minimumSlope > slopeLow) {
            slopeLow = minimumSlope;
        }
        for (j = i + 1; j < 23; j++) {
            float distanceSpan = distances[j] - distances[i];
            float valueSpan = values[j] - values[i];
            float pairLow;
            float pairHigh;

            if (distanceSpan <= 0.0f) {
                return 0;
            }
            pairLow = (valueSpan - (2.0f * error)) / distanceSpan;
            pairHigh = (valueSpan + (2.0f * error)) / distanceSpan;
            if (pairLow > slopeLow) {
                slopeLow = pairLow;
            }
            if (pairHigh < slopeHigh) {
                slopeHigh = pairHigh;
            }
        }
    }
    if (slopeHigh < slopeLow) {
        return 0;
    }
    *slope = (slopeLow + slopeHigh) * 0.5f;
    for (i = 0; i < 23; i++) {
        float sampleLow = values[i] - error - (*slope * distances[i]);
        float sampleHigh = values[i] + error - (*slope * distances[i]);

        if (sampleLow > interceptLow) {
            interceptLow = sampleLow;
        }
        if (sampleHigh < interceptHigh) {
            interceptHigh = sampleHigh;
        }
    }
    if (interceptHigh < interceptLow) {
        return 0;
    }
    *intercept = (interceptLow + interceptHigh) * 0.5f;
    return 1;
}

static int psp_gfx_dl_fit_black_fog_curve(const PspGfxDlFogProjection* projection, s16 fogMul,
                                          s16 fogOffset, float* start, float* end) {
    float distances[23];
    float values[23];
    float errorLow = 0.0f;
    float errorHigh = 1.0f;
    float slope = 0.0f;
    float intercept = 0.0f;
    u32 i;

    for (i = 0; i < 23; i++) {
        u32 q = i + 8U;
        float alpha = (float) ((q << 3) + 4U);
        float ndcZ = (alpha - (float) fogOffset) / (float) fogMul;

        distances[i] = psp_gfx_dl_fog_distance(projection, ndcZ);
        values[i] = (float) sPspGfxDlPresentedFogAlpha[q] / 255.0f;
    }
    for (i = 0; i < 24; i++) {
        float error = (errorLow + errorHigh) * 0.5f;

        if (psp_gfx_dl_black_fog_minimax_feasible(distances, values, error,
                                                   &slope, &intercept)) {
            errorHigh = error;
        } else {
            errorLow = error;
        }
    }
    if (!psp_gfx_dl_black_fog_minimax_feasible(distances, values, errorHigh,
                                                &slope, &intercept) ||
        (slope <= 0.000001f)) {
        return 0;
    }
    *start = -intercept / slope;
    *end = (1.0f - intercept) / slope;
    return (*start >= 0.0f) && (*end > *start);
}

static int psp_gfx_dl_get_black_fog_curve(const PspGfxDlFogProjection* projection, s16 fogMul,
                                          s16 fogOffset, float* start, float* end) {
    PspGfxDlApproxFogFit* fit = &sPspGfxDlApproxFogFit;

    if (!fit->valid || (fit->fogMul != fogMul) || (fit->fogOffset != fogOffset) ||
        (fit->projection22 != projection->p22) || (fit->projection23 != projection->p23) ||
        (fit->projection32 != projection->p32) || (fit->projection33 != projection->p33)) {
        if (!psp_gfx_dl_fit_black_fog_curve(projection, fogMul, fogOffset,
                                            &fit->start, &fit->end)) {
            fit->valid = 0;
            return 0;
        }
        fit->projection22 = projection->p22;
        fit->projection23 = projection->p23;
        fit->projection32 = projection->p32;
        fit->projection33 = projection->p33;
        fit->fogMul = fogMul;
        fit->fogOffset = fogOffset;
        fit->valid = 1;
    }
    *start = fit->start;
    *end = fit->end;
    return 1;
}
#endif

static PspGfxPspglTextureWrap psp_gfx_dl_texture_wrap(u32 mode, u32 mask) {
    if ((mode & G_TX_CLAMP) != 0) {
        return PSP_GFX_PSPGL_WRAP_CLAMP;
    }
    if (mask == G_TX_NOMASK) {
        return PSP_GFX_PSPGL_WRAP_CLAMP;
    }
    if ((mode & G_TX_MIRROR) != 0) {
        return PSP_GFX_PSPGL_WRAP_MIRROR;
    }
    return PSP_GFX_PSPGL_WRAP_REPEAT;
}

#define PSP_GFX_DL_ENCODED_MIRROR(ctx, mode, mask) \
    (!(ctx)->textureMirrorFallback && (((mode) & G_TX_MIRROR) != 0) && ((mask) != G_TX_NOMASK))

static PspGfxPspglTextureWrap psp_gfx_dl_texture_draw_wrap(u32 mode, u32 mask, int needsWrap) {
    if (((mode & G_TX_MIRROR) != 0) && !needsWrap) {
        return PSP_GFX_PSPGL_WRAP_CLAMP;
    }
    return psp_gfx_dl_texture_wrap(mode, mask);
}

static PspGfxPspglTextureWrap psp_gfx_dl_texture_tri3_wrap(u32 mode, u32 mask, u32 uploadSize,
                                                           s16 a, s16 b, s16 c, int encodedMirror) {
    u32 limit;
    int needsWrap;
    PspGfxPspglTextureWrap wrap;

    if (encodedMirror) {
        return PSP_GFX_PSPGL_WRAP_REPEAT;
    }

    if ((mode & G_TX_MIRROR) == 0) {
        PspProfiler_CountMirrorClassification(0, 1, 0, 0);
        return psp_gfx_dl_texture_wrap(mode, mask);
    }
    limit = uploadSize << 5;
    needsWrap = (a < 0) || (b < 0) || (c < 0) || ((u32) a > limit) ||
                ((u32) b > limit) || ((u32) c > limit);
    wrap = psp_gfx_dl_texture_draw_wrap(mode, mask, needsWrap);
    PspProfiler_CountMirrorClassification(1, 0, wrap == PSP_GFX_PSPGL_WRAP_CLAMP,
                                          wrap == PSP_GFX_PSPGL_WRAP_MIRROR);
    return wrap;
}

static PspGfxPspglTextureWrap psp_gfx_dl_texture_tri6_wrap(u32 mode, u32 mask, u32 uploadSize,
                                                           s16 a, s16 b, s16 c, s16 d, s16 e, s16 f,
                                                           int encodedMirror) {
    u32 limit;
    int needsWrap;
    PspGfxPspglTextureWrap wrap;

    if (encodedMirror) {
        return PSP_GFX_PSPGL_WRAP_REPEAT;
    }

    if ((mode & G_TX_MIRROR) == 0) {
        PspProfiler_CountMirrorClassification(0, 1, 0, 0);
        return psp_gfx_dl_texture_wrap(mode, mask);
    }
    limit = uploadSize << 5;
    needsWrap = (a < 0) || (b < 0) || (c < 0) || (d < 0) || (e < 0) || (f < 0) ||
                ((u32) a > limit) || ((u32) b > limit) || ((u32) c > limit) ||
                ((u32) d > limit) || ((u32) e > limit) || ((u32) f > limit);
    wrap = psp_gfx_dl_texture_draw_wrap(mode, mask, needsWrap);
    PspProfiler_CountMirrorClassification(1, 0, wrap == PSP_GFX_PSPGL_WRAP_CLAMP,
                                          wrap == PSP_GFX_PSPGL_WRAP_MIRROR);
    return wrap;
}

#if PROFILE_PHASES
static void psp_gfx_dl_profile_mirror_texture(PspGfxDlContext* ctx, int clampS, int clampT,
                                               u32 triangles) {
    u32 mirrorS = (ctx->textureCms & G_TX_MIRROR) != 0;
    u32 mirrorT = (ctx->textureCmt & G_TX_MIRROR) != 0;
    u32 palette = (u32) ctx->texturePalette;

    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b)) {
        palette += ctx->texturePaletteIndex * 16U * sizeof(u16);
    }
    PspProfiler_RecordMirrorTexture((u32) ctx->textureImage, palette, ctx->textureId,
                                    ctx->textureRef.generation, ctx->textureFormat, ctx->textureSize,
                                    ctx->textureWidth, ctx->textureHeight, ctx->textureUploadWidth,
                                    ctx->textureUploadHeight, mirrorS, mirrorT, mirrorS && clampS,
                                    mirrorS && !clampS, mirrorT && clampT, mirrorT && !clampT,
                                    triangles);
}
#endif

static float psp_gfx_dl_normalize_s10_5_scaled(s16 coord, u32 uploadSize, u32 uploadOffset, s32 tileOrigin,
                                               s32 scale) {
    float scaledCoord = ((float) coord * (float) scale) / 65536.0f;

    if (uploadSize == 0) {
        return 0.0f;
    }
    return (scaledCoord - ((float) tileOrigin * 8.0f) + ((float) uploadOffset * 32.0f)) /
           (32.0f * (float) uploadSize);
}

static float psp_gfx_dl_normalize_s10_5_s(const PspGfxDlContext* ctx, s16 coord, u32 uploadSize, s32 tileOrigin) {
    return psp_gfx_dl_normalize_s10_5_scaled(coord, uploadSize, ctx->textureUploadX, tileOrigin,
                                             ctx->textureScaleS);
}

static float psp_gfx_dl_normalize_s10_5_t(const PspGfxDlContext* ctx, s16 coord, u32 uploadSize, s32 tileOrigin) {
    return psp_gfx_dl_normalize_s10_5_scaled(coord, uploadSize, ctx->textureUploadY, tileOrigin,
                                             ctx->textureScaleT);
}

static float psp_gfx_dl_normalize_texel_coord(float coord, u32 uploadSize, u32 uploadOffset, s32 tileOrigin) {
    float result;

    if (uploadSize == 0) {
        result = 0.0f;
    } else {
        result = (coord - ((float) tileOrigin * 0.25f) + (float) uploadOffset) / (float) uploadSize;
    }
    return result;
}

static void psp_gfx_dl_apply_depth_bias(PspGfxDlContext* ctx, float* z) {
    if (!ctx->batchDepthBias) {
        return;
    }
    if (ctx->batchPretransformed) {
        *z -= PSP_GFX_DL_DEPTH_BIAS_NDC;
    } else {
        *z += PSP_GFX_DL_DEPTH_BIAS_VIEW;
    }
}

static int psp_gfx_dl_rgba16_coverage_alpha_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha && psp_gfx_dl_blend_enabled(ctx) &&
           ((ctx->otherModeL & CVG_DST_SAVE) == CVG_DST_SAVE) &&
           ((ctx->otherModeL & 3U) == G_AC_NONE) &&
           (ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b);
}

static int psp_gfx_dl_soft_coverage_texture_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha && psp_gfx_dl_blend_enabled(ctx) &&
           ((ctx->otherModeL & CVG_DST_SAVE) == CVG_DST_SAVE) &&
           ((ctx->otherModeL & 3U) == G_AC_NONE) &&
           (ctx->textureFormat == G_IM_FMT_IA) &&
           ((ctx->textureSize == G_IM_SIZ_8b) || (ctx->textureSize == G_IM_SIZ_16b));
}

static int psp_gfx_dl_alpha_test_enabled(PspGfxDlContext* ctx) {
    if (psp_gfx_dl_rgba16_coverage_alpha_enabled(ctx)) {
        if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA) {
            return 1;
        }
        return ((ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA) &&
                ((ctx->otherModeH & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_1CYCLE)) ? 0 : 2;
    }
    if (psp_gfx_dl_soft_coverage_texture_enabled(ctx)) {
        return 1;
    }
    return ctx->combineUsesTextureAlpha &&
           (((ctx->otherModeL & 3U) != G_AC_NONE) || ((ctx->otherModeL & CVG_X_ALPHA) != 0));
}

static int psp_gfx_dl_effective_point_filter(PspGfxDlContext* ctx) {
    u32 filter = ctx->otherModeH & (3U << G_MDSFT_TEXTFILT);
    int trainingBackdrop =
        (ctx->textureImage == aTrBackdropBottomTex) ||
        (ctx->textureImage == aTrBackdropTopTex);

    return (filter == G_TF_POINT) ||
           (trainingBackdrop && psp_gfx_dl_rgba16_coverage_alpha_enabled(ctx));
}

static int psp_gfx_dl_blend_enabled(PspGfxDlContext* ctx) {
    return ctx->combineUsesTextureAlpha && ((ctx->otherModeL & FORCE_BL) != 0);
}

static int psp_gfx_dl_depth_bias_enabled(PspGfxDlContext* ctx) {
    return ((ctx->geometryMode & G_ZBUFFER) != 0) &&
           ((ctx->otherModeL & Z_CMP) != 0) &&
           ((ctx->otherModeL & Z_UPD) == 0) &&
           ((ctx->otherModeL & FORCE_BL) != 0) &&
           ((ctx->otherModeL & CVG_DST_SAVE) == CVG_DST_SAVE) &&
           ((ctx->otherModeL & ZMODE_DEC) == ZMODE_XLU);
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
    if (ctx->combineMode != PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        return 0;
    }
    return ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_32b)) ||
           ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_8b));
}

static PspGfxPspglTextureEnv psp_gfx_dl_texture_env_for_combine(const PspGfxDlContext* ctx) {
    if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
        return PSP_GFX_PSPGL_TEX_MODULATE;
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

#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
static int psp_gfx_dl_has_bounded_end(const Gfx* dl) {
    u32 i;

    for (i = 0; i < PSP_GFX_DL_MAX_NESTED_COMMANDS; i++) {
        if (psp_gfx_dl_is_end(psp_gfx_dl_opcode(&dl[i]))) {
            return 1;
        }
    }
    return 0;
}
#endif

#if PSP_RENDERER_DIAGNOSTICS
static int sMapExplosionDlProbeDone;
static int sMapVenomDlProbeDone;
static int sMapCursorDlProbeDone;

static int psp_gfx_dl_same_address(const void* a, const void* b) {
    return (((uintptr_t) a) & 0x3FFFFFFFu) == (((uintptr_t) b) & 0x3FFFFFFFu);
}

static void psp_gfx_dl_probe_map_asset_dl(const char* label, const Gfx* dl, u32 rawTarget, int noPush,
                                          int boundedEnd, u32 depth, const void* expectedVtx) {
    char line[224];
    u32 i;

    snprintf(line, sizeof(line),
             "[map-dl-probe] label=%s raw=%08lx resolved=%p noPush=%d boundedEnd=%d depth=%lu expectedVtx=%p",
             label, (unsigned long) rawTarget, (const void*) dl, noPush, boundedEnd, (unsigned long) depth,
             expectedVtx);
    PspPlatform_LogLine(line);
    for (i = 0; i < 8; i++) {
        snprintf(line, sizeof(line), "[map-dl-raw] label=%s i=%lu addr=%p w0=%08lx w1=%08lx",
                 label, (unsigned long) i, (const void*) &dl[i], (unsigned long) dl[i].words.w0,
                 (unsigned long) dl[i].words.w1);
        PspPlatform_LogLine(line);
    }
}
#endif

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

static void psp_gfx_dl_mtx_mul(
    float out[4][4],
    const float a[4][4],
    const float b[4][4]
) {
    n64psp_mat4f alignedA;
    n64psp_mat4f alignedB;
    n64psp_mat4f alignedResult;

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

    psp_gfx_dl_mtx_copy(out, alignedResult.m);
}

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
                ctx->alignedMatrices.modelview.m,
                ctx->modelview
            );
        } else {
            psp_gfx_dl_identity(ctx->alignedMatrices.modelview.m);
        }

        ctx->cachedModelviewSerial = ctx->modelviewSerial;
    }

    if (ctx->alignedMatricesValid &&
        !modelviewChanged &&
        (ctx->cachedProjectionSerial == ctx->projectionSerial)) {
        return;
    }

    if (ctx->hasProjection) {
        psp_gfx_dl_mtx_copy(ctx->alignedMatrices.projection.m, ctx->projection);
    } else {
        psp_gfx_dl_identity(ctx->alignedMatrices.projection.m);
    }

    ctx->cachedProjectionSerial = ctx->projectionSerial;
    ctx->alignedMatricesValid = 1;
}

static u32 psp_gfx_dl_prepare_vertex_projection(PspGfxDlContext* ctx, u32 count) {
    u32 freeSnapshot = PSP_GFX_DL_NO_PROJECTION_SNAPSHOT;
    u32 i;

    if (!ctx->hasProjection) {
        return PSP_GFX_DL_NO_PROJECTION_SNAPSHOT;
    }

    if ((ctx->currentProjectionSnapshot < PSP_GFX_DL_PROJECTION_SNAPSHOTS) &&
        (ctx->projectionSnapshots[ctx->currentProjectionSnapshot].serial == ctx->projectionSerial)) {
        ctx->projectionSnapshots[ctx->currentProjectionSnapshot].refCount += count;
        return ctx->currentProjectionSnapshot;
    }

    for (i = 0; i < PSP_GFX_DL_PROJECTION_SNAPSHOTS; i++) {
        PspGfxDlProjectionSnapshot* snapshot = &ctx->projectionSnapshots[i];

        if ((snapshot->refCount == 0) &&
            (freeSnapshot == PSP_GFX_DL_NO_PROJECTION_SNAPSHOT)) {
            freeSnapshot = i;
        }
    }

    psp_gfx_dl_mtx_copy(ctx->projectionSnapshots[freeSnapshot].matrix, ctx->projection);
    psp_gfx_dl_get_fog_projection(ctx->fogProjection,
                                  &ctx->projectionSnapshots[freeSnapshot].fogProjection);
    ctx->projectionSnapshots[freeSnapshot].serial = ctx->projectionSerial;
    ctx->projectionSnapshots[freeSnapshot].refCount = count;
    ctx->currentProjectionSnapshot = freeSnapshot;
    return freeSnapshot;
}

static const PspGfxDlFogProjection* psp_gfx_dl_vertex_fog_projection(
    const PspGfxDlContext* ctx, const PspGfxDlVertex* vertex) {
    if (vertex->projectionSnapshot < PSP_GFX_DL_PROJECTION_SNAPSHOTS) {
        return &ctx->projectionSnapshots[vertex->projectionSnapshot].fogProjection;
    }
    return NULL;
}

static void psp_gfx_dl_set_vertex_projection(PspGfxDlContext* ctx, PspGfxDlVertex* vertex,
                                             u32 projectionSnapshot) {
    if ((vertex->projectionSerial != 0) &&
        (vertex->projectionSnapshot < PSP_GFX_DL_PROJECTION_SNAPSHOTS)) {
        PspGfxDlProjectionSnapshot* oldSnapshot =
            &ctx->projectionSnapshots[vertex->projectionSnapshot];

        if (oldSnapshot->refCount != 0) {
            oldSnapshot->refCount--;
        }
    }

    if (projectionSnapshot < PSP_GFX_DL_PROJECTION_SNAPSHOTS) {
        vertex->projection = ctx->projectionSnapshots[projectionSnapshot].matrix;
        vertex->projectionSerial = ctx->projectionSerial;
        vertex->projectionSnapshot = projectionSnapshot;
    } else {
        vertex->projection = NULL;
        vertex->projectionSerial = 0;
        vertex->projectionSnapshot = PSP_GFX_DL_NO_PROJECTION_SNAPSHOT;
    }
}

static int __attribute__((used)) psp_gfx_dl_store_transformed_vertex(
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

        return 1;
    }

    if ((clip->w > -0.001f) && (clip->w < 0.001f)) {
#if PSP_GFX_DL_HOT_STATS
        ctx->stats.nearZeroWCount++;
#endif
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

    if (PSP_GFX_DL_HOT_STATS && (clip->w < 0.0f)) {
        ctx->stats.behindEyeVertexCount++;
    }

    return 1;
}

#if PSP_ORIGINAL_FOG
static void psp_gfx_dl_fog_clip_depth(const PspGfxDlFogProjection* projection,
                                      float viewZ, float viewW, float* clipZ, float* clipW) {
    *clipZ = (viewZ * projection->p22) + (viewW * projection->p32);
    *clipW = (viewZ * projection->p23) + (viewW * projection->p33);
}

static u8 psp_gfx_dl_calculate_fog_alpha(const PspGfxDlContext* ctx, const PspGfxDlVertex* vertex) {
    n64psp_fog_coefficients coefficients;
    const PspGfxDlFogProjection* projection = psp_gfx_dl_vertex_fog_projection(ctx, vertex);
    float clipZ;
    float clipW;

    if (!vertex->state.fields.valid || ((ctx->geometryMode & G_FOG) == 0) ||
        (projection == NULL)) {
        return 0;
    }

    coefficients.multiplier = ctx->fogMul;
    coefficients.offset = ctx->fogOffset;
    psp_gfx_dl_fog_clip_depth(projection, vertex->viewZ, vertex->viewW, &clipZ, &clipW);
    return n64psp_fog_alpha(&coefficients, clipZ, clipW);
}
#endif

// the only G_MTX flag bits this GBI defines
#define PSP_GFX_DL_MTX_FLAG_MASK ((u32) (G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_PUSH))

static void psp_gfx_dl_push_modelview(PspGfxDlContext* ctx) {
    if (ctx->modelviewStackDepth >= PSP_GFX_DL_MTX_STACK_DEPTH) {
        ctx->stats.mtxStackRejected++;
        return;
    }

    psp_gfx_dl_mtx_copy(ctx->modelviewStack[ctx->modelviewStackDepth], ctx->modelview);
    ctx->modelviewStackDepth++;
    ctx->stats.mtxPushCount++;
    if (ctx->modelviewStackDepth > ctx->stats.mtxMaxStackDepth) {
        ctx->stats.mtxMaxStackDepth = ctx->modelviewStackDepth;
    }
}

static void psp_gfx_dl_note_matrix_changed(PspGfxDlContext* ctx, int projection) {
    if (projection) {
        psp_gfx_dl_bump_serial(&ctx->projectionSerial);
        ctx->currentProjectionSnapshot = PSP_GFX_DL_NO_PROJECTION_SNAPSHOT;
        psp_gfx_dl_mark_effective_fog_dirty(ctx);
    } else {
        psp_gfx_dl_bump_serial(&ctx->modelviewSerial);
    }
}

static void psp_gfx_dl_apply_depth_clamp_projection(float matrix[4][4]) {
    const float nearPlane = 2.0f;
    const float farPlane = 12800.0f;

    if ((fabsf(matrix[2][3] + 1.0f) < 0.0001f) &&
        (fabsf(matrix[3][3]) < 0.0001f) &&
        (fabsf(matrix[2][2] + 1.0015637f) < 0.0001f) &&
        (fabsf(matrix[3][2] + 20.015638f) < 0.001f)) {
        matrix[2][2] = (nearPlane + farPlane) / (nearPlane - farPlane);
        matrix[3][2] = (2.0f * nearPlane * farPlane) / (nearPlane - farPlane);
    }
}

static void psp_gfx_dl_handle_mtx_generic(PspGfxDlContext* ctx, const void* src, u32 flags, int floating) {
    float loaded[4][4];
    float (*target)[4];
    int* hasTarget;

    if ((flags & G_MTX_PROJECTION) != 0) {
        target = ctx->projection;
        hasTarget = &ctx->hasProjection;
    } else {
        target = ctx->modelview;
        hasTarget = &ctx->hasModelview;
        if ((flags & G_MTX_PUSH) != 0) {
            psp_gfx_dl_push_modelview(ctx);
        }
    }
    PspProfiler_CountMatrixCommand((flags & G_MTX_PROJECTION) != 0,
                                   ((flags & G_MTX_LOAD) == 0) && *hasTarget);

    if (floating) {
        psp_gfx_dl_mtx_copy(loaded, (const float (*)[4]) src);
    } else {
        psp_gfx_dl_mtx_l2f(loaded, (const Mtx*) src);
    }
    if ((flags & G_MTX_PROJECTION) != 0) {
        if (((flags & G_MTX_LOAD) != 0) || !*hasTarget) {
            psp_gfx_dl_mtx_copy(ctx->fogProjection, loaded);
        } else {
            psp_gfx_dl_mtx_mul(ctx->fogProjection, loaded, ctx->fogProjection);
        }
        psp_gfx_dl_mtx_copy(target, ctx->fogProjection);
    } else
    if (((flags & G_MTX_LOAD) != 0) || !*hasTarget) {
        psp_gfx_dl_mtx_copy(target, loaded);
    } else {
        psp_gfx_dl_mtx_mul(target, loaded, target);
    }
    if ((flags & G_MTX_PROJECTION) != 0) {
        psp_gfx_dl_apply_depth_clamp_projection(target);
    }
    *hasTarget = 1;
    psp_gfx_dl_note_matrix_changed(ctx, (flags & G_MTX_PROJECTION) != 0);
}

static void psp_gfx_dl_handle_mtx(PspGfxDlContext* ctx, const Gfx* gfx, int floating) {
    const void* src = psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
    u32 flags = (gfx->words.w0 >> 16) & 0xFF;
    float decoded[4][4];
    const float (*loaded)[4];
    float (*target)[4];
    int* hasTarget;
    int projection;
    int load;

    ctx->stats.mtxCount++;
    if (floating) {
        ctx->stats.mtxFloatCount++;
    }
    ctx->matrixFlagsSeen |= flags;
    if ((src == NULL) || (floating && ((((uintptr_t) src) & 0xF) != 0))) {
        ctx->stats.matrixPointerRejected++;
        return;
    }

    if ((flags & ~PSP_GFX_DL_MTX_FLAG_MASK) != 0) {
        ctx->stats.mtxUnexpectedFlags++;
        psp_gfx_dl_handle_mtx_generic(ctx, src, flags, floating);
        return;
    }

    projection = (flags & G_MTX_PROJECTION) != 0;
    load = (flags & G_MTX_LOAD) != 0;
    if (projection) {
        target = ctx->projection;
        hasTarget = &ctx->hasProjection;
    } else {
        target = ctx->modelview;
        hasTarget = &ctx->hasModelview;
        if ((flags & G_MTX_PUSH) != 0) {
            psp_gfx_dl_push_modelview(ctx);
        }
    }
    PspProfiler_CountMatrixCommand(projection, !load && *hasTarget);

    if (floating) {
        loaded = (const float (*)[4]) src;
    } else {
        psp_gfx_dl_mtx_l2f(decoded, (const Mtx*) src);
        loaded = decoded;
    }
    if (projection) {
        if (load || !*hasTarget) {
            psp_gfx_dl_mtx_copy(ctx->fogProjection, loaded);
        } else {
            psp_gfx_dl_mtx_mul(ctx->fogProjection, loaded, ctx->fogProjection);
        }
        psp_gfx_dl_mtx_copy(target, ctx->fogProjection);
    } else
    if (load || !*hasTarget) {
        psp_gfx_dl_mtx_copy(target, loaded);
    } else {
        psp_gfx_dl_mtx_mul(target, loaded, target);
    }
    if (projection) {
        psp_gfx_dl_apply_depth_clamp_projection(target);
    }
    *hasTarget = 1;
    psp_gfx_dl_note_matrix_changed(ctx, projection);
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
    psp_gfx_dl_bump_serial(&ctx->modelviewSerial);
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

static void psp_gfx_dl_stage_ambient_light(const PspGfxDlContext* ctx) {
    sPspGfxDlLightingAmbient.x = (float) ctx->ambientR;
    sPspGfxDlLightingAmbient.y = (float) ctx->ambientG;
    sPspGfxDlLightingAmbient.z = (float) ctx->ambientB;
    sPspGfxDlLightingAmbient.w = 0.0f;
}

static void psp_gfx_dl_stage_directional_light(const PspGfxDlContext* ctx, u32 index) {
    sPspGfxDlLightingLights[index].direction.x = ctx->lights[index].x;
    sPspGfxDlLightingLights[index].direction.y = ctx->lights[index].y;
    sPspGfxDlLightingLights[index].direction.z = ctx->lights[index].z;
    sPspGfxDlLightingLights[index].direction.w = 0.0f;
    sPspGfxDlLightingLights[index].color.x = (float) ctx->lights[index].r;
    sPspGfxDlLightingLights[index].color.y = (float) ctx->lights[index].g;
    sPspGfxDlLightingLights[index].color.z = (float) ctx->lights[index].b;
    sPspGfxDlLightingLights[index].color.w = 0.0f;
}

static int psp_gfx_dl_lights_match(const PspGfxDlLight* a, const PspGfxDlLight* b) {
    return (a->r == b->r) &&
           (a->g == b->g) &&
           (a->b == b->b) &&
           (a->x == b->x) &&
           (a->y == b->y) &&
           (a->z == b->z);
}

static void psp_gfx_dl_stage_grouped_light(
    n64psp_directional_lightf* dst,
    const PspGfxDlLight* src,
    float scale
) {
    dst->direction.x = src->x;
    dst->direction.y = src->y;
    dst->direction.z = src->z;
    dst->direction.w = 0.0f;
    dst->color.x = (float) src->r * scale;
    dst->color.y = (float) src->g * scale;
    dst->color.z = (float) src->b * scale;
    dst->color.w = 0.0f;
}

static void psp_gfx_dl_prepare_effective_lights(PspGfxDlContext* ctx) {
    if (!ctx->lightingStateDirty) {
        return;
    }

    ctx->groupedLightCount = 0;
    if ((ctx->lightCount == 7) &&
        psp_gfx_dl_lights_match(&ctx->lights[0], &ctx->lights[1]) &&
        psp_gfx_dl_lights_match(&ctx->lights[0], &ctx->lights[2]) &&
        psp_gfx_dl_lights_match(&ctx->lights[0], &ctx->lights[3]) &&
        psp_gfx_dl_lights_match(&ctx->lights[4], &ctx->lights[5]) &&
        psp_gfx_dl_lights_match(&ctx->lights[4], &ctx->lights[6])) {
        psp_gfx_dl_stage_grouped_light(
            &sPspGfxDlGroupedLightingLights[0],
            &ctx->lights[0],
            4.0f
        );
        ctx->groupedLightCount = 1;
        if ((ctx->lights[4].r | ctx->lights[4].g | ctx->lights[4].b) != 0) {
            psp_gfx_dl_stage_grouped_light(
                &sPspGfxDlGroupedLightingLights[1],
                &ctx->lights[4],
                3.0f
            );
            ctx->groupedLightCount = 2;
        }
    }
    ctx->lightingStateDirty = 0;
}


static void psp_gfx_dl_load_ambient_light(PspGfxDlContext* ctx, const Light* src) {
    ctx->ambientR = src->l.col[0];
    ctx->ambientG = src->l.col[1];
    ctx->ambientB = src->l.col[2];
}

#if PROFILE_COMPONENTS
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
        ctx->lightingStateDirty = 1;
        psp_gfx_dl_stage_directional_light(ctx, lightSlot);
    } else if (lightSlot == ctx->lightCount) {
        psp_gfx_dl_load_ambient_light(ctx, light);
        psp_gfx_dl_stage_ambient_light(ctx);
    }
}

/*
 * Backdrop seam weld. The sky panorama (Background_DrawBackdrop) draws the
 * same backdrop DL twice, at x-translations differing by exactly one
 * panorama period; the two instances' abutting edge vertices are
 * mathematically equal but reach the GE through different float expressions
 * (edge = +x*m00 + t1 vs -x*m00 + t2), which differ by ~1 ulp (~1/16 px on
 * screen). The N64 RSP quantizes all screen coordinates to s13.2
 * quarter-pixels, welding such edges shut; the PSP GE rasterizes full
 * floats and leaves a one-pixel unpainted column at the seam (the Corneria
 * intro's vertical streak: background fill color showing through the sky).
 * Weld: in small, flat (constant view-z), non-pretransformed batches --
 * the backdrop wrap pair's signature -- snap any vertex within ~1/8 px
 * screen tolerance of an earlier vertex onto that vertex. UVs are left
 * alone (each strip keeps sampling its own edge texel).
 */
static void psp_gfx_dl_weld_flat_batch_seams(PspGfxDlContext* ctx) {
    float minZ;
    float maxZ;
    float eps;
    u32 i;
    u32 j;

    if (ctx->batchPretransformed || (ctx->batchCount < 12) || (ctx->batchCount > 48)) {
        return;
    }
    minZ = maxZ = PSP_GFX_DL_BATCH[0].z;
    for (i = 1; i < ctx->batchCount; i++) {
        float z = PSP_GFX_DL_BATCH[i].z;

        if (z < minZ) {
            minZ = z;
        }
        if (z > maxZ) {
            maxZ = z;
        }
    }
    /* Flat and in front of the eye only (view z < 0, constant across batch). */
    if ((maxZ >= 0.0f) || ((maxZ - minZ) > (0.001f * -minZ))) {
        return;
    }
    /* ~1/8 pixel at this depth: dx_view = (1/8)/240 ndc * |z| / P00(=1.811). */
    eps = 3.0e-4f * -minZ;
    for (i = 1; i < ctx->batchCount; i++) {
        PspGfxPspglColorVertex* b = &PSP_GFX_DL_BATCH[i];

        for (j = 0; j < i; j++) {
            const PspGfxPspglColorVertex* a = &PSP_GFX_DL_BATCH[j];
            float dx = b->x - a->x;
            float dy = b->y - a->y;

            if (((dx != 0.0f) || (dy != 0.0f)) && (dx < eps) && (dx > -eps) && (dy < eps) && (dy > -eps)) {
                b->x = a->x;
                b->y = a->y;
                break;
            }
        }
    }
}

#if PSP_ORIGINAL_FOG
static int psp_gfx_dl_original_fog_eligible(int sprites, int alphaTest, int blend,
                                            int depthTest, int depthWrite, int pretransformed) {
    return !sprites && !blend && !pretransformed &&
           (!depthTest || depthWrite) &&
           (!alphaTest || (depthTest && depthWrite));
}

static u32 psp_gfx_dl_original_fog_color(const PspGfxDlContext* ctx, u8 fogAlpha) {
    if ((ctx->fogR == 0) && (ctx->fogG == 0) && (ctx->fogB == 0)) {
        fogAlpha = psp_gfx_dl_present_fog_alpha(fogAlpha);
    }
    return psp_gfx_dl_pack_rgba_u8(ctx->fogR, ctx->fogG, ctx->fogB, fogAlpha, 0);
}

static void psp_gfx_dl_original_fog_vertex(const PspGfxDlContext* ctx,
                                           const PspGfxPspglColorVertex* src,
                                           u8 fogAlpha,
                                           PspGfxPspglFogVertex* dst) {
    dst->color = psp_gfx_dl_original_fog_color(ctx, fogAlpha);
    dst->x = src->x;
    dst->y = src->y;
    dst->z = src->z;
}

static u32 psp_gfx_dl_build_original_fog_batch(PspGfxDlContext* ctx) {
    u32 i;

    if (!ctx->batchOriginalFog ||
        !psp_gfx_dl_original_fog_eligible(ctx->batchSprites, ctx->batchAlphaTest,
                                          ctx->batchBlend, ctx->batchDepthTest,
                                          ctx->batchDepthWrite, ctx->batchPretransformed)) {
        return 0;
    }
    for (i = 0; i < ctx->batchCount; i++) {
        psp_gfx_dl_original_fog_vertex(ctx, &PSP_GFX_DL_BATCH[i],
                                       PSP_GFX_DL_BATCH_FOG_ALPHA[i],
                                       &sPspGfxDlFogBatch[i]);
    }
    return ctx->batchCount;
}
#endif

static void psp_gfx_dl_flush_reason(PspGfxDlContext* ctx, PspProfileFlushReason reason) {
#if PSP_ORIGINAL_FOG
    u32 originalFogVertexCount;
#endif
#if PROFILE_COMPONENTS
    u32 ownerComponent;
    u32 ownerMask;
#endif

    if (ctx->batchCount == 0) {
#if PROFILE_COMPONENTS
        ctx->batchComponentMask = 0;
#endif
        return;
    }
#if PROFILE_COMPONENTS
    ownerMask = ctx->batchComponentMask;
    ownerComponent = psp_gfx_dl_batch_owner_component(ctx);
    PspProfiler_ComponentScopeBegin(ownerComponent);
#endif
    (void) reason;
    psp_gfx_dl_weld_flat_batch_seams(ctx);
    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_SUBMIT);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_FLUSH);
#if PSP_ORIGINAL_FOG
    originalFogVertexCount = psp_gfx_dl_build_original_fog_batch(ctx);
#endif
    if (ctx->batchSprites) {
        PspGfxPspgl_DrawColoredSprites(
            PSP_GFX_DL_BATCH, ctx->batchCount, ctx->batchTextureId, ctx->batchTextureRef,
            ctx->batchTextureEnv, ctx->batchTextureEnvColor, ctx->batchWrapS, ctx->batchWrapT,
            ctx->batchAlphaTest, ctx->batchBlend, ctx->batchPremultiplied, ctx->batchDepthTest,
            ctx->batchDepthWrite, ctx->batchFog, ctx->batchFogColor, ctx->batchFogStart, ctx->batchFogEnd,
            &ctx->batchProjection[0][0], ctx->batchProjectionSerial, ctx->batchPretransformed,
            ctx->batchPointFilter, -1);
    } else if (ctx->batchReserved) {
        PspGfxPspgl_DrawReservedColoredTriangles(
            &ctx->batchReservation, ctx->batchCount, ctx->batchTextureId, ctx->batchTextureRef,
            ctx->batchTextureEnv, ctx->batchTextureEnvColor, ctx->batchWrapS, ctx->batchWrapT,
            ctx->batchAlphaTest, ctx->batchBlend, ctx->batchPremultiplied, ctx->batchDepthTest,
            ctx->batchDepthWrite, ctx->batchFog, ctx->batchFogColor, ctx->batchFogStart, ctx->batchFogEnd,
            &ctx->batchProjection[0][0], ctx->batchProjectionSerial, ctx->batchPretransformed,
            ctx->batchPointFilter);
    } else {
        PspGfxPspgl_DrawColoredTriangles(
            PSP_GFX_DL_BATCH, ctx->batchCount, ctx->batchTextureId, ctx->batchTextureRef,
            ctx->batchTextureEnv, ctx->batchTextureEnvColor, ctx->batchWrapS, ctx->batchWrapT,
            ctx->batchAlphaTest, ctx->batchBlend, ctx->batchPremultiplied, ctx->batchDepthTest,
            ctx->batchDepthWrite, ctx->batchFog, ctx->batchFogColor, ctx->batchFogStart, ctx->batchFogEnd,
            &ctx->batchProjection[0][0], ctx->batchProjectionSerial, ctx->batchPretransformed,
            ctx->batchPointFilter);
    }
#if PSP_ORIGINAL_FOG
    if (originalFogVertexCount != 0) {
        PspGfxPspgl_DrawFogTriangles(sPspGfxDlFogBatch, originalFogVertexCount,
                                     &ctx->batchProjection[0][0], ctx->batchProjectionSerial,
                                     ctx->batchPretransformed, ctx->batchDepthTest,
                                     ctx->batchDepthWrite, ctx->batchTextureId, PSP_GFX_DL_BATCH);
        ctx->stats.originalFogDrawCount++;
        ctx->stats.originalFogTriangleCount += originalFogVertexCount / 3;
        ctx->stats.originalFogVertexBytes += originalFogVertexCount * sizeof(PspGfxPspglFogVertex);
        ctx->stats.originalFogVertexCopies += originalFogVertexCount;
    }
#endif
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_FLUSH);
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_SUBMIT);
    PspProfiler_CountBatchFlush(reason, ctx->batchCount);
    PspHwCounterProfile_CountBatchFlush((u32) reason, ctx->batchCount);
#if PROFILE_TRIVIAL_REJECTS
    if (ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectFlush(reason, ctx->batchCount);
    }
#endif
#if PROFILE_COMPONENTS
    PspProfiler_CountBatchComponentOwnership(ownerComponent, ownerMask, ctx->batchCount);
    PspProfiler_ComponentScopeEnd();
#endif
    ctx->stats.drawVertexCount += ctx->batchCount;
    ctx->batchCount = 0;
#if PROFILE_COMPONENTS
    ctx->batchComponentMask = 0;
#endif
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

// One open batch per texture material. Every non material state change (depth,
// fog, transform, scissor, colour image, immediate draws, end of task) drains
// the whole pool first, so all open slots always share that state by construction
typedef struct {
    PspGfxPspglColorVertex vertices[PSP_BATCH_POOL_VERTICES] __attribute__((aligned(16)));
#if PSP_ORIGINAL_FOG
    u8 fogAlpha[PSP_BATCH_POOL_VERTICES] __attribute__((aligned(16)));
#endif
    PspGfxPspglVertexReservation reservation;
    int reserved;
    u32 count;
    u32 textureId;
    PspGfxPspglTextureRef textureRef;
    PspGfxPspglTextureEnv textureEnv;
    u32 textureEnvColor;
    PspGfxDlCombineMode combineMode;
    u32 primitiveColor;
    u32 environmentColor;
    PspGfxPspglTextureWrap wrapS;
    PspGfxPspglTextureWrap wrapT;
    int alphaTest;
    int blend;
    int premultiplied;
    int pointFilter;
    u8 open;
} PspGfxDlBatchSlot;

static PspGfxDlBatchSlot sPspGfxDlPool[PSP_BATCH_POOL_SLOTS];
static u8 sPspGfxDlPoolOrder[PSP_BATCH_POOL_SLOTS];
static u32 sPspGfxDlPoolOpen;
static int sPspGfxDlPoolCurrent = -1;

static u32 sPspGfxDlPoolHits;
static u32 sPspGfxDlPoolOpens;
static u32 sPspGfxDlPoolEvictions;
static u32 sPspGfxDlPoolCapacityFlushes;
static u32 sPspGfxDlPoolDrained;
static u32 sPspGfxDlPoolUnpooled;
static u32 sPspGfxDlPoolPeakOpen;

static void psp_gfx_dl_pool_store(PspGfxDlContext* ctx, u32 index) {
    PspGfxDlBatchSlot* slot = &sPspGfxDlPool[index];

    slot->count = ctx->batchCount;
    slot->reservation = ctx->batchReservation;
    slot->reserved = ctx->batchReserved;
    slot->textureId = ctx->batchTextureId;
    slot->textureRef = ctx->batchTextureRef;
    slot->textureEnv = ctx->batchTextureEnv;
    slot->textureEnvColor = ctx->batchTextureEnvColor;
    slot->combineMode = ctx->batchCombineMode;
    slot->primitiveColor = ctx->batchPrimitiveColor;
    slot->environmentColor = ctx->batchEnvironmentColor;
    slot->wrapS = ctx->batchWrapS;
    slot->wrapT = ctx->batchWrapT;
    slot->alphaTest = ctx->batchAlphaTest;
    slot->blend = ctx->batchBlend;
    slot->premultiplied = ctx->batchPremultiplied;
    slot->pointFilter = ctx->batchPointFilter;
}

static void psp_gfx_dl_pool_load(PspGfxDlContext* ctx, u32 index) {
    const PspGfxDlBatchSlot* slot = &sPspGfxDlPool[index];

    ctx->batchCount = slot->count;
    ctx->batchReservation = slot->reservation;
    ctx->batchReserved = slot->reserved;
    ctx->batchTextureId = slot->textureId;
    ctx->batchTextureRef = slot->textureRef;
    ctx->batchTextureEnv = slot->textureEnv;
    ctx->batchTextureEnvColor = slot->textureEnvColor;
    ctx->batchCombineMode = slot->combineMode;
    ctx->batchPrimitiveColor = slot->primitiveColor;
    ctx->batchEnvironmentColor = slot->environmentColor;
    ctx->batchWrapS = slot->wrapS;
    ctx->batchWrapT = slot->wrapT;
    ctx->batchAlphaTest = slot->alphaTest;
    ctx->batchBlend = slot->blend;
    ctx->batchPremultiplied = slot->premultiplied;
    ctx->batchPointFilter = slot->pointFilter;
}

static void psp_gfx_dl_pool_select(PspGfxDlContext* ctx, u32 index) {
    sPspGfxDlPoolCurrent = (int) index;
    ctx->batchReservation = sPspGfxDlPool[index].reservation;
    ctx->batchReserved = sPspGfxDlPool[index].reserved;
    PSP_GFX_DL_BATCH = ctx->batchReserved ? ctx->batchReservation.vertices : sPspGfxDlPool[index].vertices;
    PSP_GFX_DL_BATCH_CAP = PSP_BATCH_POOL_VERTICES;
#if PSP_ORIGINAL_FOG
    PSP_GFX_DL_BATCH_FOG_ALPHA = sPspGfxDlPool[index].fogAlpha;
#endif
}

// geometry that may not be reordered keeps the standalone buffer
static void psp_gfx_dl_pool_use_direct(PspGfxDlContext* ctx) {
    sPspGfxDlPoolCurrent = -1;
    ctx->batchReserved = 0;
    PSP_GFX_DL_BATCH = sPspGfxDlBatch;
    PSP_GFX_DL_BATCH_CAP = PSP_GFX_DL_BATCH_VERTICES;
#if PSP_ORIGINAL_FOG
    PSP_GFX_DL_BATCH_FOG_ALPHA = sPspGfxDlBatchFogAlpha;
#endif
}

static void psp_gfx_dl_pool_park(PspGfxDlContext* ctx) {
    if (sPspGfxDlPoolCurrent < 0) {
        return;
    }
    psp_gfx_dl_pool_store(ctx, (u32) sPspGfxDlPoolCurrent);
    sPspGfxDlPoolCurrent = -1;
    ctx->batchCount = 0;
}

static void psp_gfx_dl_pool_release(u32 index) {
    u32 i;

    sPspGfxDlPool[index].open = 0;
    sPspGfxDlPool[index].count = 0;
    sPspGfxDlPool[index].reserved = 0;
    for (i = 0; i < sPspGfxDlPoolOpen; i++) {
        if (sPspGfxDlPoolOrder[i] == (u8) index) {
            for (; (i + 1) < sPspGfxDlPoolOpen; i++) {
                sPspGfxDlPoolOrder[i] = sPspGfxDlPoolOrder[i + 1];
            }
            sPspGfxDlPoolOpen--;
            return;
        }
    }
}

// emits one slot and closes it, leaving nothing selected
static void psp_gfx_dl_pool_emit(PspGfxDlContext* ctx, u32 index, PspProfileFlushReason reason) {
    psp_gfx_dl_pool_park(ctx);
    psp_gfx_dl_pool_load(ctx, index);
    psp_gfx_dl_pool_select(ctx, index);
    psp_gfx_dl_flush_reason(ctx, reason);
    psp_gfx_dl_pool_release(index);
    psp_gfx_dl_pool_use_direct(ctx);
}

// drains every open slot in the order the slots were opened
static void psp_gfx_dl_pool_drain(PspGfxDlContext* ctx, PspProfileFlushReason reason) {
    if (sPspGfxDlPoolCurrent < 0) {
        // pending standalone geometry was submitted first, so it draws first
        psp_gfx_dl_flush_reason(ctx, reason);
    } else {
        psp_gfx_dl_pool_park(ctx);
    }
    while (sPspGfxDlPoolOpen != 0) {
        psp_gfx_dl_pool_emit(ctx, sPspGfxDlPoolOrder[0], reason);
        sPspGfxDlPoolDrained++;
        PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_DRAINED);
    }
    psp_gfx_dl_pool_use_direct(ctx);
}

static int psp_gfx_dl_pool_material_matches(const PspGfxDlBatchSlot* slot, u32 textureId,
                                            PspGfxPspglTextureRef textureRef,
                                            PspGfxPspglTextureEnv textureEnv, u32 textureEnvColor,
                                            PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT,
                                            int alphaTest, int blend, int premultiplied, int pointFilter) {
    return (slot->textureId == textureId) && psp_gfx_dl_texture_ref_equal(slot->textureRef, textureRef) &&
           (slot->textureEnv == textureEnv) && (slot->textureEnvColor == textureEnvColor) &&
           (slot->wrapS == wrapS) && (slot->wrapT == wrapT) && (slot->alphaTest == alphaTest) &&
           (slot->blend == blend) && (slot->premultiplied == premultiplied) &&
           (slot->pointFilter == pointFilter);
}

#if PROFILE_PHASES
static int psp_gfx_dl_pool_material_matches_without_wrap(const PspGfxDlBatchSlot* slot, u32 textureId,
                                                         PspGfxPspglTextureRef textureRef,
                                                         PspGfxPspglTextureEnv textureEnv, u32 textureEnvColor,
                                                         int alphaTest, int blend, int premultiplied,
                                                         int pointFilter) {
    return (slot->textureId == textureId) && psp_gfx_dl_texture_ref_equal(slot->textureRef, textureRef) &&
           (slot->textureEnv == textureEnv) && (slot->textureEnvColor == textureEnvColor) &&
           (slot->alphaTest == alphaTest) && (slot->blend == blend) &&
           (slot->premultiplied == premultiplied) && (slot->pointFilter == pointFilter);
}
#endif

// takes a free slot, giving up the oldest open one when the pool is full
static u32 psp_gfx_dl_pool_acquire(PspGfxDlContext* ctx) {
    u32 i;

    for (i = 0; i < PSP_BATCH_POOL_SLOTS; i++) {
        if (!sPspGfxDlPool[i].open) {
            break;
        }
    }
    if (i == PSP_BATCH_POOL_SLOTS) {
        i = sPspGfxDlPoolOrder[0];
        psp_gfx_dl_pool_emit(ctx, i, PSP_PROFILE_FLUSH_OTHER);
        sPspGfxDlPoolEvictions++;
        PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_EVICTION);
    }

    sPspGfxDlPool[i].open = 1;
    sPspGfxDlPool[i].count = 0;
    sPspGfxDlPool[i].reserved =
        PspGfxPspgl_ReserveColoredVertices(PSP_BATCH_POOL_VERTICES, &sPspGfxDlPool[i].reservation);
    if (!sPspGfxDlPool[i].reserved) {
        PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_RESERVATION_FALLBACK);
    }
    sPspGfxDlPoolOrder[sPspGfxDlPoolOpen++] = (u8) i;
    if (sPspGfxDlPoolOpen > sPspGfxDlPoolPeakOpen) {
        sPspGfxDlPoolPeakOpen = sPspGfxDlPoolOpen;
    }
    sPspGfxDlPoolOpens++;
    PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_OPEN);
    return i;
}

// the current slot filled up, emit it and continue the same material in a new one
static void psp_gfx_dl_pool_rotate_full(PspGfxDlContext* ctx) {
    u32 index;

    if (sPspGfxDlPoolCurrent < 0) {
        return;
    }
    psp_gfx_dl_pool_emit(ctx, (u32) sPspGfxDlPoolCurrent, PSP_PROFILE_FLUSH_BUFFER_FULL);
    sPspGfxDlPoolCapacityFlushes++;
    PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_CAPACITY_FLUSH);
    index = psp_gfx_dl_pool_acquire(ctx);
    psp_gfx_dl_pool_select(ctx, index);
    ctx->batchCount = 0;
    psp_gfx_dl_pool_store(ctx, index);
}

#define psp_gfx_dl_flush_all(ctx, reason) psp_gfx_dl_pool_drain((ctx), (reason))

static void psp_gfx_dl_set_batch_sprites(PspGfxDlContext* ctx, int sprites) {
    if (ctx->batchSprites == sprites) {
        return;
    }
    psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    ctx->batchSprites = sprites;
}

static int psp_gfx_dl_texture_barrier_has_pending(const PspGfxDlContext* ctx) {
    return (ctx->batchCount != 0) || (sPspGfxDlPoolOpen != 0);
}

static void psp_gfx_dl_flush_texture_change(PspGfxDlContext* ctx, PspProfileTextureFlushSource source) {
    (void) source;
    if (psp_gfx_dl_texture_barrier_has_pending(ctx)) {
        PspProfiler_CountTextureFlushSource(source);
        PspHwCounterProfile_CountTextureBarrier((u32) source);
    }
    psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_TEXTURE_CHANGE);
}

static void psp_gfx_dl_set_batch_texture(PspGfxDlContext* ctx, u32 textureId, PspGfxPspglTextureRef textureRef,
                                         PspGfxPspglTextureEnv textureEnv, u32 textureEnvColor,
                                         PspGfxDlCombineMode combineMode, u32 primitiveColor, u32 environmentColor,
                                         PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT, int alphaTest,
                                         int blend, int premultiplied, int pointFilter) {
    // only depth ordered opaque geometry may be regrouped, anything else keeps
    // its position in the submission order
    if ((blend == 0) && (ctx->batchDepthTest != 0) && (ctx->batchDepthWrite != 0)) {
        u32 i;
        u32 index;
#if PROFILE_PHASES
        int mixedWrapVariant = 0;
#endif

        if ((sPspGfxDlPoolCurrent < 0) && (ctx->batchCount != 0)) {
            psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
        }

        for (i = 0; i < sPspGfxDlPoolOpen; i++) {
            index = sPspGfxDlPoolOrder[i];
#if PROFILE_PHASES
            if (!mixedWrapVariant &&
                psp_gfx_dl_pool_material_matches_without_wrap(&sPspGfxDlPool[index], textureId, textureRef,
                                                              textureEnv, textureEnvColor, alphaTest, blend,
                                                              premultiplied, pointFilter) &&
                ((sPspGfxDlPool[index].wrapS != wrapS) || (sPspGfxDlPool[index].wrapT != wrapT))) {
                mixedWrapVariant = 1;
                PspProfiler_CountWrapBatching(1, 0, 0, 0);
            }
#endif
            if (psp_gfx_dl_pool_material_matches(&sPspGfxDlPool[index], textureId, textureRef, textureEnv,
                                                 textureEnvColor, wrapS, wrapT, alphaTest, blend,
                                                 premultiplied, pointFilter)) {
                psp_gfx_dl_pool_park(ctx);
                psp_gfx_dl_pool_load(ctx, index);
                psp_gfx_dl_pool_select(ctx, index);
                ctx->batchCombineMode = combineMode;
                ctx->batchPrimitiveColor = primitiveColor;
                ctx->batchEnvironmentColor = environmentColor;
                sPspGfxDlPoolHits++;
                PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_HIT);
                return;
            }
        }

        psp_gfx_dl_pool_park(ctx);
        index = psp_gfx_dl_pool_acquire(ctx);
        psp_gfx_dl_pool_select(ctx, index);
        ctx->batchCount = 0;
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
        ctx->batchPointFilter = pointFilter;
        psp_gfx_dl_pool_store(ctx, index);
        return;
    }

    // unpoolable material, anything pooled was submitted earlier and draws first
    if (sPspGfxDlPoolOpen != 0) {
        psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
        sPspGfxDlPoolUnpooled++;
        PspHwCounterProfile_CountPoolEvent(PSP_HW_POOL_EVENT_UNPOOLED);
    }
    int textureIdChanged = (ctx->batchTextureId != textureId) ||
                           !psp_gfx_dl_texture_ref_equal(ctx->batchTextureRef, textureRef);
    int textureEnvChanged = ctx->batchTextureEnv != textureEnv;
    int textureEnvColorChanged = ctx->batchTextureEnvColor != textureEnvColor;
    int wrapSChanged = ctx->batchWrapS != wrapS;
    int wrapTChanged = ctx->batchWrapT != wrapT;
    int alphaTestChanged = ctx->batchAlphaTest != alphaTest;
    int blendChanged = ctx->batchBlend != blend;
    int premultipliedChanged = ctx->batchPremultiplied != premultiplied;
    int pointFilterChanged = ctx->batchPointFilter != pointFilter;

    if ((ctx->batchCount != 0) &&
        (textureIdChanged || textureEnvChanged || textureEnvColorChanged || wrapSChanged || wrapTChanged ||
         alphaTestChanged || blendChanged || premultipliedChanged || pointFilterChanged)) {
        if ((wrapSChanged || wrapTChanged) && !textureIdChanged && !textureEnvChanged &&
            !textureEnvColorChanged && !alphaTestChanged && !blendChanged && !premultipliedChanged &&
            !pointFilterChanged) {
            PspProfiler_CountWrapBatching(0, 0, 1, ctx->batchCount);
        }
        PspProfiler_CountBatchStateTransitions(textureIdChanged, textureEnvChanged || textureEnvColorChanged,
                                               wrapSChanged, wrapTChanged,
                                               alphaTestChanged, blendChanged, premultipliedChanged);
#if PROFILE_TRIVIAL_REJECTS
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
    ctx->batchPointFilter = pointFilter;
}

static void psp_gfx_dl_set_batch_depth(PspGfxDlContext* ctx, int depthTest, int depthWrite, int depthBias) {
    if (psp_gfx_dl_texture_barrier_has_pending(ctx) &&
        ((ctx->batchDepthTest != depthTest) || (ctx->batchDepthWrite != depthWrite) ||
         (ctx->batchDepthBias != depthBias))) {
#if PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            if (ctx->batchDepthTest != depthTest) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_TEST);
            }
            if (ctx->batchDepthWrite != depthWrite) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_WRITE);
            }
            if (ctx->batchDepthBias != depthBias) {
                PspProfiler_CountTrivialRejectStateTransition(PSP_PROFILE_TRIVIAL_REJECT_STATE_DEPTH_TEST);
            }
        }
#endif
        psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->batchDepthTest = depthTest;
    ctx->batchDepthWrite = depthWrite;
    ctx->batchDepthBias = depthBias;
}

static void psp_gfx_dl_set_batch_fog_resolved(PspGfxDlContext* ctx, int fog, int originalFog,
                                              const float color[4], float start, float end) {
    if (psp_gfx_dl_texture_barrier_has_pending(ctx) &&
        ((ctx->batchFog != fog) || (ctx->batchOriginalFog != originalFog) ||
         (ctx->batchFogColor[0] != color[0]) ||
         (ctx->batchFogColor[1] != color[1]) || (ctx->batchFogColor[2] != color[2]) ||
         (ctx->batchFogColor[3] != color[3]) || (ctx->batchFogStart != start) ||
         (ctx->batchFogEnd != end))) {
#if PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            PspProfiler_CountTrivialRejectStateTransition(
                PSP_PROFILE_TRIVIAL_REJECT_STATE_FOG_ENABLE_OR_PARAMETERS);
        }
#endif
        psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    }
    ctx->batchFog = fog;
    ctx->batchOriginalFog = originalFog;
    ctx->batchFogColor[0] = color[0];
    ctx->batchFogColor[1] = color[1];
    ctx->batchFogColor[2] = color[2];
    ctx->batchFogColor[3] = color[3];
    ctx->batchFogStart = start;
    ctx->batchFogEnd = end;
}

static void psp_gfx_dl_resolve_fog_values(PspGfxDlContext* ctx, int fog,
                                          const PspGfxDlFogProjection* projection,
                                          float color[4], float* start, float* end) {
    *start = 0.0f;
    *end = 0.0f;
    fog = fog && (ctx->fogMul != 0);
    color[0] = (float) ctx->fogR / 255.0f;
    color[1] = (float) ctx->fogG / 255.0f;
    color[2] = (float) ctx->fogB / 255.0f;
    color[3] = (float) ctx->fogA / 255.0f;
    if (fog) {
#if PSP_ORIGINAL_FOG
        float startNdc = -(float) ctx->fogOffset / (float) ctx->fogMul;
        float endNdc = (255.0f - (float) ctx->fogOffset) / (float) ctx->fogMul;

        *start = psp_gfx_dl_fog_distance(projection, startNdc);
        *end = psp_gfx_dl_fog_distance(projection, endNdc);
#else
        if ((ctx->fogR == 0) && (ctx->fogG == 0) && (ctx->fogB == 0)) {
            if (!psp_gfx_dl_get_black_fog_curve(projection, ctx->fogMul, ctx->fogOffset,
                                                start, end)) {
                *start = 0.0f;
                *end = 0.0f;
            }
        } else {
            float startNdc = -(float) ctx->fogOffset / (float) ctx->fogMul;
            float endNdc = (255.0f - (float) ctx->fogOffset) / (float) ctx->fogMul;

            *start = psp_gfx_dl_fog_distance(projection, startNdc) * 0.45f;
            *end = psp_gfx_dl_fog_distance(projection, endNdc) * 0.5f;
        }
#endif
        if ((*start < 0.0f) || (*end <= *start)) {
            *start = 0.0f;
            *end = 0.0f;
        }
    }
}

static int psp_gfx_dl_resolve_fog_state_values(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex,
                                               int pretransformed, float color[4], float* start, float* end) {
    int requestedFog = !pretransformed && ((ctx->otherModeL >> 30) == G_BL_CLR_FOG);

    psp_gfx_dl_resolve_fog_values(ctx, requestedFog,
                                  psp_gfx_dl_vertex_fog_projection(ctx, vertex),
                                  color, start, end);
    return requestedFog && (ctx->fogMul != 0) && (*end > *start) && (*start >= 0.0f);
}

#if PSP_RENDERER_DIAGNOSTICS
static void psp_gfx_dl_trace_fog_curve(const PspGfxDlContext* ctx,
                                       const PspGfxDlFogProjection* projection,
                                       float start, float end) {
    static const u8 samples[4] = { 8, 16, 24, 31 };
    char line[512];
    u32 used;
    u32 i;

    if ((ctx->fogMul == 0) || (end <= start)) {
        return;
    }
    used = (u32) snprintf(line, sizeof(line),
                          "[rdp-trace-fog-curve] proj=%.7f,%.7f,%.7f,%.7f gl=%.2f..%.2f",
                          projection->p22, projection->p23, projection->p32, projection->p33,
                          start, end);
    for (i = 0; i < 4; i++) {
        u32 q = samples[i];
        float alpha = (float) ((q << 3) + 4U);
        float ndcZ = (alpha - (float) ctx->fogOffset) / (float) ctx->fogMul;
        float distance = psp_gfx_dl_fog_distance(projection, ndcZ);
        float hardware = (distance - start) / (end - start);

        if (hardware < 0.0f) {
            hardware = 0.0f;
        } else if (hardware > 1.0f) {
            hardware = 1.0f;
        }
        used += (u32) snprintf(line + used, sizeof(line) - used,
                               " q%lu=%.1f:%u/%.3f", (unsigned long) q, distance,
                               (unsigned int) sPspGfxDlPresentedFogAlpha[q], hardware);
    }
    PspPlatform_LogLine(line);
}
#endif

static void psp_gfx_dl_set_batch_fog(PspGfxDlContext* ctx, int fog,
                                     const PspGfxDlFogProjection* projection) {
    float color[4];
    float start;
    float end;
    int originalFog = PSP_ORIGINAL_FOG && fog;

    psp_gfx_dl_resolve_fog_values(ctx, fog, projection, color, &start, &end);
    fog = fog && (ctx->fogMul != 0) && (end > start) && (start >= 0.0f);
    psp_gfx_dl_set_batch_fog_resolved(ctx, originalFog ? 0 : fog,
                                      originalFog, color, start, end);
}

static void psp_gfx_dl_set_batch_transform(PspGfxDlContext* ctx, int pretransformed, u32 projectionSerial,
                                           const float projection[4][4]) {
    if (psp_gfx_dl_texture_barrier_has_pending(ctx) &&
        ((ctx->batchPretransformed != pretransformed) ||
         (!pretransformed && (ctx->batchProjectionSerial != projectionSerial)))) {
#if PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            PspProfiler_CountTrivialRejectStateTransition(
                PSP_PROFILE_TRIVIAL_REJECT_STATE_TRANSFORM_OR_PROJECTION);
        }
#endif
        psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_TRANSFORM_CHANGE);
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
    // Preserve the Training backdrop coverage seam workaround
    material->pointFilter = psp_gfx_dl_effective_point_filter(ctx);
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
    depth->depthBias = psp_gfx_dl_depth_bias_enabled(ctx);
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

#if PSP_RENDERER_DIAGNOSTICS
// records the effective material tuple and returns nothing; triangles are
// attributed separately so corpus totals reconcile with stats.triangleCount
static void psp_gfx_dl_material_corpus_note_state(const PspGfxDlContext* ctx) {
    const PspGfxDlEffectiveMaterialState* material = &ctx->effectiveMaterial;
    u32 geometryMode = ctx->geometryMode & PSP_GFX_DL_MATERIAL_GEOMETRY_MASK;
    u32 textured = material->textureId != 0;
    u32 textureFormat = textured ? ctx->textureFormat : 0;
    u32 textureSize = textured ? ctx->textureSize : 0;
    u32 key;
    u32 i;

    // FNV-1a over the N64-level inputs plus the resolved booleans
    key = 2166136261U;
    key = (key ^ (u32) ctx->combineMode) * 16777619U;
    key = (key ^ ctx->otherModeH) * 16777619U;
    key = (key ^ ctx->otherModeL) * 16777619U;
    key = (key ^ geometryMode) * 16777619U;
    key = (key ^ textureFormat) * 16777619U;
    key = (key ^ textureSize) * 16777619U;
    key = (key ^ (textured | ((u32) material->textureEnv << 1) | ((u32) material->wrapS << 5) |
                  ((u32) material->wrapT << 7) | ((u32) (material->alphaTest != 0) << 9) |
                  ((u32) (material->blend != 0) << 10) | ((u32) (material->premultiplied != 0) << 11) |
                  ((u32) (material->pointFilter != 0) << 12) |
                  ((u32) (ctx->effectiveDepth.depthTest != 0) << 13) |
                  ((u32) (ctx->effectiveDepth.depthWrite != 0) << 14) |
                  ((u32) (ctx->effectiveFog.fog != 0) << 15))) *
          16777619U;

    for (i = 0; i < sPspGfxDlMaterialCorpusCount; i++) {
        if (sPspGfxDlMaterialCorpus[i].key == key) {
            sPspGfxDlMaterialCorpus[i].applyCount++;
            sPspGfxDlMaterialCorpusCurrent = i;
            return;
        }
    }

    if (sPspGfxDlMaterialCorpusCount >= PSP_GFX_DL_MATERIAL_CORPUS_ENTRIES) {
        sPspGfxDlMaterialCorpusOverflow++;
        sPspGfxDlMaterialCorpusCurrent = PSP_GFX_DL_MATERIAL_CORPUS_NONE;
        return;
    }

    i = sPspGfxDlMaterialCorpusCount++;
    sPspGfxDlMaterialCorpus[i].key = key;
    sPspGfxDlMaterialCorpus[i].combineMode = (u32) ctx->combineMode;
    sPspGfxDlMaterialCorpus[i].otherModeH = ctx->otherModeH;
    sPspGfxDlMaterialCorpus[i].otherModeL = ctx->otherModeL;
    sPspGfxDlMaterialCorpus[i].geometryMode = geometryMode;
    sPspGfxDlMaterialCorpus[i].textureFormat = textureFormat;
    sPspGfxDlMaterialCorpus[i].textureSize = textureSize;
    sPspGfxDlMaterialCorpus[i].applyCount = 1;
    sPspGfxDlMaterialCorpus[i].triangleCount = 0;
    sPspGfxDlMaterialCorpus[i].textured = (u8) textured;
    sPspGfxDlMaterialCorpus[i].textureEnv = (u8) material->textureEnv;
    sPspGfxDlMaterialCorpus[i].wrapS = (u8) material->wrapS;
    sPspGfxDlMaterialCorpus[i].wrapT = (u8) material->wrapT;
    sPspGfxDlMaterialCorpus[i].alphaTest = material->alphaTest != 0;
    sPspGfxDlMaterialCorpus[i].blend = material->blend != 0;
    sPspGfxDlMaterialCorpus[i].premultiplied = material->premultiplied != 0;
    sPspGfxDlMaterialCorpus[i].pointFilter = material->pointFilter != 0;
    sPspGfxDlMaterialCorpus[i].depthTest = ctx->effectiveDepth.depthTest != 0;
    sPspGfxDlMaterialCorpus[i].depthWrite = ctx->effectiveDepth.depthWrite != 0;
    sPspGfxDlMaterialCorpus[i].fog = ctx->effectiveFog.fog != 0;
    sPspGfxDlMaterialCorpusCurrent = i;
}

static void psp_gfx_dl_material_corpus_add_triangles(u32 count) {
    sPspGfxDlMaterialCorpusTriangles += count;
    if (sPspGfxDlMaterialCorpusCurrent < sPspGfxDlMaterialCorpusCount) {
        sPspGfxDlMaterialCorpus[sPspGfxDlMaterialCorpusCurrent].triangleCount += count;
    } else {
        sPspGfxDlMaterialCorpusUnattributed += count;
    }
}

// triangles rejected before any material was applied; never charge them to the
// previous triangle's material
static void psp_gfx_dl_material_corpus_add_rejected(u32 count) {
    sPspGfxDlMaterialCorpusTriangles += count;
    sPspGfxDlMaterialCorpusUnattributed += count;
}
#endif

static u32 psp_gfx_dl_apply_effective_batch_state(PspGfxDlContext* ctx, const PspGfxDlVertex* vertex, int pretransformed, PspGfxPspglTextureWrap wrapS,
                                                    PspGfxPspglTextureWrap wrapT) {
    int materialResolved;
    int depthResolved;
    int fogResolved;
    int resolved;

    psp_gfx_dl_resolve_effective_state(ctx, vertex, pretransformed, &materialResolved, &depthResolved, &fogResolved);
    if ((ctx->batchWrapS != wrapS) || (ctx->batchWrapT != wrapT)) {
        PspProfiler_CountWrapBatching(0, 1, 0, 0);
    }
    resolved = materialResolved || depthResolved || fogResolved || (ctx->batchWrapS != wrapS) || (ctx->batchWrapT != wrapT);
    PspProfiler_CountEffectiveState(resolved ? 1 : 0, resolved ? 0 : 1, materialResolved, depthResolved,
                                    fogResolved);
#if PSP_RENDERER_DIAGNOSTICS
    // note on every call, cache hit or miss, so triangles attribute to the material in force
    psp_gfx_dl_material_corpus_note_state(ctx);
#endif
#if PROFILE_TRIVIAL_REJECTS
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
    if (!resolved) {
        return ctx->effectiveMaterial.textureId;
    }
    psp_gfx_dl_set_batch_transform(ctx, pretransformed, vertex->projectionSerial, vertex->projection);
    psp_gfx_dl_set_batch_depth(ctx, ctx->effectiveDepth.depthTest, ctx->effectiveDepth.depthWrite,
                               ctx->effectiveDepth.depthBias);
    {
        int originalFog = PSP_ORIGINAL_FOG && !pretransformed &&
                          ((ctx->otherModeL >> 30) == G_BL_CLR_FOG);

        psp_gfx_dl_set_batch_fog_resolved(ctx,
                                          originalFog ? 0 : ctx->effectiveFog.fog,
                                          originalFog, ctx->effectiveFog.color,
                                          ctx->effectiveFog.start, ctx->effectiveFog.end);
    }
    psp_gfx_dl_set_batch_texture(ctx, ctx->effectiveMaterial.textureId, ctx->effectiveMaterial.textureRef,
                                 ctx->effectiveMaterial.textureEnv, ctx->effectiveMaterial.textureEnvColor,
                                 ctx->combineMode, psp_gfx_dl_primitive_color(ctx),
                                 psp_gfx_dl_environment_color(ctx),
                                 wrapS, wrapT,
                                 ctx->effectiveMaterial.alphaTest, ctx->effectiveMaterial.blend,
                                 ctx->effectiveMaterial.premultiplied, ctx->effectiveMaterial.pointFilter);
    (void) resolved;
    return ctx->effectiveMaterial.textureId;
}

static int psp_gfx_dl_vertex_is_valid(PspGfxDlContext* ctx, u8 index) {
    return (index < PSP_GFX_DL_MAX_VERTICES) && ctx->vertices[index].state.fields.valid;
}

#if PSP_RENDERER_DIAGNOSTICS
static u32 psp_gfx_dl_trace_hash_mix(u32 hash, u32 value) {
    hash ^= value;
    return hash * 16777619U;
}

static const char* psp_gfx_dl_trace_combine_name(PspGfxDlCombineMode mode) {
    static const char* const names[] = {
        "unknown", "shade", "primitive", "decal-rgb", "decal-rgba",
        "mod-shade-decal-a", "mod-shade-a", "mod-prim-a", "mod-shade-prim-a", "env-tex-prim-a"
    };

    return ((u32) mode < ARRAY_COUNT(names)) ? names[mode] : "invalid";
}

static const char* psp_gfx_dl_trace_wrap_name(PspGfxPspglTextureWrap wrap) {
    if (wrap == PSP_GFX_PSPGL_WRAP_CLAMP) {
        return "clamp";
    }
    return (wrap == PSP_GFX_PSPGL_WRAP_MIRROR) ? "mirror" : "repeat";
}

static u32 psp_gfx_dl_trace_state_hash(const PspGfxDlContext* ctx, int fog, float fogStart, float fogEnd) {
    union {
        float f;
        u32 u;
    } startBits, endBits;
    u32 hash = 2166136261U;

    startBits.f = fogStart;
    endBits.f = fogEnd;
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->combineMux0);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->combineMux1);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->otherModeH);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->otherModeL);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->geometryMode);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->primitiveColorRaw);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->environmentColorRaw);
    hash = psp_gfx_dl_trace_hash_mix(hash, ctx->fogColorRaw);
    hash = psp_gfx_dl_trace_hash_mix(hash, (u32) (uintptr_t) ctx->textureImage);
    hash = psp_gfx_dl_trace_hash_mix(hash, (ctx->textureFormat << 28) | (ctx->textureSize << 24) |
                                           (ctx->textureCms << 20) | (ctx->textureCmt << 18) |
                                           (ctx->textureMaskS << 12) | (ctx->textureMaskT << 8) |
                                           (ctx->textureShiftS << 4) | ctx->textureShiftT);
    hash = psp_gfx_dl_trace_hash_mix(hash, (ctx->textureWidth << 16) ^ ctx->textureHeight);
    hash = psp_gfx_dl_trace_hash_mix(hash, (u32) ctx->textureTileUls);
    hash = psp_gfx_dl_trace_hash_mix(hash, (u32) ctx->textureTileUlt);
    hash = psp_gfx_dl_trace_hash_mix(hash, (u32) fog);
    hash = psp_gfx_dl_trace_hash_mix(hash, startBits.u);
    return psp_gfx_dl_trace_hash_mix(hash, endBits.u);
}

static int psp_gfx_dl_trace_state(PspGfxDlContext* ctx, const char* kind, const Gfx* cmd, u32 depth,
                                  int force, int fog, float fogStart, float fogEnd) {
    PspGfxPspglTextureWrap wrapS;
    PspGfxPspglTextureWrap wrapT;
    u32 hash;
    char line[768];

    if (!ctx->traceActive) {
        return 0;
    }
    hash = psp_gfx_dl_trace_state_hash(ctx, fog, fogStart, fogEnd);
    if (!force && ctx->traceHasStateHash && (ctx->traceLastStateHash == hash)) {
        return 0;
    }
    ctx->traceLastStateHash = hash;
    ctx->traceHasStateHash = 1;
    if (ctx->traceRecordCount >= PSP_GFX_DL_TRACE_MAX_RECORDS) {
        ctx->traceDroppedCount++;
        return 0;
    }
    ctx->traceRecordCount++;
    wrapS = psp_gfx_dl_texture_wrap(ctx->textureCms, ctx->textureMaskS);
    wrapT = psp_gfx_dl_texture_wrap(ctx->textureCmt, ctx->textureMaskT);

    snprintf(line, sizeof(line),
             "[rdp-trace-state] task=%lu draw=%lu kind=%s depth=%lu cmd=%p mux=%06lx,%08lx combine=%s "
             "unknown=%d otherH=%08lx otherL=%08lx cycle=%lu prim=%08lx env=%08lx fogColor=%08lx "
             "alphaCmp=%lu alphaTest=%d forceBl=%d blend=%d premul=%d texA=%d blender=%04lx "
             "zGeom=%d zCmp=%d zTest=%d zWrite=%d "
             "geom=%08lx texgen=%d fog=%d fogFactor=%d,%d fogRange=%.2f..%.2f",
             (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex, kind,
             (unsigned long) depth, (const void*) cmd, (unsigned long) ctx->combineMux0,
             (unsigned long) ctx->combineMux1, psp_gfx_dl_trace_combine_name(ctx->combineMode),
             ctx->combineMode == PSP_GFX_DL_COMBINE_UNKNOWN, (unsigned long) ctx->otherModeH,
             (unsigned long) ctx->otherModeL,
             (unsigned long) ((ctx->otherModeH >> G_MDSFT_CYCLETYPE) & 3U),
             (unsigned long) ctx->primitiveColorRaw, (unsigned long) ctx->environmentColorRaw,
             (unsigned long) ctx->fogColorRaw, (unsigned long) (ctx->otherModeL & 3U),
             psp_gfx_dl_alpha_test_enabled(ctx), (ctx->otherModeL & FORCE_BL) != 0,
             psp_gfx_dl_blend_enabled(ctx), psp_gfx_dl_premultiplied_blend_enabled(ctx),
             ctx->combineUsesTextureAlpha, (unsigned long) (ctx->otherModeL >> 16),
             (ctx->geometryMode & G_ZBUFFER) != 0,
             (ctx->otherModeL & Z_CMP) != 0, (ctx->geometryMode & G_ZBUFFER) != 0,
             (ctx->otherModeL & Z_UPD) != 0, (unsigned long) ctx->geometryMode,
             (ctx->geometryMode & G_TEXTURE_GEN) != 0, fog, ctx->fogMul, ctx->fogOffset,
             fogStart, fogEnd);
    PspPlatform_LogLine(line);

    snprintf(line, sizeof(line),
             "[rdp-trace-tile] task=%lu draw=%lu image=%p enabled=%d fmt=%lu size=%lu tile=%lux%lu "
             "upload=%lux%lu+%lu,%lu origin=%ld,%ld scale=%ld,%ld "
             "S(cm=%lu clamp=%d mirror=%d mask=%lu shift=%lu ->%s) "
             "T(cm=%lu clamp=%d mirror=%d mask=%lu shift=%lu ->%s) filter=%s",
             (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex, ctx->textureImage,
             ctx->textureEnabled, (unsigned long) ctx->textureFormat, (unsigned long) ctx->textureSize,
             (unsigned long) ctx->textureWidth, (unsigned long) ctx->textureHeight,
             (unsigned long) ctx->textureUploadWidth, (unsigned long) ctx->textureUploadHeight,
             (unsigned long) ctx->textureUploadX, (unsigned long) ctx->textureUploadY,
             (long) ctx->textureTileUls, (long) ctx->textureTileUlt,
             (long) ctx->textureScaleS, (long) ctx->textureScaleT,
             (unsigned long) ctx->textureCms, (ctx->textureCms & G_TX_CLAMP) != 0,
             (ctx->textureCms & G_TX_MIRROR) != 0, (unsigned long) ctx->textureMaskS,
             (unsigned long) ctx->textureShiftS, psp_gfx_dl_trace_wrap_name(wrapS),
             (unsigned long) ctx->textureCmt, (ctx->textureCmt & G_TX_CLAMP) != 0,
             (ctx->textureCmt & G_TX_MIRROR) != 0, (unsigned long) ctx->textureMaskT,
             (unsigned long) ctx->textureShiftT, psp_gfx_dl_trace_wrap_name(wrapT),
             psp_gfx_dl_effective_point_filter(ctx) ? "point" : "bilerp");
    PspPlatform_LogLine(line);
    return 1;
}

#if PSP_ORIGINAL_FOG
static float psp_gfx_dl_fog_interpolate_affine(const PspGfxDlVertex* const vertices[3],
                                               const float weights[3]) {
    return weights[0] * vertices[0]->state.fields.fogAlpha +
           weights[1] * vertices[1]->state.fields.fogAlpha +
           weights[2] * vertices[2]->state.fields.fogAlpha;
}

static float psp_gfx_dl_fog_interpolate_perspective(const PspGfxDlVertex* const vertices[3],
                                                    const float weights[3]) {
    float weightedFog = 0.0f;
    float weightedInverseW = 0.0f;
    u32 i;

    for (i = 0; i < 3; i++) {
        float inverseW = 1.0f / vertices[i]->clipW;

        weightedFog += weights[i] * vertices[i]->state.fields.fogAlpha * inverseW;
        weightedInverseW += weights[i] * inverseW;
    }
    return weightedFog / weightedInverseW;
}

static void psp_gfx_dl_trace_fog_interpolation(PspGfxDlContext* ctx,
                                               const PspGfxDlVertex* const vertices[3]) {
    float centroidWeights[3] = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
    float nearWeights[3] = { 0.2f, 0.2f, 0.2f };
    float edgeWeights[3] = { 0.45f, 0.45f, 0.45f };
    float affine[3];
    float perspective[3];
    u32 order[3] = { 0, 1, 2 };
    u32 minAlpha = vertices[0]->state.fields.fogAlpha;
    u32 maxAlpha = minAlpha;
    u32 i;
    u32 j;
    char line[640];

    for (i = 1; i < 3; i++) {
        if (vertices[i]->state.fields.fogAlpha < minAlpha) {
            minAlpha = vertices[i]->state.fields.fogAlpha;
        }
        if (vertices[i]->state.fields.fogAlpha > maxAlpha) {
            maxAlpha = vertices[i]->state.fields.fogAlpha;
        }
    }
    if ((ctx->fogInterpolationSampleCount >= 12) || ((maxAlpha - minAlpha) < 64U)) {
        return;
    }
    for (i = 0; i < 3; i++) {
        for (j = i + 1; j < 3; j++) {
            if (vertices[order[j]]->state.fields.fogAlpha < vertices[order[i]]->state.fields.fogAlpha) {
                u32 swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
        }
    }
    nearWeights[order[0]] = 0.6f;
    edgeWeights[order[2]] = 0.1f;
    affine[0] = psp_gfx_dl_fog_interpolate_affine(vertices, centroidWeights);
    perspective[0] = psp_gfx_dl_fog_interpolate_perspective(vertices, centroidWeights);
    affine[1] = psp_gfx_dl_fog_interpolate_affine(vertices, nearWeights);
    perspective[1] = psp_gfx_dl_fog_interpolate_perspective(vertices, nearWeights);
    affine[2] = psp_gfx_dl_fog_interpolate_affine(vertices, edgeWeights);
    perspective[2] = psp_gfx_dl_fog_interpolate_perspective(vertices, edgeWeights);
    snprintf(line, sizeof(line),
             "[fog-interpolation] task=%lu draw=%lu "
             "screen=%.2f,%.2f/%.2f,%.2f/%.2f,%.2f "
             "clipW=%.6f,%.6f,%.6f fogAlpha=%u,%u,%u "
             "centroid=%.2f,%.2f,%+.2f near=%.2f,%.2f,%+.2f edge=%.2f,%.2f,%+.2f",
             (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex,
             (vertices[0]->x + 1.0f) * 240.0f, (1.0f - vertices[0]->y) * 136.0f,
             (vertices[1]->x + 1.0f) * 240.0f, (1.0f - vertices[1]->y) * 136.0f,
             (vertices[2]->x + 1.0f) * 240.0f, (1.0f - vertices[2]->y) * 136.0f,
             vertices[0]->clipW, vertices[1]->clipW, vertices[2]->clipW,
             (unsigned int) vertices[0]->state.fields.fogAlpha,
             (unsigned int) vertices[1]->state.fields.fogAlpha,
             (unsigned int) vertices[2]->state.fields.fogAlpha,
             affine[0], perspective[0], perspective[0] - affine[0],
             affine[1], perspective[1], perspective[1] - affine[1],
             affine[2], perspective[2], perspective[2] - affine[2]);
    PspPlatform_LogLine(line);
    ctx->fogInterpolationSampleCount++;
}
#endif

#if PSP_ORIGINAL_FOG
static void psp_gfx_dl_trace_fog_interpolation_if_needed(
    PspGfxDlContext* ctx, int pretransformed, const PspGfxDlVertex* const vertices[3]) {
    if (((ctx->otherModeL >> 30) == G_BL_CLR_FOG) && !pretransformed &&
        ((ctx->geometryMode & G_FOG) != 0) &&
        ((ctx->geometryMode & G_ZBUFFER) == 0) && !psp_gfx_dl_alpha_test_enabled(ctx) &&
        !psp_gfx_dl_blend_enabled(ctx)) {
        psp_gfx_dl_trace_fog_interpolation(ctx, vertices);
    }
}
#endif

static void psp_gfx_dl_trace_triangle(PspGfxDlContext* ctx, const Gfx* cmd, u32 depth, u8 a, u8 b, u8 c) {
    const PspGfxDlVertex* vertices[3];
    float fogColor[4];
    float fogStart = 0.0f;
    float fogEnd = 0.0f;
    float fogNdc[3];
    float u[3] = { 0.0f, 0.0f, 0.0f };
    float v[3] = { 0.0f, 0.0f, 0.0f };
    int pretransformed;
    int fog;
    int rdpFog;
    int force;
    char line[768];
    u32 i;

    if (!ctx->traceActive) {
        return;
    }
    ctx->traceDrawIndex++;
    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        if (psp_gfx_dl_trace_state(ctx, "tri-invalid", cmd, depth, 1, 0, 0.0f, 0.0f)) {
            snprintf(line, sizeof(line), "[rdp-trace-geom] task=%lu draw=%lu indices=%u,%u,%u invalid=1",
                     (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex, a, b, c);
            PspPlatform_LogLine(line);
        }
        return;
    }

    vertices[0] = &ctx->vertices[a];
    vertices[1] = &ctx->vertices[b];
    vertices[2] = &ctx->vertices[c];
    pretransformed = !ctx->hasProjection || (vertices[0]->projectionSerial == 0) ||
                     (vertices[0]->projectionSerial != vertices[1]->projectionSerial) ||
                     (vertices[0]->projectionSerial != vertices[2]->projectionSerial);
    fog = psp_gfx_dl_resolve_fog_state_values(ctx, vertices[0], pretransformed,
                                              fogColor, &fogStart, &fogEnd);
    rdpFog = !pretransformed && ((ctx->otherModeL >> 30) == G_BL_CLR_FOG);
    force = (ctx->combineMode == PSP_GFX_DL_COMBINE_UNKNOWN);
    for (i = 0; i < 3; i++) {
        fogNdc[i] = (vertices[i]->clipW > 0.0f) ? (vertices[i]->clipZ / vertices[i]->clipW) : 0.0f;
        force |= (vertices[i]->clipW <= 0.0f) || (vertices[i]->clipCode != 0);
        if ((ctx->textureUploadWidth != 0) && (ctx->textureUploadHeight != 0)) {
            u[i] = psp_gfx_dl_normalize_s10_5_s(ctx, vertices[i]->s, ctx->textureUploadWidth,
                                                ctx->textureTileUls);
            v[i] = psp_gfx_dl_normalize_s10_5_t(ctx, vertices[i]->t, ctx->textureUploadHeight,
                                                ctx->textureTileUlt);
        }
    }
#if PSP_ORIGINAL_FOG
    psp_gfx_dl_trace_fog_interpolation_if_needed(ctx, pretransformed, vertices);
#endif
    if (!psp_gfx_dl_trace_state(ctx, "tri", cmd, depth, force, fog, fogStart, fogEnd)) {
        return;
    }
    if (fog) {
        psp_gfx_dl_trace_fog_curve(ctx,
                                   psp_gfx_dl_vertex_fog_projection(ctx, vertices[0]),
                                   fogStart, fogEnd);
    }

    snprintf(line, sizeof(line),
             "[rdp-trace-geom] task=%lu draw=%lu indices=%u,%u,%u "
             "clip0=%.3f,%.3f,%.3f,%.3f/%02lx clip1=%.3f,%.3f,%.3f,%.3f/%02lx "
             "clip2=%.3f,%.3f,%.3f,%.3f/%02lx st=%d,%d;%d,%d;%d,%d uv=%.4f,%.4f;%.4f,%.4f;%.4f,%.4f",
             (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex, a, b, c,
             vertices[0]->clipX, vertices[0]->clipY, vertices[0]->clipZ, vertices[0]->clipW,
             (unsigned long) vertices[0]->clipCode,
             vertices[1]->clipX, vertices[1]->clipY, vertices[1]->clipZ, vertices[1]->clipW,
             (unsigned long) vertices[1]->clipCode,
             vertices[2]->clipX, vertices[2]->clipY, vertices[2]->clipZ, vertices[2]->clipW,
             (unsigned long) vertices[2]->clipCode,
             vertices[0]->s, vertices[0]->t, vertices[1]->s, vertices[1]->t, vertices[2]->s, vertices[2]->t,
             u[0], v[0], u[1], v[1], u[2], v[2]);
    PspPlatform_LogLine(line);

    snprintf(line, sizeof(line),
             "[rdp-trace-fog] task=%lu draw=%lu rspFog=%d rdpFog=%d mul=%d offset=%d "
             "zOverW=%.6f,%.6f,%.6f fogAlpha=%u,%u,%u",
             (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex,
             (ctx->geometryMode & G_FOG) != 0, rdpFog, ctx->fogMul, ctx->fogOffset,
             fogNdc[0], fogNdc[1], fogNdc[2], (unsigned int) vertices[0]->state.fields.fogAlpha,
             (unsigned int) vertices[1]->state.fields.fogAlpha,
             (unsigned int) vertices[2]->state.fields.fogAlpha);
    PspPlatform_LogLine(line);
}

static void psp_gfx_dl_trace_rectangle(PspGfxDlContext* ctx, const Gfx* cmd, u32 depth, int textured) {
    float x0;
    float y0;
    float x1;
    float y1;
    float area;
    char line[256];

    if (!ctx->traceActive) {
        return;
    }
    ctx->traceDrawIndex++;
    if (textured) {
        x0 = (float) ((cmd->words.w1 >> 12) & 0xFFF) * 0.25f;
        y0 = (float) (cmd->words.w1 & 0xFFF) * 0.25f;
        x1 = (float) ((cmd->words.w0 >> 12) & 0xFFF) * 0.25f;
        y1 = (float) (cmd->words.w0 & 0xFFF) * 0.25f;
    } else {
        x0 = (float) ((cmd->words.w1 >> 14) & 0x3FF);
        y0 = (float) ((cmd->words.w1 >> 2) & 0x3FF);
        x1 = (float) (((cmd->words.w0 >> 14) & 0x3FF) + 1U);
        y1 = (float) (((cmd->words.w0 >> 2) & 0x3FF) + 1U);
    }
    area = (x1 - x0) * (y1 - y0);
    if (!psp_gfx_dl_trace_state(ctx, textured ? "texrect" : "fillrect", cmd, depth,
                                area >= ((float) SCREEN_WIDTH * (float) SCREEN_HEIGHT * 0.5f),
                                0, 0.0f, 0.0f)) {
        return;
    }
    snprintf(line, sizeof(line),
             "[rdp-trace-rect] task=%lu draw=%lu textured=%d xy=%.2f,%.2f..%.2f,%.2f area=%.2f fill=%08lx",
             (unsigned long) ctx->taskIndex, (unsigned long) ctx->traceDrawIndex, textured,
             x0, y0, x1, y1, area, (unsigned long) ctx->fillColor);
    PspPlatform_LogLine(line);
}
#endif

#if PROFILE_TRIVIAL_REJECTS
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
        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            dst->r = 1.0f;
            dst->g = 1.0f;
            dst->b = 1.0f;
        } else {
            dst->r = (float) ctx->environmentR / 255.0f;
            dst->g = (float) ctx->environmentG / 255.0f;
            dst->b = (float) ctx->environmentB / 255.0f;
        }
        dst->a = (float) ctx->primitiveA / 255.0f;
    } else if ((ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGB) ||
               (ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGBA)) {
        dst->r = 1.0f;
        dst->g = 1.0f;
        dst->b = 1.0f;
        dst->a = (float) src->a / 255.0f;
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) {
        dst->r = (float) src->r / 255.0f;
        dst->g = (float) src->g / 255.0f;
        dst->b = (float) src->b / 255.0f;
        dst->a = 1.0f;
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
#if PSP_ORIGINAL_FOG
    dst->fogAlpha = src->state.fields.fogAlpha;
    dst->reserved = 0;
#endif
}

static void psp_gfx_dl_emit_clip_vertex(PspGfxDlContext* ctx, const PspGfxDlClipVertex* src) {
    PspGfxPspglColorVertex* dst;
    float r;
    float g;
    float b;
    float a;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_CAP) {
        if (sPspGfxDlPoolCurrent >= 0) {
            psp_gfx_dl_pool_rotate_full(ctx);
        } else
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &PSP_GFX_DL_BATCH[ctx->batchCount];
#if PSP_ORIGINAL_FOG
    PSP_GFX_DL_BATCH_FOG_ALPHA[ctx->batchCount] = src->fogAlpha;
#endif
    ctx->batchCount++;
    psp_gfx_dl_mark_batch_component(ctx);

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
    psp_gfx_dl_apply_depth_bias(ctx, &dst->z);
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
        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            *r = *g = *b = 255;
        } else {
            *r = ctx->environmentR;
            *g = ctx->environmentG;
            *b = ctx->environmentB;
        }
        *a = ctx->primitiveA;
    } else if ((ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGB) ||
               (ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGBA)) {
        *r = 255;
        *g = 255;
        *b = 255;
        *a = src->a;
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) {
        *r = src->r;
        *g = src->g;
        *b = src->b;
        *a = 255;
    } else {
        *r = src->r;
        *g = src->g;
        *b = src->b;
        *a = src->a;
    }
}

static void __attribute__((noinline))
psp_gfx_dl_build_direct_pair_colors(PspGfxDlContext* ctx, const PspGfxDlVertex* const vertices[6],
                                    PspGfxPspglColorVertex* dst) {
    u32 i;

    if ((ctx->combineMode == PSP_GFX_DL_COMBINE_PRIMITIVE) ||
        (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_PRIM_ALPHA) ||
        ((ctx->geometryMode & G_SHADE) == 0)) {
        u32 color = psp_gfx_dl_pack_rgba_u8(ctx->primitiveR, ctx->primitiveG, ctx->primitiveB,
                                            ctx->primitiveA, ctx->batchPremultiplied);

        for (i = 0; i < 6; i++) {
            dst[i].color = color;
        }
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_PRIM_ALPHA) {
        for (i = 0; i < 6; i++) {
            const PspGfxDlVertex* src = vertices[i];
            u32 r = (src->r * ctx->primitiveR) / 255U;
            u32 g = (src->g * ctx->primitiveG) / 255U;
            u32 b = (src->b * ctx->primitiveB) / 255U;

            dst[i].color = psp_gfx_dl_pack_rgba_u8(r, g, b, ctx->primitiveA, ctx->batchPremultiplied);
        }
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_ENV_TEX_PRIM_ALPHA_BLEND) {
        u32 r = ctx->environmentR;
        u32 g = ctx->environmentG;
        u32 b = ctx->environmentB;
        u32 color;

        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            r = g = b = 255;
        }
        color = psp_gfx_dl_pack_rgba_u8(r, g, b, ctx->primitiveA, ctx->batchPremultiplied);

        for (i = 0; i < 6; i++) {
            dst[i].color = color;
        }
    } else if ((ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGB) ||
               (ctx->combineMode == PSP_GFX_DL_COMBINE_DECAL_RGBA)) {
        for (i = 0; i < 6; i++) {
            dst[i].color = psp_gfx_dl_pack_rgba_u8(255, 255, 255, vertices[i]->a,
                                                   ctx->batchPremultiplied);
        }
    } else if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) {
        for (i = 0; i < 6; i++) {
            const PspGfxDlVertex* src = vertices[i];

            dst[i].color = psp_gfx_dl_pack_rgba_u8(src->r, src->g, src->b, 255, ctx->batchPremultiplied);
        }
    } else {
        for (i = 0; i < 6; i++) {
            const PspGfxDlVertex* src = vertices[i];

            dst[i].color = psp_gfx_dl_pack_rgba_u8(src->r, src->g, src->b, src->a,
                                                   ctx->batchPremultiplied);
        }
    }
}

static void psp_gfx_dl_build_direct_triangle_colors(PspGfxDlContext* ctx,
                                                    const PspGfxDlVertex* const vertices[3],
                                                    PspGfxPspglColorVertex* dst) {
    u32 i;

    for (i = 0; i < 3; i++) {
        u32 r;
        u32 g;
        u32 b;
        u32 a;

        psp_gfx_dl_vertex_color_u8(ctx, vertices[i], &r, &g, &b, &a);
        dst[i].color = psp_gfx_dl_pack_rgba_u8(r, g, b, a, ctx->batchPremultiplied);
    }
}

static void psp_gfx_dl_emit_direct_vertex(PspGfxDlContext* ctx, const PspGfxDlVertex* src, float uScale,
                                          float vScale) {
    PspGfxPspglColorVertex* dst;
    u32 r;
    u32 g;
    u32 b;
    u32 a;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_CAP) {
        if (sPspGfxDlPoolCurrent >= 0) {
            psp_gfx_dl_pool_rotate_full(ctx);
        } else
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &PSP_GFX_DL_BATCH[ctx->batchCount];
#if PSP_ORIGINAL_FOG
    PSP_GFX_DL_BATCH_FOG_ALPHA[ctx->batchCount] = src->state.fields.fogAlpha;
#endif
    ctx->batchCount++;
    psp_gfx_dl_mark_batch_component(ctx);

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
    psp_gfx_dl_apply_depth_bias(ctx, &dst->z);
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

    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_BATCH);
    if ((ctx->textureUploadWidth != 0) && (ctx->textureUploadHeight != 0)) {
        uScale = 1.0f / (32.0f * (float) ctx->textureUploadWidth);
        vScale = 1.0f / (32.0f * (float) ctx->textureUploadHeight);
    }

    psp_gfx_dl_emit_direct_vertex(ctx, a, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex(ctx, b, uScale, vScale);
    psp_gfx_dl_emit_direct_vertex(ctx, c, uScale, vScale);
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_BATCH);
}

static int psp_gfx_dl_triangle_pretransformed(const PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                              const PspGfxDlVertex* b, const PspGfxDlVertex* c) {
    return !ctx->hasProjection || (a->projectionSerial == 0) ||
           (a->projectionSerial != b->projectionSerial) ||
           (a->projectionSerial != c->projectionSerial);
}

static void psp_gfx_dl_build_direct_vertex(PspGfxDlContext* ctx, const PspGfxDlVertex* src, float uScale,
                                           float vScale, PspGfxPspglColorVertex* dst) {
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
    psp_gfx_dl_apply_depth_bias(ctx, &dst->z);
    dst->u = ((((float) src->s * (float) ctx->textureScaleS) / 65536.0f) -
              ((float) ctx->textureTileUls * 8.0f) + ((float) ctx->textureUploadX * 32.0f)) * uScale;
    dst->v = ((((float) src->t * (float) ctx->textureScaleT) / 65536.0f) -
              ((float) ctx->textureTileUlt * 8.0f) + ((float) ctx->textureUploadY * 32.0f)) * vScale;
}

static void psp_gfx_dl_emit_direct_vertex_unchecked(PspGfxDlContext* ctx, const PspGfxDlVertex* src,
                                                    float uScale, float vScale) {
    PspGfxPspglColorVertex* dst = &PSP_GFX_DL_BATCH[ctx->batchCount];

#if PSP_ORIGINAL_FOG
    PSP_GFX_DL_BATCH_FOG_ALPHA[ctx->batchCount] = src->state.fields.fogAlpha;
#endif
    ctx->batchCount++;

    psp_gfx_dl_mark_batch_component(ctx);
    psp_gfx_dl_build_direct_vertex(ctx, src, uScale, vScale, dst);
}

#if PSP_GFX_DL_HOT_STATS
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
#endif

static int psp_gfx_dl_culls_area(u32 geometryMode, float area);

static int psp_gfx_dl_try_emit_tri2_direct_pair(PspGfxDlContext* ctx, u8 a0, u8 b0, u8 c0,
                                                u8 a1, u8 b1, u8 c1) {
    const PspGfxDlVertex* va0;
    const PspGfxDlVertex* vb0;
    const PspGfxDlVertex* vc0;
    const PspGfxDlVertex* va1;
    const PspGfxDlVertex* vb1;
    const PspGfxDlVertex* vc1;
    const PspGfxDlVertex* vertices[6];
    const PspGfxDlVertex* const* emittedVertices;
    PspGfxPspglTextureWrap wrapS;
    PspGfxPspglTextureWrap wrapT;
    u8 combined0;
    u8 combined1;
    u8 shared0;
    u8 shared1;
    int pretransformed0;
    int pretransformed1;
    u32 textureId;
    float uScale = 0.0f;
    float vScale = 0.0f;
    u32 bufferPreflush = 0;
    float area0;
    float area1;
    int cull0;
    int cull1;
    int mixedCull;
    u32 emittedVertexCount;

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
    shared0 = va0->clipCode & vb0->clipCode & vc0->clipCode;
    shared1 = va1->clipCode & vb1->clipCode & vc1->clipCode;
    if ((shared0 != 0) && (shared1 != 0)) {
#if PSP_GFX_DL_HOT_STATS
        ctx->stats.sharedClipTriangleCount += 2;
        ctx->stats.clipRejectedTriangleCount += 2;
        ctx->stats.triangleCount += 2;
#endif
        PspProfiler_CountTriangleResult(0, 2, 0, 0, 0);
        PspProfiler_CountTri2DoubleTrivialFastReject();
        return 1;
    }
    combined0 = va0->clipCode | vb0->clipCode | vc0->clipCode;
    combined1 = va1->clipCode | vb1->clipCode | vc1->clipCode;
    if ((combined0 != 0) || (combined1 != 0)) {
        PspProfiler_CountTri2PairFastpath(0, 0, 1, 0, 0, 0, 0);
        return 0;
    }

    area0 = ((vb0->x - va0->x) * (vc0->y - va0->y)) - ((vb0->y - va0->y) * (vc0->x - va0->x));
    area1 = ((vb1->x - va1->x) * (vc1->y - va1->y)) - ((vb1->y - va1->y) * (vc1->x - va1->x));
    cull0 = psp_gfx_dl_culls_area(ctx->geometryMode, area0);
    cull1 = psp_gfx_dl_culls_area(ctx->geometryMode, area1);
    PspProfiler_CountTri2CullOutcome(1, !cull0 && !cull1, cull0 && !cull1, !cull0 && cull1,
                                     cull0 && cull1, 0);
    if (cull0 && cull1) {
#if PSP_GFX_DL_HOT_STATS
        ctx->stats.triangleCount += 2;
        psp_gfx_dl_count_tri2_pair_triangle_stats(ctx, va0, vb0, vc0);
        psp_gfx_dl_count_tri2_pair_triangle_stats(ctx, va1, vb1, vc1);
        if (psp_gfx_dl_triangle_pretransformed(ctx, va0, vb0, vc0)) {
            ctx->stats.pretransformedTriangleCount++;
        } else {
            ctx->stats.projectedTriangleCount++;
        }
        if (psp_gfx_dl_triangle_pretransformed(ctx, va1, vb1, vc1)) {
            ctx->stats.pretransformedTriangleCount++;
        } else {
            ctx->stats.projectedTriangleCount++;
        }
#endif
        PspProfiler_CountTriangleResult(0, 2, 0, 0, 0);
        return 1;
    }
    mixedCull = cull0 || cull1;

    pretransformed0 = psp_gfx_dl_triangle_pretransformed(ctx, va0, vb0, vc0);
    pretransformed1 = psp_gfx_dl_triangle_pretransformed(ctx, va1, vb1, vc1);
    if (pretransformed0 != pretransformed1) {
        PspProfiler_CountTri2CullMixedResult(0, mixedCull, 0, 0);
        PspProfiler_CountTri2PairFastpath(0, 0, 0, 1, 0, 0, 0);
        return 0;
    }
    if (!pretransformed0 && (va0->projectionSerial != va1->projectionSerial)) {
        PspProfiler_CountTri2CullMixedResult(0, mixedCull, 0, 0);
        PspProfiler_CountTri2PairFastpath(0, 0, 0, 1, 0, 0, 0);
        return 0;
    }
    if (ctx->textureEnabled && pretransformed0) {
        PspProfiler_CountTri2CullMixedResult(0, mixedCull, 0, 0);
        PspProfiler_CountTri2PairFastpath(0, 0, 0, 0, 1, 0, 0);
        return 0;
    }

    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    vertices[0] = va0;
    vertices[1] = vb0;
    vertices[2] = vc0;
    vertices[3] = va1;
    vertices[4] = vb1;
    vertices[5] = vc1;
    emittedVertices = cull0 ? &vertices[3] : &vertices[0];
    emittedVertexCount = mixedCull ? 3 : 6;
    wrapS = psp_gfx_dl_texture_tri6_wrap(ctx->textureCms, ctx->textureMaskS, ctx->textureUploadWidth,
                                        va0->s, vb0->s, vc0->s, va1->s, vb1->s, vc1->s,
                                        PSP_GFX_DL_ENCODED_MIRROR(ctx, ctx->textureCms, ctx->textureMaskS));
    wrapT = psp_gfx_dl_texture_tri6_wrap(ctx->textureCmt, ctx->textureMaskT, ctx->textureUploadHeight,
                                        va0->t, vb0->t, vc0->t, va1->t, vb1->t, vc1->t,
                                        PSP_GFX_DL_ENCODED_MIRROR(ctx, ctx->textureCmt, ctx->textureMaskT));
#if PROFILE_PHASES
    psp_gfx_dl_profile_mirror_texture(ctx, wrapS == PSP_GFX_PSPGL_WRAP_CLAMP,
                                      wrapT == PSP_GFX_PSPGL_WRAP_CLAMP, mixedCull ? 1 : 2);
#endif
    psp_gfx_dl_set_batch_sprites(ctx, 0);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
#if PSP_GFX_DL_HOT_STATS
    if (pretransformed0) {
        ctx->stats.pretransformedTriangleCount += 2;
    } else {
        ctx->stats.projectedTriangleCount += 2;
    }
    psp_gfx_dl_count_tri2_pair_triangle_stats(ctx, va0, vb0, vc0);
    psp_gfx_dl_count_tri2_pair_triangle_stats(ctx, va1, vb1, vc1);
#endif

    textureId = psp_gfx_dl_apply_effective_batch_state(ctx, emittedVertices[0], pretransformed0, wrapS, wrapT);

#if PSP_GFX_DL_HOT_STATS
    if (ctx->batchDepthTest) {
        ctx->stats.depthTestTriangleCount += mixedCull ? 1 : 2;
    }
    if (ctx->batchDepthWrite) {
        ctx->stats.depthWriteTriangleCount += mixedCull ? 1 : 2;
    }
    if (ctx->batchFog) {
        if (mixedCull) {
            psp_gfx_dl_count_fog_triangle_stats(ctx, emittedVertices[0], emittedVertices[1], emittedVertices[2],
                                                ctx->batchFogStart, ctx->batchFogEnd);
        } else {
            psp_gfx_dl_count_tri2_pair_fog_stats(ctx, vertices);
        }
    }
#endif
    if (ctx->batchCount + emittedVertexCount > PSP_GFX_DL_BATCH_CAP) {
        bufferPreflush = 1;
        if (sPspGfxDlPoolCurrent >= 0) {
            psp_gfx_dl_pool_rotate_full(ctx);
        } else {
            psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
        }
    }
    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_BATCH);
    if (mixedCull) {
        psp_gfx_dl_build_direct_triangle_colors(ctx, emittedVertices, &PSP_GFX_DL_BATCH[ctx->batchCount]);
    } else {
        psp_gfx_dl_build_direct_pair_colors(ctx, vertices, &PSP_GFX_DL_BATCH[ctx->batchCount]);
    }
    if (ctx->textureUploadWidth != 0) {
        uScale = 1.0f / (32.0f * (float) ctx->textureUploadWidth);
    }
    if (ctx->textureUploadHeight != 0) {
        vScale = 1.0f / (32.0f * (float) ctx->textureUploadHeight);
    }

    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, emittedVertices[0], uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, emittedVertices[1], uScale, vScale);
    psp_gfx_dl_emit_direct_vertex_unchecked(ctx, emittedVertices[2], uScale, vScale);
    if (!mixedCull) {
        psp_gfx_dl_emit_direct_vertex_unchecked(ctx, emittedVertices[3], uScale, vScale);
        psp_gfx_dl_emit_direct_vertex_unchecked(ctx, emittedVertices[4], uScale, vScale);
        psp_gfx_dl_emit_direct_vertex_unchecked(ctx, emittedVertices[5], uScale, vScale);
    }
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_BATCH);
    PspProfiler_CountTriangleResult(mixedCull ? 1 : 2, mixedCull ? 1 : 0, 0, 0, mixedCull ? 1 : 2);
    PspProfiler_CountTrianglePath(mixedCull ? 1 : 2, 0, 0, 0, emittedVertexCount);
    if (mixedCull) {
        PspProfiler_CountTri2CullMixedResult(1, 0, emittedVertexCount, bufferPreflush);
    } else {
        PspProfiler_CountTri2PairFastpath(1, 0, 0, 0, 0, bufferPreflush, 0);
    }
    (void) bufferPreflush;
#if PSP_RENDERER_DIAGNOSTICS
    psp_gfx_dl_material_corpus_add_triangles(mixedCull ? 1 : 2);
#endif
#if PSP_GFX_DL_HOT_STATS
    ctx->stats.triangleCount += 2;
    if (textureId != 0) {
        ctx->stats.texturedTriangleCount += mixedCull ? 1 : 2;
        if (ctx->batchAlphaTest) {
            ctx->stats.alphaTestTriangleCount += mixedCull ? 1 : 2;
        }
        if (ctx->batchBlend) {
            ctx->stats.blendTriangleCount += mixedCull ? 1 : 2;
        }
    }
#else
    (void) textureId;
#endif
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
    return 1;
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
#if PSP_ORIGINAL_FOG
    out->fogAlpha = n64psp_fog_alpha_lerp(from->fogAlpha, to->fogAlpha, t);
    out->reserved = 0;
#endif
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
    u32 emitted;

    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_BATCH);
    if (ctx->batchPretransformed) {
        emitted = psp_gfx_dl_emit_perspective_triangle(ctx, a, b, c, 0);
    } else {
        psp_gfx_dl_emit_clip_vertex(ctx, a);
        psp_gfx_dl_emit_clip_vertex(ctx, b);
        psp_gfx_dl_emit_clip_vertex(ctx, c);
        ctx->stats.perspectiveTriangleCount++;
        emitted = 1;
    }
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_BATCH);
    return emitted;
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

static int psp_gfx_dl_culls_area(u32 geometryMode, float area) {
    u32 cullMode = geometryMode & G_CULL_BOTH;

    if (cullMode == G_CULL_BOTH) {
        return 1;
    }
    if (area > 0.000001f) {
        return (cullMode & G_CULL_FRONT) != 0;
    }
    if (area < -0.000001f) {
        return (cullMode & G_CULL_BACK) != 0;
    }
    return 0;
}

static u32 psp_gfx_dl_emit_clipped_triangle(PspGfxDlContext* ctx, const PspGfxDlVertex* a,
                                            const PspGfxDlVertex* b, const PspGfxDlVertex* c, int textured) {
    PspGfxDlClipVertex buffers[2][PSP_GFX_DL_MAX_CLIP_VERTICES];
    PspGfxDlClipVertex* input = buffers[0];
    PspGfxDlClipVertex* output = buffers[1];
    PspGfxDlClipVertex* swap;
    u32 vertexCount = 3;
    u32 generatedCount = 0;
    u32 emittedCount = 0;
    u32 plane;
    u32 i;

    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_CLIPPING);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_CLIPPING);
    psp_gfx_dl_build_clip_vertex(ctx, a, &input[0]);
    psp_gfx_dl_build_clip_vertex(ctx, b, &input[1]);
    psp_gfx_dl_build_clip_vertex(ctx, c, &input[2]);
    for (plane = 0; plane < PSP_GFX_DL_CLIP_PLANES; plane++) {
        vertexCount = psp_gfx_dl_clip_polygon_plane(input, vertexCount, output, plane);
        if (vertexCount < 3) {
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_CLIPPING);
            PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_CLIPPING);
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

    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_CLIPPING);
    if (!textured) {
        PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_BATCH);
    }
    for (i = 1; i + 1 < vertexCount; i++) {
        float ax = input[0].x / input[0].w;
        float ay = input[0].y / input[0].w;
        float bx = input[i].x / input[i].w;
        float by = input[i].y / input[i].w;
        float cx = input[i + 1].x / input[i + 1].w;
        float cy = input[i + 1].y / input[i + 1].w;
        float area = ((bx - ax) * (cy - ay)) - ((by - ay) * (cx - ax));

        if (psp_gfx_dl_culls_area(ctx->geometryMode, area)) {
            continue;
        }
        if (textured) {
            psp_gfx_dl_emit_textured_triangle(ctx, &input[0], &input[i], &input[i + 1]);
        } else {
            psp_gfx_dl_emit_clip_vertex(ctx, &input[0]);
            psp_gfx_dl_emit_clip_vertex(ctx, &input[i]);
            psp_gfx_dl_emit_clip_vertex(ctx, &input[i + 1]);
        }
        emittedCount++;
    }
    if (!textured) {
        PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_BATCH);
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_CLIPPING);
    return emittedCount;
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
    PspGfxPspglTextureWrap wrapS;
    PspGfxPspglTextureWrap wrapT;
    int pretransformed;

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
#if PROFILE_TRIVIAL_REJECTS
    PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_GENERIC_TRI_INVOCATIONS, 1);
#endif
    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        ctx->stats.invalidTriangleCount++;
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
        return;
    }


    va = &ctx->vertices[a];
    vb = &ctx->vertices[b];
    vc = &ctx->vertices[c];
    sharedClipCode = va->clipCode & vb->clipCode & vc->clipCode;
    combinedClipCode = va->clipCode | vb->clipCode | vc->clipCode;
    if (sharedClipCode != 0) {
#if PROFILE_TRIVIAL_REJECTS
        if (ctx->trivialRejectDiagnosticActive) {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_INVALID_NESTING, 1);
        }
        ctx->trivialRejectDiagnosticActive = 1;
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_BEGINS, 1);
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_TRIANGLES, 1);
        if (ctx->batchCount == 0) {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_EMPTY_BEFORE_STATE, 1);
        } else {
            PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_NONEMPTY_BEFORE_STATE, 1);
        }
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_BATCH_VERTICES_BEFORE_STATE,
                                           ctx->batchCount);
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_EARLY_REJECT_TAKEN, 1);
#endif
        ctx->stats.sharedClipTriangleCount++;
        ctx->stats.clipRejectedTriangleCount++;
        ctx->stats.triangleCount++;
#if PSP_RENDERER_DIAGNOSTICS
        psp_gfx_dl_material_corpus_add_rejected(1);
#endif
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
#if PROFILE_TRIVIAL_REJECTS
        ctx->trivialRejectDiagnosticActive = 0;
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_SCOPE_ENDS, 1);
#endif
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
        return;
    }
    if (ctx->textureEnabled && (ctx->textureId == 0)) {
        psp_gfx_dl_prepare_texture(ctx, 1, psp_gfx_dl_premultiplied_blend_enabled(ctx));
    }
    wrapS = psp_gfx_dl_texture_tri3_wrap(ctx->textureCms, ctx->textureMaskS, ctx->textureUploadWidth,
                                        va->s, vb->s, vc->s,
                                        PSP_GFX_DL_ENCODED_MIRROR(ctx, ctx->textureCms, ctx->textureMaskS));
    wrapT = psp_gfx_dl_texture_tri3_wrap(ctx->textureCmt, ctx->textureMaskT, ctx->textureUploadHeight,
                                        va->t, vb->t, vc->t,
                                        PSP_GFX_DL_ENCODED_MIRROR(ctx, ctx->textureCmt, ctx->textureMaskT));
#if PROFILE_TRIVIAL_REJECTS
    if (ctx->trivialRejectDiagnosticActive) {
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_WRAP_RESOLVES_S, 1);
        PspProfiler_CountTrivialRejectCost(PSP_PROFILE_TRIVIAL_REJECT_COST_WRAP_RESOLVES_T, 1);
    }
#endif
#if PROFILE_PHASES
    psp_gfx_dl_profile_mirror_texture(ctx, wrapS == PSP_GFX_PSPGL_WRAP_CLAMP,
                                      wrapT == PSP_GFX_PSPGL_WRAP_CLAMP, 1);
#endif
    psp_gfx_dl_set_batch_sprites(ctx, 0);
    pretransformed = !ctx->hasProjection || (va->projectionSerial == 0) ||
                     (va->projectionSerial != vb->projectionSerial) ||
                     (va->projectionSerial != vc->projectionSerial);
    if (pretransformed) {
        ctx->stats.pretransformedTriangleCount++;
    } else {
        ctx->stats.projectedTriangleCount++;
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

    if ((combinedClipCode == 0) && psp_gfx_dl_culls_area(ctx->geometryMode, area)) {
        ctx->stats.triangleCount++;
        PspProfiler_CountTriangleResult(0, 1, 0, 0, 0);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_BATCH_CONSTRUCTION);
        return;
    }
    textureId = psp_gfx_dl_apply_effective_batch_state(ctx, va, pretransformed, wrapS, wrapT);
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
        if ((textureId == 0) || !pretransformed) {
            psp_gfx_dl_emit_direct_triangle(ctx, va, vb, vc);
            emittedTriangles = 1;
            PspProfiler_CountTrianglePath(1, 0, 0, 0, 3);
        } else
        {
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
            PspProfiler_CountTrianglePath(0, 1, ((textureId != 0) && pretransformed) ? 1 : 0, 0, 0);
        }
        PspProfiler_CountTriangleResult(1, 0, 0, 0, 1);
    }
    ctx->stats.triangleCount++;
#if PSP_RENDERER_DIAGNOSTICS
    psp_gfx_dl_material_corpus_add_triangles(1);
#endif
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

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_CAP) {
        if (sPspGfxDlPoolCurrent >= 0) {
            psp_gfx_dl_pool_rotate_full(ctx);
        } else
        psp_gfx_dl_flush_reason(ctx, PSP_PROFILE_FLUSH_BUFFER_FULL);
    }

    dst = &PSP_GFX_DL_BATCH[ctx->batchCount];
#if PSP_ORIGINAL_FOG
    PSP_GFX_DL_BATCH_FOG_ALPHA[ctx->batchCount] = 0;
#endif
    ctx->batchCount++;
    psp_gfx_dl_mark_batch_component(ctx);
    dst->u = psp_gfx_dl_normalize_texel_coord(u, ctx->textureUploadWidth, ctx->textureUploadX,
                                             ctx->textureTileUls);
    dst->v = psp_gfx_dl_normalize_texel_coord(v, ctx->textureUploadHeight, ctx->textureUploadY,
                                             ctx->textureTileUlt);

    r = ctx->primitiveR;
    g = ctx->primitiveG;
    b = ctx->primitiveB;
    a = ctx->primitiveA;
    if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
        r = g = b = 255;
    }

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
    int sprites;

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

    sprites = !flip && (ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_4b) &&
              (ctx->textureWidth == 16) && (ctx->textureHeight == 13);
    psp_gfx_dl_set_batch_sprites(ctx, sprites);

    if (sprites) {
        psp_gfx_dl_set_batch_depth(ctx, 0, 0, 0);
        psp_gfx_dl_set_batch_fog(ctx, 0, NULL);
        psp_gfx_dl_set_batch_transform(ctx, 1, 0, NULL);
    }
    psp_gfx_dl_set_batch_texture(
        ctx, ctx->textureId, ctx->textureRef, PSP_GFX_PSPGL_TEX_MODULATE,
        0, ctx->combineMode, psp_gfx_dl_primitive_color(ctx), psp_gfx_dl_environment_color(ctx),
        PSP_GFX_DL_ENCODED_MIRROR(ctx, ctx->textureCms, ctx->textureMaskS) ? PSP_GFX_PSPGL_WRAP_REPEAT :
        psp_gfx_dl_texture_draw_wrap(
            ctx->textureCms, ctx->textureMaskS,
            (s0 < 0.0f) || (s1 < 0.0f) || (s0 > (float) ctx->textureUploadWidth) ||
                (s1 > (float) ctx->textureUploadWidth)),
        PSP_GFX_DL_ENCODED_MIRROR(ctx, ctx->textureCmt, ctx->textureMaskT) ? PSP_GFX_PSPGL_WRAP_REPEAT :
        psp_gfx_dl_texture_draw_wrap(
            ctx->textureCmt, ctx->textureMaskT,
            (t0 < 0.0f) || (t1 < 0.0f) || (t0 > (float) ctx->textureUploadHeight) ||
                (t1 > (float) ctx->textureUploadHeight)),
        psp_gfx_dl_alpha_test_enabled(ctx), psp_gfx_dl_blend_enabled(ctx),
        psp_gfx_dl_premultiplied_blend_enabled(ctx), psp_gfx_dl_effective_point_filter(ctx));
    if (!sprites) {
        psp_gfx_dl_set_batch_depth(ctx, 0, 0, 0);
        psp_gfx_dl_set_batch_fog(ctx, 0, NULL);
        psp_gfx_dl_set_batch_transform(ctx, 1, 0, NULL);
    }
    psp_gfx_dl_mark_effective_state_dirty(ctx);

    if (sprites) {
        psp_gfx_dl_emit_rect_vertex(ctx, x0, y0, s0, t0);
        psp_gfx_dl_emit_rect_vertex(ctx, x1, y1, s1, t1);
    } else if (flip) {
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

#if PSP_RENDERER_DIAGNOSTICS
    ctx->primitiveColorRaw = gfx->words.w1;
#endif

    int rgbChanged = (ctx->primitiveR != r) || (ctx->primitiveG != g) || (ctx->primitiveB != b);

    if (rgbChanged || (ctx->primitiveA != a)) {
        psp_gfx_dl_mark_effective_material_dirty(ctx);
        if (rgbChanged && psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
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

#if PSP_RENDERER_DIAGNOSTICS
    ctx->environmentColorRaw = gfx->words.w1;
#endif

    int rgbChanged = (ctx->environmentR != r) || (ctx->environmentG != g) || (ctx->environmentB != b);

    if (rgbChanged || (ctx->environmentA != a)) {
        psp_gfx_dl_mark_effective_material_dirty(ctx);
        if (rgbChanged && psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
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
        u8 alpha = ctx->primitiveA;

        if (((ctx->otherModeL & FORCE_BL) != 0) && (alpha != 255)) {
            alpha = psp_gfx_color_transfer_u8(alpha);
        }
        color = psp_gfx_dl_pack_rgba_u8(ctx->primitiveR, ctx->primitiveG, ctx->primitiveB,
                                        alpha, 0);
        blend = ((ctx->otherModeL & FORCE_BL) != 0) && (ctx->primitiveA != 255);
        ctx->stats.fillRectanglePrimitiveColorCount++;
    } else if (!psp_gfx_dl_is_fill_cycle(ctx)) {
        ctx->stats.fillRectangleUnsupportedCount++;
    }

    if (!ctx->colorImageIsDisplay) {
#if PSP_RENDERER_DIAGNOSTICS
        if (ctx->traceActive && activeBackgroundRect) {
            psp_gfx_dl_log_active_background_fill(ctx, color, primitiveFill, blend);
        }
#endif
        ctx->stats.fillRectangleCount++;
        return;
    }

    psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive && activeBackgroundRect) {
        psp_gfx_dl_log_active_background_fill(ctx, color, primitiveFill, blend);
    }
#endif
    if (primitiveFill && blend && activeBackgroundRect && !sPspGfxDlBackgroundFeedbackPrimed) {
        PspGfxPspgl_DrawSolidRect(ulx, uly, lrx, lry, sPspGfxDlBackgroundFeedbackSeedColor, 0, 0);
        sPspGfxDlBackgroundFeedbackPrimed = 1;
    }
    PspGfxPspgl_DrawSolidRect(ulx, uly, lrx, lry, color, blend, 0);
    if (psp_gfx_dl_is_fill_cycle(ctx) && activeBackgroundRect) {
        sPspGfxDlBackgroundFeedbackSeedColor = ctx->fillColor | 0xFF000000u;
        sPspGfxDlBackgroundFeedbackPrimed = 0;
    }
    ctx->stats.fillRectangleCount++;
}

static void psp_gfx_dl_handle_set_scissor(PspGfxDlContext* ctx, const Gfx* gfx) {
    float ulx = (float) ((gfx->words.w0 >> 12) & 0xFFF) * 0.25f;
    float uly = (float) (gfx->words.w0 & 0xFFF) * 0.25f;
    float lrx = (float) ((gfx->words.w1 >> 12) & 0xFFF) * 0.25f;
    float lry = (float) (gfx->words.w1 & 0xFFF) * 0.25f;

    psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
    PspGfxPspgl_SetScissor(ulx, uly, lrx, lry);
}

static void psp_gfx_dl_handle_set_fog_color(PspGfxDlContext* ctx, const Gfx* gfx) {
    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
#if PSP_RENDERER_DIAGNOSTICS
    ctx->fogColorRaw = gfx->words.w1;
#endif
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

#if PSP_RENDERER_DIAGNOSTICS
    ctx->combineMux0 = mux0;
    ctx->combineMux1 = mux1;
#endif

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
    } else if ((psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_PRIMITIVE, G_CCMUX_ENVIRONMENT,
                                                  G_CCMUX_TEXEL0, G_CCMUX_ENVIRONMENT, G_ACMUX_TEXEL0,
                                                  G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0) &&
                psp_gfx_dl_combine_cycle1_matches(mux0, mux1, G_CCMUX_PRIMITIVE, G_CCMUX_ENVIRONMENT,
                                                  G_CCMUX_TEXEL0, G_CCMUX_ENVIRONMENT, G_ACMUX_TEXEL0,
                                                  G_ACMUX_0, G_ACMUX_PRIMITIVE, G_ACMUX_0)) ||
               (psp_gfx_dl_combine_cycle0_matches(mux0, mux1, G_CCMUX_PRIMITIVE, G_CCMUX_ENVIRONMENT,
                                                  G_CCMUX_TEXEL0, G_CCMUX_ENVIRONMENT, G_ACMUX_PRIMITIVE,
                                                  G_ACMUX_ENVIRONMENT, G_ACMUX_TEXEL0, G_ACMUX_ENVIRONMENT) &&
                psp_gfx_dl_combine_cycle1_matches(mux0, mux1, G_CCMUX_PRIMITIVE, G_CCMUX_ENVIRONMENT,
                                                  G_CCMUX_TEXEL0, G_CCMUX_ENVIRONMENT, G_ACMUX_PRIMITIVE,
                                                  G_ACMUX_ENVIRONMENT, G_ACMUX_TEXEL0, G_ACMUX_ENVIRONMENT))) {
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

#if PSP_RENDERER_DIAGNOSTICS && PSP_ORIGINAL_FOG
static void psp_gfx_dl_note_fog_transform_sample(PspGfxDlContext* ctx, const Vtx* src,
                                                 const PspGfxDlVertex* out) {
    static const u8 targets[3] = { 32, 128, 224 };
    u32 i;

    if (!ctx->traceActive || !out->state.fields.valid || ((ctx->geometryMode & G_FOG) == 0)) {
        return;
    }
    for (i = 0; i < 3; i++) {
        PspGfxDlFogTransformSample* sample = &ctx->fogTransformSamples[i];
        u32 distance = out->state.fields.fogAlpha > targets[i]
                           ? out->state.fields.fogAlpha - targets[i]
                           : targets[i] - out->state.fields.fogAlpha;

        if (sample->valid && (sample->distance <= distance)) {
            continue;
        }
        sample->valid = 1;
        sample->distance = distance;
        sample->objectX = src->v.ob[0];
        sample->objectY = src->v.ob[1];
        sample->objectZ = src->v.ob[2];
        psp_gfx_dl_mtx_copy(sample->modelview, ctx->alignedMatrices.modelview.m);
        psp_gfx_dl_mtx_copy(sample->projection, ctx->alignedMatrices.projection.m);
        sample->view[0] = out->viewX;
        sample->view[1] = out->viewY;
        sample->view[2] = out->viewZ;
        sample->view[3] = out->viewW;
        sample->clip[0] = out->clipX;
        sample->clip[1] = out->clipY;
        sample->clip[2] = out->clipZ;
        sample->clip[3] = out->clipW;
        sample->fogMul = ctx->fogMul;
        sample->fogOffset = ctx->fogOffset;
        sample->fogAlpha = out->state.fields.fogAlpha;
    }
}
#endif

static void psp_gfx_dl_handle_vtx(PspGfxDlContext* ctx, const Gfx* gfx) {
    const Vtx* src = (const Vtx*) psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);
    u32 w0 = gfx->words.w0;
    u32 count;
    u32 projectionSnapshot;
    s32 v0;
    u32 i;
    u64 phaseStartUs;
    const n64psp_directional_lightf* lightingLights;
    u32 lightingLightCount;
    n64psp_tnl_output_streams directOutput;

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

    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_VERTEX);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_G_VTX);
    PspProfiler_CountGvtx(count, (ctx->geometryMode & G_LIGHTING) != 0);

    lightingLights = sPspGfxDlLightingLights;
    lightingLightCount = ctx->lightCount;
    if ((ctx->geometryMode & G_LIGHTING) != 0) {
        psp_gfx_dl_prepare_effective_lights(ctx);
        if (ctx->groupedLightCount != 0) {
            lightingLights = sPspGfxDlGroupedLightingLights;
            lightingLightCount = ctx->groupedLightCount;
        }
    }

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

    phaseStartUs = PspProfiler_RenderPhaseBegin();
    {

        PspProfiler_RenderPhaseEnd(PSP_PROFILE_PHASE_G_VTX_UNPACK, phaseStartUs);
        phaseStartUs = PspProfiler_RenderPhaseBegin();
        psp_gfx_dl_prepare_batch_matrices(ctx);
        if ((ctx->geometryMode & (G_LIGHTING | G_TEXTURE_GEN)) ==
            (G_LIGHTING | G_TEXTURE_GEN)) {
            n64psp_texgen_snorm8_batch(
                sPspGfxDlTexgenOutput,
                &ctx->alignedMatrices.modelview,
                src,
                (ctx->geometryMode & G_TEXTURE_GEN_LINEAR) != 0
                    ? N64PSP_TEXGEN_LINEAR
                    : N64PSP_TEXGEN_SPHERICAL,
                count
            );
        }
        projectionSnapshot = psp_gfx_dl_prepare_vertex_projection(ctx, count);
        PspProfiler_RenderPhaseEnd(PSP_PROFILE_PHASE_G_VTX_MATRIX_PREPARE, phaseStartUs);
    }

    phaseStartUs = PspProfiler_RenderPhaseBegin();
    {
        PspGfxDlVertex* firstOutput = &ctx->vertices[v0];

        directOutput.view = &firstOutput->viewX;
        directOutput.clip = &firstOutput->clipX;
        directOutput.projected = &firstOutput->x;
        directOutput.lighting = sPspGfxDlLightingOutput;
        directOutput.clip_code = &firstOutput->clipCode;
        directOutput.valid = &firstOutput->state.raw;
        directOutput.vertex_stride = sizeof(*firstOutput);
        directOutput.lighting_stride = sizeof(sPspGfxDlLightingOutput[0]);

        if ((ctx->geometryMode & G_LIGHTING) != 0) {
            n64psp_tnl_transform_project_light_packed_batch(
                &directOutput,
                &ctx->alignedMatrices,
                src,
                lightingLightCount != 0 ? lightingLights : NULL,
                &sPspGfxDlLightingAmbient,
                lightingLightCount,
                ctx->hasProjection,
                count
            );
        } else {
            n64psp_tnl_transform_project_packed_batch(
                &directOutput,
                &ctx->alignedMatrices,
                src,
                ctx->hasProjection,
                count
            );
        }
    }
    PspProfiler_RenderPhaseEnd(PSP_PROFILE_PHASE_G_VTX_TRANSFORM, phaseStartUs);

    phaseStartUs = PspProfiler_RenderPhaseBegin();
    {
        for (i = 0; i < count; i++) {
            PspGfxDlVertex* out = &ctx->vertices[v0 + i];

            psp_gfx_dl_set_vertex_projection(ctx, out, projectionSnapshot);


#if PSP_ORIGINAL_FOG
            out->state.fields.fogAlpha = psp_gfx_dl_calculate_fog_alpha(ctx, out);
#endif

#if PSP_RENDERER_DIAGNOSTICS && PSP_ORIGINAL_FOG
            psp_gfx_dl_note_fog_transform_sample(ctx, &src[i], out);
#endif

#if PSP_GFX_DL_HOT_STATS
            if (!out->state.fields.valid) {
                ctx->stats.invalidVertexCount++;
            }
#endif
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
            else {
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
#endif
        }
    }
    PspProfiler_RenderPhaseEnd(PSP_PROFILE_PHASE_G_VTX_POST_TRANSFORM, phaseStartUs);


    phaseStartUs = PspProfiler_RenderPhaseBegin();
    {
        for (i = 0; i < count; i++) {
            const Vtx* in = &src[i];
            PspGfxDlVertex* out = &ctx->vertices[v0 + i];

            if ((ctx->geometryMode & G_LIGHTING) != 0) {
                float r;
                float g;
                float b;

                r = sPspGfxDlLightingOutput[i].x;
                g = sPspGfxDlLightingOutput[i].y;
                b = sPspGfxDlLightingOutput[i].z;
                out->r = psp_gfx_dl_remap_lighting(r);
                out->g = psp_gfx_dl_remap_lighting(g);
                out->b = psp_gfx_dl_remap_lighting(b);
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
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
#endif
            } else {
                /* Unlit shade RGB gets the same transfer the lit path applies via
                 * psp_gfx_dl_remap_lighting(); every triangle path (direct, tri2,
                 * generic, clipped) consumes these already-transformed values. */
                out->r = psp_gfx_color_transfer_u8(in->v.cn[0]);
                out->g = psp_gfx_color_transfer_u8(in->v.cn[1]);
                out->b = psp_gfx_color_transfer_u8(in->v.cn[2]);
            }
            out->a = in->v.cn[3];
            if ((ctx->geometryMode & (G_LIGHTING | G_TEXTURE_GEN)) ==
                (G_LIGHTING | G_TEXTURE_GEN)) {
                out->s = sPspGfxDlTexgenOutput[i].s;
                out->t = sPspGfxDlTexgenOutput[i].t;
            } else {
                out->s = in->v.tc[0];
                out->t = in->v.tc[1];
            }
        }
    }
    PspProfiler_RenderPhaseEnd(PSP_PROFILE_PHASE_G_VTX_ATTRIBUTE_COPY, phaseStartUs);

#if PSP_GFX_DL_HOT_STATS || PROFILE_HW_COUNTERS
    /* Loaded vertices normalise counter captures so this add survives without hot stats */
    ctx->stats.vertexCount += count;
#endif
    PspProfiler_CountTransformWork(count,
                                   (ctx->geometryMode & G_LIGHTING) != 0 ? count : 0,
                                   (ctx->geometryMode & G_LIGHTING) != 0 ? count : 0,
                                   (ctx->geometryMode & G_LIGHTING) != 0 ? count : 0,
                                   count,
                                   ctx->hasProjection ? count : 0);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_G_VTX);
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_VERTEX);
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
    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
        ctx->fogMul = (s16) (gfx->words.w1 >> 16);
        ctx->fogOffset = (s16) gfx->words.w1;
        psp_gfx_dl_mark_effective_fog_dirty(ctx);
        return;
    }
    if ((index != G_MW_NUMLIGHT) || (offset != G_MWO_NUMLIGHT)) {
        return;
    }

    encodedCount = (gfx->words.w1 & 0x7FFFFFFF) / 32U;
    encodedCount = (encodedCount > 0) ? (encodedCount - 1) : 0;
    if (encodedCount > 7) {
        encodedCount = 7;
    }
    if (ctx->lightCount != encodedCount) {
        ctx->lightCount = encodedCount;
        ctx->lightingStateDirty = 1;
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
    if (psp_gfx_dl_texture_barrier_has_pending(ctx) &&
        (((ctx->otherModeL ^ gfx->words.w1) & mask &
          (0xC0000000U | 3U | CVG_X_ALPHA | FORCE_BL | Z_UPD)) != 0)) {
        psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
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
    if ((mask & (3U << G_MDSFT_TEXTFILT)) != 0) {
        psp_gfx_dl_mark_effective_material_dirty(ctx);
    }
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

    if (psp_gfx_dl_texture_barrier_has_pending(ctx) && (ctx->textureEnabled != enabled)) {
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
    ctx->textureUploadX = 0;
    ctx->textureUploadY = 0;
    ctx->textureUploadAttempted = 0;
    psp_gfx_dl_mark_effective_material_dirty(ctx);
}

static void psp_gfx_dl_handle_set_color_image(PspGfxDlContext* ctx, const Gfx* gfx) {
    const void* image = psp_gfx_dl_resolve_ptr(ctx, gfx->words.w1);

    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
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
    int oldMirrorS = ((ctx->textureCms & G_TX_MIRROR) != 0) && (ctx->textureMaskS != G_TX_NOMASK);
    int oldMirrorT = ((ctx->textureCmt & G_TX_MIRROR) != 0) && (ctx->textureMaskT != G_TX_NOMASK);

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
    if ((oldMirrorS != (((ctx->textureCms & G_TX_MIRROR) != 0) && (ctx->textureMaskS != G_TX_NOMASK))) ||
        (oldMirrorT != (((ctx->textureCmt & G_TX_MIRROR) != 0) && (ctx->textureMaskT != G_TX_NOMASK)))) {
        ctx->textureId = 0;
    }
#if PSP_RENDERER_DIAGNOSTICS
    ctx->textureShiftT = (gfx->words.w1 >> 10) & 0xF;
    ctx->textureShiftS = gfx->words.w1 & 0xF;
#endif
    ctx->textureUploadAttempted = 0;
    psp_gfx_dl_mark_effective_material_dirty(ctx);
}

static int psp_gfx_dl_prepare_texture(PspGfxDlContext* ctx, int deferred, int premultiply) {
    int result;
    int hit = 0;
    int supported = 1;
    PspHwTextureCacheClass cache = PSP_HW_TEXTURE_CACHE_COUNT;
    const u16* palette;

    int mirrorS;
    int mirrorT;

#if PROFILE_TRIVIAL_REJECTS
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

    u32 feasibilityKey;

    mirrorS = ((ctx->textureCms & G_TX_MIRROR) != 0) && (ctx->textureMaskS != G_TX_NOMASK);
    mirrorT = ((ctx->textureCmt & G_TX_MIRROR) != 0) && (ctx->textureMaskT != G_TX_NOMASK);
    feasibilityKey = 2166136261U;
    feasibilityKey = (feasibilityKey ^ ctx->textureWidth) * 16777619U;
    feasibilityKey = (feasibilityKey ^ ctx->textureHeight) * 16777619U;
    feasibilityKey = (feasibilityKey ^ (u32) mirrorS) * 16777619U;
    feasibilityKey = (feasibilityKey ^ (u32) mirrorT) * 16777619U;
    if (feasibilityKey != ctx->textureMirrorFeasibilityKey) {
        ctx->textureMirrorFeasibilityKey = feasibilityKey;
        ctx->textureMirrorFallback = (mirrorS || mirrorT) &&
                                     !PspGfxPspgl_CanMirrorEncode(ctx->textureWidth, ctx->textureHeight,
                                                                 mirrorS, mirrorT);
        if (ctx->textureMirrorFallback) {
            PspProfiler_CountMirrorEncodedTexture(mirrorS, mirrorT, 0, 0, 0, 0, 1, 0, 0, 0);
        }
    }
    PspGfxPspgl_SetMirrorEncoding(!ctx->textureMirrorFallback && mirrorS,
                                  !ctx->textureMirrorFallback && mirrorT);

    ctx->textureUploadAttempted = 1;
    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_TEXTURE);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
    if ((ctx->textureFormat == G_IM_FMT_CI) && (ctx->textureSize == G_IM_SIZ_8b)) {
        cache = PSP_HW_TEXTURE_CACHE_CI8;
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
        cache = PSP_HW_TEXTURE_CACHE_CONVERTED;
        palette = ctx->texturePalette + (ctx->texturePaletteIndex * 16);
        hit = PspGfxPspgl_FindCi4Texture((const u8*) ctx->textureImage, palette, ctx->textureWidth,
                                         ctx->textureHeight, &ctx->textureId, &ctx->textureRef,
                                         &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                         &ctx->textureUploadX, &ctx->textureUploadY);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId = PspGfxPspgl_CreateCi4Texture((const u8*) ctx->textureImage, palette, ctx->textureWidth,
                                                          ctx->textureHeight, &ctx->textureUploadWidth,
                                                          &ctx->textureUploadHeight, &ctx->textureUploadX,
                                                          &ctx->textureUploadY, &ctx->textureRef);
        }
    } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        cache = PSP_HW_TEXTURE_CACHE_RGBA16;
        hit = PspGfxPspgl_FindRgba16Texture((const u16*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                            premultiply, &ctx->textureId, &ctx->textureRef,
                                            &ctx->textureUploadWidth, &ctx->textureUploadHeight);
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            ctx->textureId = PspGfxPspgl_CreateRgba16Texture((const u16*) ctx->textureImage, ctx->textureWidth,
                                                             ctx->textureHeight, premultiply,
                                                             &ctx->textureUploadWidth,
                                                             &ctx->textureUploadHeight, &ctx->textureRef);
        }
    } else if ((ctx->textureFormat == G_IM_FMT_RGBA) && (ctx->textureSize == G_IM_SIZ_32b)) {
        cache = PSP_HW_TEXTURE_CACHE_RGBA32;
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
        cache = PSP_HW_TEXTURE_CACHE_CONVERTED;
        if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
            hit = PspGfxPspgl_FindIa8EnvBlendTexture((const u8*) ctx->textureImage, ctx->textureWidth,
                                                     ctx->textureHeight, psp_gfx_dl_primitive_color(ctx),
                                                     psp_gfx_dl_environment_color(ctx), &ctx->textureId,
                                                     &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                                     &ctx->textureRef);
        } else if (psp_gfx_dl_soft_coverage_texture_enabled(ctx)) {
            hit = PspGfxPspgl_FindIa8SoftCoverageTexture((const u8*) ctx->textureImage, ctx->textureWidth,
                                                         ctx->textureHeight, &ctx->textureId,
                                                         &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                                         &ctx->textureRef);
        } else {
            hit = PspGfxPspgl_FindIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                                             &ctx->textureId, &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                             &ctx->textureRef);
        }
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            if (psp_gfx_dl_baked_env_blend_texture_enabled(ctx)) {
                ctx->textureId = PspGfxPspgl_CreateIa8EnvBlendTexture(
                    (const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                    psp_gfx_dl_primitive_color(ctx), psp_gfx_dl_environment_color(ctx),
                    &ctx->textureUploadWidth, &ctx->textureUploadHeight, &ctx->textureRef);
            } else if (psp_gfx_dl_soft_coverage_texture_enabled(ctx)) {
                ctx->textureId = PspGfxPspgl_CreateIa8SoftCoverageTexture(
                    (const u8*) ctx->textureImage, ctx->textureWidth, ctx->textureHeight,
                    &ctx->textureUploadWidth, &ctx->textureUploadHeight, &ctx->textureRef);
            } else {
                ctx->textureId = PspGfxPspgl_CreateIa8Texture((const u8*) ctx->textureImage, ctx->textureWidth,
                                                              ctx->textureHeight, &ctx->textureUploadWidth,
                                                              &ctx->textureUploadHeight, &ctx->textureRef);
            }
        }
    } else if ((ctx->textureFormat == G_IM_FMT_IA) && (ctx->textureSize == G_IM_SIZ_16b)) {
        cache = PSP_HW_TEXTURE_CACHE_CONVERTED;
        const u16* ia16Source = (const u16*) ctx->textureImage;
        if (psp_gfx_dl_soft_coverage_texture_enabled(ctx)) {
            hit = PspGfxPspgl_FindIa16SoftCoverageTexture(ia16Source, ctx->textureWidth, ctx->textureHeight,
                                                          &ctx->textureId, &ctx->textureUploadWidth,
                                                          &ctx->textureUploadHeight, &ctx->textureRef);
        } else {
            hit = PspGfxPspgl_FindIa16Texture(ia16Source, ctx->textureWidth, ctx->textureHeight, &ctx->textureId,
                                              &ctx->textureUploadWidth, &ctx->textureUploadHeight, &ctx->textureRef);
        }
        if (!hit) {
            psp_gfx_dl_flush_texture_change(ctx, PSP_PROFILE_TEXTURE_FLUSH_CACHE_MISS_UPLOAD);
            if (psp_gfx_dl_soft_coverage_texture_enabled(ctx)) {
                ctx->textureId = PspGfxPspgl_CreateIa16SoftCoverageTexture(
                    ia16Source, ctx->textureWidth, ctx->textureHeight, &ctx->textureUploadWidth,
                    &ctx->textureUploadHeight, &ctx->textureRef);
            } else {
                ctx->textureId = PspGfxPspgl_CreateIa16Texture(ia16Source, ctx->textureWidth, ctx->textureHeight,
                                                               &ctx->textureUploadWidth, &ctx->textureUploadHeight,
                                                               &ctx->textureRef);
            }
        }
    } else {
        supported = 0;
    }

    if (supported && (ctx->textureId == 0) && PspGfxPspgl_MirrorEncodingFailed()) {
        ctx->textureMirrorFallback = 1;
        ctx->textureUploadAttempted = 0;
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_PREPARE);
        PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_TEXTURE);
        return psp_gfx_dl_prepare_texture(ctx, deferred, premultiply);
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
    if (cache != PSP_HW_TEXTURE_CACHE_COUNT) {
        PspHwCounterProfile_CountTextureCacheLookup(cache, hit);
        if (!hit && (ctx->textureId != 0)) {
            PspHwCounterProfile_CountTextureUpload(cache, ctx->textureUploadWidth * ctx->textureUploadHeight * 4U);
        }
    }
#if PROFILE_TRIVIAL_REJECTS
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
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_TEXTURE);
    return result;
}

static void psp_gfx_dl_handle_set_tile_size(PspGfxDlContext* ctx, const Gfx* gfx) {
    u32 tile = (gfx->words.w1 >> 24) & 0x7;
    u32 uls;
    u32 ult;
    u32 lrs;
    u32 lrt;

    if (tile != G_TX_RENDERTILE) {
        return;
    }

    uls = (gfx->words.w0 >> 12) & 0xFFF;
    ult = gfx->words.w0 & 0xFFF;
    lrs = (gfx->words.w1 >> 12) & 0xFFF;
    lrt = gfx->words.w1 & 0xFFF;
    ctx->textureTileUls = uls;
    ctx->textureTileUlt = ult;
    /*
     * textureWidth/Height intentionally ignore uls/ult: for the ground/water
     * scroll technique (fox_bg.c gDPSetupTile) and the fox_end2.c ending
     * scroll, lrs/lrt are held constant at (width-1)<<2 while uls/ult alone
     * carry the scroll phase, so (lrs-uls) is not the tile's real pixel
     * width -- it would shrink toward zero as the scroll phase advances.
     * Every static asset in the codebase issues G_SETTILESIZE with
     * uls=ult=0, so this is equivalent to (lrs-uls) for all of them anyway.
     */
    ctx->textureWidth = (lrs >> G_TEXTURE_IMAGE_FRAC) + 1;
    ctx->textureHeight = (lrt >> G_TEXTURE_IMAGE_FRAC) + 1;
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
    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspRenderer_DrawPendingStarfield();
                continue;
            }
            if (PSP_RENDERER_DL_MARKER_ID(cmd->words.w1) == PSP_RENDERER_DL_MARKER_HISTORY_BEGIN) {
                psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspGfxPspgl_BeginReplayCache();
                continue;
            }
            if (PSP_RENDERER_DL_MARKER_ID(cmd->words.w1) == PSP_RENDERER_DL_MARKER_HISTORY_END) {
                psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspGfxPspgl_EndReplayCache();
                continue;
            }
            if (PSP_RENDERER_DL_MARKER_ID(cmd->words.w1) == PSP_RENDERER_DL_MARKER_HISTORY_REPLAY) {
                psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspGfxPspgl_ReplayCache();
                continue;
            }
            if (PSP_RENDERER_DL_MARKER_ID(cmd->words.w1) == PSP_RENDERER_DL_MARKER_VIEWPORT_FULL) {
                psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspGfxPspgl_SetViewportPolicy(0);
                continue;
            }
            if (PSP_RENDERER_DL_MARKER_ID(cmd->words.w1) == PSP_RENDERER_DL_MARKER_VIEWPORT_AUTO) {
                psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
                PspGfxPspgl_SetViewportPolicy(-1);
                continue;
            }
        }

#if PROFILE_COMPONENTS
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
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
            int childHasEnd = (child != NULL) && psp_gfx_dl_has_bounded_end(child);
#else
            int childHasEnd = child != NULL;
#endif

#if PSP_RENDERER_DIAGNOSTICS
            if ((child != NULL) && !sMapExplosionDlProbeDone &&
                psp_gfx_dl_same_address(child, aMapPlanetExplosionDL)) {
                psp_gfx_dl_probe_map_asset_dl("expl", child, cmd->words.w1, noPush, childHasEnd, depth + 1,
                                              (const void*) &ast_map_seg6_vtx_47A28[0]);
                sMapExplosionDlProbeDone = 1;
            }
            if ((child != NULL) && !sMapVenomDlProbeDone &&
                psp_gfx_dl_same_address(child, gMapVenomCloudRuntimeDL)) {
                psp_gfx_dl_probe_map_asset_dl("venom", child, cmd->words.w1, noPush, childHasEnd, depth + 1,
                                              (const void*) &ast_map_seg6_vtx_47F00[0]);
                sMapVenomDlProbeDone = 1;
            }
            if ((child != NULL) && !sMapCursorDlProbeDone && psp_gfx_dl_same_address(child, aMapCursorDL)) {
                psp_gfx_dl_probe_map_asset_dl("cursor", child, cmd->words.w1, noPush, childHasEnd, depth + 1,
                                              (const void*) &ast_map_seg6_vtx_1DD98[0]);
                sMapCursorDlProbeDone = 1;
            }
#endif

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
            psp_gfx_dl_handle_mtx(ctx, cmd, 0);
            continue;
        }

        if (opcode == PSP_GFX_OP_PORT_MTXF) {
            psp_gfx_dl_handle_mtx(ctx, cmd, 1);
            continue;
        }

        if (opcode == PSP_GFX_OP_PORT_INVALIDATE_RGBA16) {
            const u16* pixels = psp_gfx_dl_resolve_ptr(ctx, cmd->words.w1);

            psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_RENDER_STATE_CHANGE);
            PspGfxPspgl_InvalidateRgba16Texture(pixels);
            if (ctx->textureImage == pixels) {
                ctx->textureId = 0;
                ctx->textureRef = psp_gfx_dl_null_texture_ref();
                ctx->textureUploadAttempted = 0;
                psp_gfx_dl_mark_effective_material_dirty(ctx);
            }
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
#if PSP_RENDERER_DIAGNOSTICS
            psp_gfx_dl_trace_rectangle(ctx, cmd, depth, 1);
#endif
            psp_gfx_dl_handle_texture_rectangle(ctx, cmd, half1, half2, opcode == G_TEXRECTFLIP);
            continue;
        }

        if (opcode == G_FILLRECT) {
#if PSP_RENDERER_DIAGNOSTICS
            psp_gfx_dl_trace_rectangle(ctx, cmd, depth, 0);
#endif
            psp_gfx_dl_handle_fill_rectangle(ctx, cmd);
            continue;
        }

        if (opcode == G_SETSCISSOR) {
            psp_gfx_dl_handle_set_scissor(ctx, cmd);
            continue;
        }

        if (opcode == PSP_GFX_OP_F3D_TRI1) {
            u32 w1 = cmd->words.w1;
            u8 a = psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF);
            u8 b = psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF);
            u8 c = psp_gfx_dl_decode_tri_index(w1 & 0xFF);
            PspProfiler_CountTriangleCommand(1, 1, 0);
            PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_TRIANGLE);
            PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
#if PSP_RENDERER_DIAGNOSTICS
            psp_gfx_dl_trace_triangle(ctx, cmd, depth, a, b, c);
#endif
            psp_gfx_dl_emit_tri(ctx, a, b, c);
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TRIANGLE);
            PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_TRIANGLE);
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
            PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_TRIANGLE);
            PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
#if PSP_RENDERER_DIAGNOSTICS
            psp_gfx_dl_trace_triangle(ctx, cmd, depth, a0, b0, c0);
            psp_gfx_dl_trace_triangle(ctx, cmd, depth, a1, b1, c1);
#endif
#if PROFILE_TRIVIAL_REJECTS
            PspProfiler_CountTri2OutcomeMatrix(psp_gfx_dl_classify_triangle_outcome(ctx, a0, b0, c0),
                                               psp_gfx_dl_classify_triangle_outcome(ctx, a1, b1, c1));
#endif
            if (!psp_gfx_dl_try_emit_tri2_direct_pair(ctx, a0, b0, c0, a1, b1, c1)) {
                psp_gfx_dl_emit_tri(ctx, a0, b0, c0);
                psp_gfx_dl_emit_tri(ctx, a1, b1, c1);
            }
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TRIANGLE);
            PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_TRIANGLE);
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

    for (i = 0; i < offsetof(PspGfxDlContext, vertices); i++) {
        bytes[i] = 0;
    }
    for (i = 0; i < ARRAY_COUNT(ctx->vertices); i++) {
        ctx->vertices[i].projectionSerial = 0;
        ctx->vertices[i].state.raw = 0;
    }
    for (i = 0; i < ARRAY_COUNT(ctx->projectionSnapshots); i++) {
        ctx->projectionSnapshots[i].refCount = 0;
    }
    bytes = (u8*) &ctx->batchCount;
    for (i = 0; i < (sizeof(*ctx) - offsetof(PspGfxDlContext, batchCount)); i++) {
        bytes[i] = 0;
    }
    psp_gfx_dl_identity(ctx->modelview);
    psp_gfx_dl_identity(ctx->projection);
    psp_gfx_dl_identity(ctx->fogProjection);
    ctx->primitiveR = 255;
    ctx->primitiveG = 255;
    ctx->primitiveB = 255;
    ctx->primitiveA = 255;
    ctx->environmentR = 255;
    ctx->environmentG = 255;
    ctx->environmentB = 255;
    ctx->environmentA = 255;
#if PSP_RENDERER_DIAGNOSTICS
    ctx->primitiveColorRaw = 0xFFFFFFFFU;
    ctx->environmentColorRaw = 0xFFFFFFFFU;
    ctx->fogColorRaw = 0x000000FFU;
#endif
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
    ctx->modelviewSerial = 1;
    ctx->projectionSerial = 1;
    ctx->currentProjectionSnapshot = PSP_GFX_DL_NO_PROJECTION_SNAPSHOT;
    ctx->cachedModelviewSerial = 0;
    ctx->cachedModelviewSerial = 0;
    ctx->cachedProjectionSerial = 0;
    psp_gfx_dl_stage_ambient_light(ctx);
    for (i = 0; i < ARRAY_COUNT(ctx->lights); i++) {
        psp_gfx_dl_stage_directional_light(ctx, i);
    }
}

#if PSP_RENDERER_DIAGNOSTICS
// reports the accumulated corpus ranked by triangle coverage, since coverage by
// triangle count is what decides whether a template path is worth having
#define PSP_GFX_DL_MATERIAL_CORPUS_REPORT_TOP 24

static u8 sPspGfxDlMaterialCorpusReported[PSP_GFX_DL_MATERIAL_CORPUS_ENTRIES];

static void psp_gfx_dl_material_corpus_report(u32 taskIndex) {
    char line[768];
    u32 total = sPspGfxDlMaterialCorpusTriangles;
    u32 cumulative = 0;
    u32 rank;
    u32 i;

    snprintf(line, sizeof(line),
             "[pspgl-material-corpus] task=%lu keys=%lu overflow=%lu tris=%lu unattributed=%lu",
             (unsigned long) taskIndex, (unsigned long) sPspGfxDlMaterialCorpusCount,
             (unsigned long) sPspGfxDlMaterialCorpusOverflow, (unsigned long) total,
             (unsigned long) sPspGfxDlMaterialCorpusUnattributed);
    PspPlatform_LogLine(line);

    if (total == 0) {
        return;
    }

    for (i = 0; i < sPspGfxDlMaterialCorpusCount; i++) {
        sPspGfxDlMaterialCorpusReported[i] = 0;
    }

    for (rank = 0; rank < PSP_GFX_DL_MATERIAL_CORPUS_REPORT_TOP; rank++) {
        const PspGfxDlMaterialCorpusEntry* entry;
        u32 best = PSP_GFX_DL_MATERIAL_CORPUS_NONE;
        u32 bestTriangles = 0;
        u32 permille;

        for (i = 0; i < sPspGfxDlMaterialCorpusCount; i++) {
            if (sPspGfxDlMaterialCorpusReported[i]) {
                continue;
            }
            if ((best == PSP_GFX_DL_MATERIAL_CORPUS_NONE) ||
                (sPspGfxDlMaterialCorpus[i].triangleCount > bestTriangles)) {
                best = i;
                bestTriangles = sPspGfxDlMaterialCorpus[i].triangleCount;
            }
        }
        if (best == PSP_GFX_DL_MATERIAL_CORPUS_NONE) {
            break;
        }
        sPspGfxDlMaterialCorpusReported[best] = 1;
        entry = &sPspGfxDlMaterialCorpus[best];
        cumulative += entry->triangleCount;
        // permille avoids relying on float formatting in log output
        permille = (u32) (((u64) entry->triangleCount * 1000U) / total);

        snprintf(line, sizeof(line),
                 "[pspgl-material-key] rank=%lu key=0x%08lx tris=%lu permille=%lu cumPermille=%lu applies=%lu "
                 "combine=%lu omH=0x%08lx omL=0x%08lx geom=0x%08lx tex=%u fmt=%lu size=%lu env=%u wrap=%u,%u "
                 "aTest=%u blend=%u premul=%u point=%u zTest=%u zWrite=%u fog=%u",
                 (unsigned long) rank, (unsigned long) entry->key, (unsigned long) entry->triangleCount,
                 (unsigned long) permille, (unsigned long) (((u64) cumulative * 1000U) / total),
                 (unsigned long) entry->applyCount, (unsigned long) entry->combineMode,
                 (unsigned long) entry->otherModeH, (unsigned long) entry->otherModeL,
                 (unsigned long) entry->geometryMode, entry->textured, (unsigned long) entry->textureFormat,
                 (unsigned long) entry->textureSize, entry->textureEnv, entry->wrapS, entry->wrapT,
                 entry->alphaTest, entry->blend, entry->premultiplied, entry->pointFilter, entry->depthTest,
                 entry->depthWrite, entry->fog);
        PspPlatform_LogLine(line);
    }
}
#endif

#if PSP_LOG_ENABLED
static void psp_gfx_dl_pool_report(u32 taskIndex) {
    char line[320];

    snprintf(line, sizeof(line),
             "[pspgl-pool] task=%lu slots=%u cap=%u hits=%lu opens=%lu evictions=%lu capFlush=%lu "
             "drained=%lu unpooled=%lu peakOpen=%lu",
             (unsigned long) taskIndex, PSP_BATCH_POOL_SLOTS, PSP_BATCH_POOL_VERTICES,
             (unsigned long) sPspGfxDlPoolHits, (unsigned long) sPspGfxDlPoolOpens,
             (unsigned long) sPspGfxDlPoolEvictions, (unsigned long) sPspGfxDlPoolCapacityFlushes,
             (unsigned long) sPspGfxDlPoolDrained, (unsigned long) sPspGfxDlPoolUnpooled,
             (unsigned long) sPspGfxDlPoolPeakOpen);
    PspPlatform_LogLine(line);
}
#endif

#if PSP_RENDERER_DIAGNOSTICS
int PspGfxDl_TracePollControls(u32 rawButtons) {
    const u32 combo = PSP_CTRL_SELECT | PSP_CTRL_TRIANGLE;
    int pressed = ((rawButtons & combo) == combo) && ((sPspGfxDlTracePreviousButtons & combo) != combo);
    int consumed = (rawButtons & combo) == combo;

    sPspGfxDlTracePreviousButtons = rawButtons;
    if (pressed) {
        sPspGfxDlTraceArmed = 1;
        PspPlatform_LogLine("[rdp-trace] armed next graphics task");
    }
    return consumed;
}
#endif

#if PSP_RENDERER_DIAGNOSTICS
static void psp_gfx_dl_report_fog_investigation(PspGfxDlContext* ctx) {
    static const char* sampleNames[3] = { "near", "middle", "far" };
    char line[512];
    u32 i;
    u32 row;

    for (i = 0; i < 3; i++) {
        const PspGfxDlFogTransformSample* sample = &ctx->fogTransformSamples[i];
        float zOverW;

        if (!sample->valid) {
            continue;
        }
        zOverW = sample->clip[3] > 0.0f ? sample->clip[2] / sample->clip[3] : 0.0f;
        snprintf(line, sizeof(line),
                 "[fog-transform] sample=%s object=%d,%d,%d view=%.6f,%.6f,%.6f,%.6f "
                 "clip=%.6f,%.6f,%.6f,%.6f zOverW=%.9f mul=%d offset=%d alpha=%u",
                 sampleNames[i], sample->objectX, sample->objectY, sample->objectZ,
                 sample->view[0], sample->view[1], sample->view[2], sample->view[3],
                 sample->clip[0], sample->clip[1], sample->clip[2], sample->clip[3], zOverW,
                 sample->fogMul, sample->fogOffset, (unsigned int) sample->fogAlpha);
        PspPlatform_LogLine(line);
        for (row = 0; row < 4; row++) {
            snprintf(line, sizeof(line),
                     "[fog-transform-mv] sample=%s row=%lu %.9f %.9f %.9f %.9f",
                     sampleNames[i], (unsigned long) row, sample->modelview[row][0],
                     sample->modelview[row][1], sample->modelview[row][2], sample->modelview[row][3]);
            PspPlatform_LogLine(line);
        }
        for (row = 0; row < 4; row++) {
            snprintf(line, sizeof(line),
                     "[fog-transform-proj] sample=%s row=%lu %.9f %.9f %.9f %.9f",
                     sampleNames[i], (unsigned long) row, sample->projection[row][0],
                     sample->projection[row][1], sample->projection[row][2], sample->projection[row][3]);
            PspPlatform_LogLine(line);
        }
    }
}
#endif

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats) {
    PspGfxDlContext* ctx = &sPspGfxDlContext;
#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
    // sized so the widest stats lines cannot truncate at large counter values
    char line[768];
#endif

    PspGfxPspgl_InitColorTransfer();
    psp_gfx_dl_reset_context(ctx);
    ctx->taskIndex = taskIndex;
#if PSP_RENDERER_DIAGNOSTICS
    if (!sPspGfxDlTraceHintLogged) {
        PspPlatform_LogLine("[rdp-trace] press SELECT+TRIANGLE to capture the next graphics task");
        sPspGfxDlTraceHintLogged = 1;
    }
    if (sPspGfxDlTraceArmed) {
        ctx->traceActive = 1;
        sPspGfxDlTraceArmed = 0;
        snprintf(line, sizeof(line), "[rdp-trace] begin task=%lu dl=%p cap=%u",
                 (unsigned long) taskIndex, (const void*) dl, PSP_GFX_DL_TRACE_MAX_RECORDS);
        PspPlatform_LogLine(line);
    }
#endif
    PspProfiler_CountDisplayListTask();
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_DL_TRAVERSAL);
    psp_gfx_dl_run_internal(ctx, dl, 0);
#if PROFILE_TRIVIAL_REJECTS
    psp_gfx_dl_trivial_reject_scope_clear_for_task(ctx);
#endif
    psp_gfx_dl_flush_all(ctx, PSP_PROFILE_FLUSH_END_OF_TASK);
    PspGfxPspgl_ClearScissor();
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_DL_TRAVERSAL);

#if PSP_LOG_ENABLED
    if ((taskIndex != 0) && ((taskIndex % 300) == 0)) {
        psp_gfx_dl_pool_report(taskIndex);
    }
#endif

    if (outStats != NULL) {
        *outStats = ctx->stats;
    }

#if PSP_LOG_ENABLED || PSP_RENDERER_DIAGNOSTICS
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {
        psp_gfx_dl_report_fog_investigation(ctx);
        snprintf(line, sizeof(line), "[rdp-trace] end task=%lu draws=%lu records=%lu dropped=%lu",
                 (unsigned long) taskIndex, (unsigned long) ctx->traceDrawIndex,
                 (unsigned long) ctx->traceRecordCount, (unsigned long) ctx->traceDroppedCount);
        PspPlatform_LogLine(line);
    }
#endif

    if ((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
        (ctx->stats.depthLimitHit != 0)) {
        snprintf(line, sizeof(line),
                 "[pspgl-dl] task=%lu cmds=%lu vtx=%lu tri=%lu drawv=%lu dl=%lu reject=%lu mtx=%lu unsup=%lu "
                 "push=%lu pop=%lu mtxReject=%lu vp=%lu invalid=%lu outside=%lu tex=%lu texReject=%lu "
                 "rgba16=%lu rgba32=%lu ci4=%lu ia8=%lu ia16=%lu texTri=%lu alphaTestTri=%lu blendTri=%lu "
                 "texRect=%lu rectReject=%lu fillRect=%lu fillPrim=%lu fillUnsup=%lu "
                 "origFogDraw=%lu origFogTri=%lu origFogBytes=%lu origFogCopies=%lu "
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
                 (unsigned long) ctx->stats.originalFogDrawCount,
                 (unsigned long) ctx->stats.originalFogTriangleCount,
                 (unsigned long) ctx->stats.originalFogVertexBytes,
                 (unsigned long) ctx->stats.originalFogVertexCopies,
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

#if PSP_RENDERER_DIAGNOSTICS
    // accumulated corpus, reported sparsely since it grows across tasks
    if ((taskIndex != 0) && ((taskIndex % 300) == 0)) {
        psp_gfx_dl_material_corpus_report(taskIndex);
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

    return ctx->stats.commandCount > 0;
}

#if PROFILE_HW_COUNTERS
void PspGfxDl_GetLastWork(u32* commands, u32* loadedVertices, u32* submittedVertices) {
    const PspGfxDlStats* stats = &sPspGfxDlContext.stats;

    if (commands != NULL) {
        *commands = stats->commandCount;
    }
    if (loadedVertices != NULL) {
        *loadedVertices = stats->vertexCount;
    }
    if (submittedVertices != NULL) {
        *submittedVertices = stats->drawVertexCount;
    }
}
#endif
