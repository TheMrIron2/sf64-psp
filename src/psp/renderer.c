#include "PR/ultratypes.h"
#include "libultra/ultra64.h"
#include "sf64thread.h"
#include "macros.h"
#include "assets/ast_title.h"
#include "src/psp/platform.h"
#include "src/psp/renderer.h"
#include "src/psp/renderer_texture.h"

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psputils.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define PSP_RENDERER_DL_MAX_DEPTH 8
#define PSP_RENDERER_DL_MAX_COMMANDS 8192
#define PSP_RENDERER_UNSUPPORTED_BUCKETS 256
#define PSP_RENDERER_RANGES 32
#define PSP_RENDERER_STATIC_DL_SCAN_COMMANDS 512

/* Logical game space stays N64-native; GU output presents it as centered 4:3. */
#define PSP_SCREEN_WIDTH 480
#define PSP_SCREEN_HEIGHT 272
#define PSP_FRAMEBUFFER_WIDTH 512
#define N64_SCREEN_WIDTH 320
#define N64_SCREEN_HEIGHT 240

#define PSP_SCREEN_GUARD 128
#define PSP_RENDERER_MAX_STARS 512
#define PSP_RENDERER_MAX_LIGHTS 7

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#define PSP_LIGHT_VARIANT_RAW 0
#define PSP_LIGHT_VARIANT_MODELVIEW_ROW 1
#define PSP_LIGHT_VARIANT_MODELVIEW_COLUMN 2
#define PSP_LIGHT_VARIANT_MODELVIEW_COLUMN_NEGATED 3

#define PSP_NORMAL_VARIANT_RAW 0
#define PSP_NORMAL_VARIANT_MODELVIEW 1
#define PSP_NORMAL_VARIANT_MODELVIEW_NEGATED 2

#ifndef PSP_RENDERER_LIGHT_VARIANT
#define PSP_RENDERER_LIGHT_VARIANT PSP_LIGHT_VARIANT_RAW
#endif

#ifndef PSP_RENDERER_NORMAL_VARIANT
#define PSP_RENDERER_NORMAL_VARIANT PSP_NORMAL_VARIANT_RAW
#endif

#define PSP_PRESENT_SCALE ((f32) PSP_SCREEN_HEIGHT / (f32) N64_SCREEN_HEIGHT)
#define PSP_PRESENT_WIDTH ((f32) N64_SCREEN_WIDTH * PSP_PRESENT_SCALE)
#define PSP_PRESENT_HEIGHT ((f32) N64_SCREEN_HEIGHT * PSP_PRESENT_SCALE)
#define PSP_PRESENT_OFFSET_X (((f32) PSP_SCREEN_WIDTH - PSP_PRESENT_WIDTH) * 0.5f)
#define PSP_PRESENT_OFFSET_Y (((f32) PSP_SCREEN_HEIGHT - PSP_PRESENT_HEIGHT) * 0.5f)

#define PSP_COORD_X(x) (PSP_PRESENT_OFFSET_X + ((f32) (x) * PSP_PRESENT_SCALE))
#define PSP_COORD_Y(y) (PSP_PRESENT_OFFSET_Y + ((f32) (y) * PSP_PRESENT_SCALE))
#define PSP_GBI_OPCODE(cmd) ((u8) (cmd))

typedef enum {
    PSP_COMBINE_UNKNOWN,
    PSP_COMBINE_SHADE,
    PSP_COMBINE_PRIMITIVE,
    PSP_COMBINE_DECAL_RGB,
    PSP_COMBINE_DECAL_RGBA,
    PSP_COMBINE_MODULATE_SHADE_DECAL_ALPHA,
    PSP_COMBINE_MODULATE_SHADE_ALPHA,
    PSP_COMBINE_MODULATE_PRIM_ALPHA,
} PspCombineMode;

typedef struct {
    u32 color;
    s16 x;
    s16 y;
    s16 z;
} PspVertex2DColor;

typedef struct {
    s16 u;
    s16 v;
    u32 color;
    s16 x;
    s16 y;
    s16 z;
} PspVertex2DTexture;

typedef struct {
    s16 x;
    s16 y;
    s16 z;
    s16 s;
    s16 t;
    u32 color;
    s8 nx;
    s8 ny;
    s8 nz;
    u8 alpha;
} PspRspVertex;

typedef struct {
    u8 r;
    u8 g;
    u8 b;
    s8 x;
    s8 y;
    s8 z;
} PspRspLight;

typedef struct {
    const Gfx* start;
    const Gfx* end;
} PspDlRange;

typedef struct {
    const Gfx* pc;
    const Gfx* end;
} PspDlFrame;

typedef struct {
    u32 primColor;
    u32 fillColor;
    u32 envColor;
    u32 renderMode;
    PspCombineMode combineMode;
    void* textureImage;
    u32 textureFmt;
    u32 textureSiz;
    u32 textureWidth;
    u32 tileWidth;
    u32 tileHeight;
    u32 palette[256];
    u32 paletteCount;
} PspRdpState;

typedef struct {
    u32 mode;
    int textureEnabled;
    u32 lightCount;
    PspRspLight lights[PSP_RENDERER_MAX_LIGHTS];
    u8 ambientR;
    u8 ambientG;
    u8 ambientB;
} PspRspState;

typedef struct {
    u32 unsupported[PSP_RENDERER_UNSUPPORTED_BUCKETS];
    u32 frameCount;
    u32 taskBytes;
    u32 commandCount;
    u32 dlCount;
    u32 endDlCount;
    u32 fillRectCount;
    u32 texRectCount;
    u32 texRectFlipCount;
    u32 texturedRectCount;
    u32 placeholderRectCount;
    u32 setTimgCount;
    u32 setTileCount;
    u32 setTileSizeCount;
    u32 loadBlockCount;
    u32 loadTlutCount;
    u32 unsupportedCount;
    u32 validationFailures;
    u32 rangeRejects;
    u32 commandLimitHit;
    u32 depthLimitHit;
    u32 texCacheHits;
    u32 texCacheMisses;
    u32 texRectDrawSkipped;
#if PSP_RENDERER_DIAGNOSTICS
    u32 lightTransformLogged;
    u32 shadeSampleCount;
    u32 shadeLitCount;
    u32 shadeMinR;
    u32 shadeMinG;
    u32 shadeMinB;
    u32 shadeMaxR;
    u32 shadeMaxG;
    u32 shadeMaxB;
    u32 shadeSumR;
    u32 shadeSumG;
    u32 shadeSumB;
    s32 shadeDotMilliSum;
    u32 shadeDotSampleCount;
    u32 texFuncModulateCount;
    u32 texFuncReplaceCount;
#endif
} PspRendererCensus;

typedef struct {
    PspRdpState rdp;
    PspRspState rsp;
    PspRspVertex vertices[64];
    f32 modelview[4][4];
    f32 projection[4][4];
    int hasModelview;
    int hasProjection;
    PspRendererCensus census;
    PspDlRange ranges[PSP_RENDERER_RANGES];
    u32 rangeCount;
    u32 taskIndex;
} PspRendererState;

typedef struct {
    s16 x;
    s16 y;
    u32 color;
} PspStarPoint;

typedef struct {
    f32 x;
    f32 y;
    f32 z;
    s16 u;
    s16 v;
    u32 color;
} PspProjectedVertex;

static unsigned int sGuList[262144] __attribute__((aligned(64)));
static PspRendererState sRenderer;
static int sRendererReady;

static PspStarPoint sStarfieldStars[PSP_RENDERER_MAX_STARS];
static u32 sStarfieldCount;
static int sStarfieldReady;

static u8 psp_gfx_opcode(const Gfx* gfx) {
    return (u8) (gfx->words.w0 >> 24);
}

static u32 psp_rgba32(u8 r, u8 g, u8 b, u8 a) {
    return ((u32) a << 24) | ((u32) b << 16) | ((u32) g << 8) | (u32) r;
}

static u8 psp_rgba32_alpha(u32 color) {
    return (u8) (color >> 24);
}

static u32 psp_rgba16_to_8888(u16 color) {
    u8 r = (u8) (((color >> 11) & 0x1F) * 255 / 31);
    u8 g = (u8) (((color >> 6) & 0x1F) * 255 / 31);
    u8 b = (u8) (((color >> 1) & 0x1F) * 255 / 31);
    u8 a = (color & 1) ? 255 : 0;

    return psp_rgba32(r, g, b, a);
}

static void psp_renderer_identity_mtx(f32 mtx[4][4]) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            mtx[row][col] = (row == col) ? 1.0f : 0.0f;
        }
    }
}

static void psp_renderer_mtx_l2f(f32 out[4][4], const Mtx* src) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            s32 fixed = ((s32) ((u32) src->u.i[row][col] << 16)) | src->u.f[row][col];
            out[row][col] = fixed / 65536.0f;
        }
    }
}

static void psp_renderer_mtx_mul(f32 out[4][4], f32 a[4][4], f32 b[4][4]) {
    f32 result[4][4];
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

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row][col] = result[row][col];
        }
    }
}

static void psp_renderer_mtx_copy(f32 out[4][4], f32 in[4][4]) {
    u32 row;
    u32 col;

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            out[row][col] = in[row][col];
        }
    }
}

static void psp_renderer_transform_modelview_vec3(f32 mtx[4][4], f32 inX, f32 inY, f32 inZ, f32* x, f32* y, f32* z,
                                                  f32* w) {
    *x = (mtx[0][0] * inX) + (mtx[1][0] * inY) + (mtx[2][0] * inZ) + mtx[3][0];
    *y = (mtx[0][1] * inX) + (mtx[1][1] * inY) + (mtx[2][1] * inZ) + mtx[3][1];
    *z = (mtx[0][2] * inX) + (mtx[1][2] * inY) + (mtx[2][2] * inZ) + mtx[3][2];
    *w = (mtx[0][3] * inX) + (mtx[1][3] * inY) + (mtx[2][3] * inZ) + mtx[3][3];
}

static void psp_renderer_transform_projection_vec3(f32 mtx[4][4], f32 inX, f32 inY, f32 inZ, f32* x, f32* y, f32* z,
                                                   f32* w) {
    *x = (mtx[0][0] * inX) + (mtx[1][0] * inY) + (mtx[2][0] * inZ) + mtx[3][0];
    *y = (mtx[0][1] * inX) + (mtx[1][1] * inY) + (mtx[2][1] * inZ) + mtx[3][1];
    *z = (mtx[0][2] * inX) + (mtx[1][2] * inY) + (mtx[2][2] * inZ) + mtx[3][2];
    *w = (mtx[0][3] * inX) + (mtx[1][3] * inY) + (mtx[2][3] * inZ) + mtx[3][3];
}

static int psp_renderer_project_model_point(f32 inX, f32 inY, f32 inZ, f32* x, f32* y, f32* z) {
    f32 vx = inX;
    f32 vy = inY;
    f32 vz = inZ;
    f32 vw = 1.0f;
    f32 clipX;
    f32 clipY;
    f32 clipZ;
    f32 clipW;

    if (sRenderer.hasModelview) {
        psp_renderer_transform_modelview_vec3(sRenderer.modelview, vx, vy, vz, &vx, &vy, &vz, &vw);
    }
    if (!sRenderer.hasProjection) {
        *x = vx;
        *y = vy;
        *z = vz;
        return 1;
    }

    psp_renderer_transform_projection_vec3(sRenderer.projection, vx, vy, vz, &clipX, &clipY, &clipZ, &clipW);
    if ((clipW > -0.001f) && (clipW < 0.001f)) {
        return 0;
    }

    *x = ((clipX / clipW) + 1.0f) * (N64_SCREEN_WIDTH * 0.5f);
    *y = (1.0f - (clipY / clipW)) * (N64_SCREEN_HEIGHT * 0.5f);
    *z = clipZ / clipW;
    return 1;
}

static int psp_renderer_transform_vertex(const PspRspVertex* src, f32* x, f32* y, f32* z) {
    return psp_renderer_project_model_point(src->x, src->y, src->z, x, y, z);
}

static s32 psp_renderer_clamp_s32(s32 value, s32 min, s32 max);

static s16 psp_renderer_depth_to_s16(f32 z) {
    s32 depth = (s32) (((1.0f - z) * 0.5f) * 32767.0f);

    return (s16) psp_renderer_clamp_s32(depth, 0, 32767);
}

static u8 psp_renderer_modulate_u8(u8 a, u8 b) {
    return (u8) (((u32) a * (u32) b) / 255U);
}

static void psp_renderer_normalize_vec3(f32* x, f32* y, f32* z) {
    f32 lengthSq = (*x * *x) + (*y * *y) + (*z * *z);

    if (lengthSq > 0.000001f) {
        f32 invLength = 1.0f / sqrtf(lengthSq);

        *x *= invLength;
        *y *= invLength;
        *z *= invLength;
    }
}

static void psp_renderer_transform_light_vec3(f32 rawX, f32 rawY, f32 rawZ, f32* outX, f32* outY, f32* outZ) {
    f32 lightX = rawX;
    f32 lightY = rawY;
    f32 lightZ = rawZ;

#if PSP_RENDERER_LIGHT_VARIANT == PSP_LIGHT_VARIANT_RAW
    (void) rawX;
    (void) rawY;
    (void) rawZ;
#elif PSP_RENDERER_LIGHT_VARIANT == PSP_LIGHT_VARIANT_MODELVIEW_ROW
    if (sRenderer.hasModelview) {
        lightX = (rawX * sRenderer.modelview[0][0]) + (rawY * sRenderer.modelview[0][1]) +
                 (rawZ * sRenderer.modelview[0][2]);
        lightY = (rawX * sRenderer.modelview[1][0]) + (rawY * sRenderer.modelview[1][1]) +
                 (rawZ * sRenderer.modelview[1][2]);
        lightZ = (rawX * sRenderer.modelview[2][0]) + (rawY * sRenderer.modelview[2][1]) +
                 (rawZ * sRenderer.modelview[2][2]);
    }
#else
    if (sRenderer.hasModelview) {
        lightX = (sRenderer.modelview[0][0] * rawX) + (sRenderer.modelview[1][0] * rawY) +
                 (sRenderer.modelview[2][0] * rawZ);
        lightY = (sRenderer.modelview[0][1] * rawX) + (sRenderer.modelview[1][1] * rawY) +
                 (sRenderer.modelview[2][1] * rawZ);
        lightZ = (sRenderer.modelview[0][2] * rawX) + (sRenderer.modelview[1][2] * rawY) +
                 (sRenderer.modelview[2][2] * rawZ);
    }
#if PSP_RENDERER_LIGHT_VARIANT == PSP_LIGHT_VARIANT_MODELVIEW_COLUMN_NEGATED
    lightX = -lightX;
    lightY = -lightY;
    lightZ = -lightZ;
#endif
#endif

    *outX = lightX;
    *outY = lightY;
    *outZ = lightZ;
}

static void psp_renderer_transform_normal_vec3(f32 rawX, f32 rawY, f32 rawZ, f32* outX, f32* outY, f32* outZ) {
    f32 normalX = rawX;
    f32 normalY = rawY;
    f32 normalZ = rawZ;

#if PSP_RENDERER_NORMAL_VARIANT == PSP_NORMAL_VARIANT_RAW
    (void) rawX;
    (void) rawY;
    (void) rawZ;
#else
    if (sRenderer.hasModelview) {
        normalX = (sRenderer.modelview[0][0] * rawX) + (sRenderer.modelview[1][0] * rawY) +
                  (sRenderer.modelview[2][0] * rawZ);
        normalY = (sRenderer.modelview[0][1] * rawX) + (sRenderer.modelview[1][1] * rawY) +
                  (sRenderer.modelview[2][1] * rawZ);
        normalZ = (sRenderer.modelview[0][2] * rawX) + (sRenderer.modelview[1][2] * rawY) +
                  (sRenderer.modelview[2][2] * rawZ);
    }
#if PSP_RENDERER_NORMAL_VARIANT == PSP_NORMAL_VARIANT_MODELVIEW_NEGATED
    normalX = -normalX;
    normalY = -normalY;
    normalZ = -normalZ;
#endif
#endif

    *outX = normalX;
    *outY = normalY;
    *outZ = normalZ;
}

#include "src/psp/renderer_diag.inc.c"

static u32 psp_renderer_lit_vertex_color(const PspRspVertex* vertex) {
    f32 normalX;
    f32 normalY;
    f32 normalZ;
    f32 r = sRenderer.rsp.ambientR;
    f32 g = sRenderer.rsp.ambientG;
    f32 b = sRenderer.rsp.ambientB;
    f32 dotSum = 0.0f;
    u32 dotCount = 0;
    u32 color;
    u32 i;

    psp_renderer_transform_normal_vec3((f32) vertex->nx, (f32) vertex->ny, (f32) vertex->nz, &normalX, &normalY,
                                       &normalZ);
    psp_renderer_normalize_vec3(&normalX, &normalY, &normalZ);

    for (i = 0; i < sRenderer.rsp.lightCount; i++) {
        const PspRspLight* light = &sRenderer.rsp.lights[i];
        f32 rawX = (f32) light->x / 127.0f;
        f32 rawY = (f32) light->y / 127.0f;
        f32 rawZ = (f32) light->z / 127.0f;
        f32 lightX;
        f32 lightY;
        f32 lightZ;
#if PSP_RENDERER_DIAGNOSTICS
        f32 transformedX;
        f32 transformedY;
        f32 transformedZ;
#endif
        f32 dot;

        psp_renderer_transform_light_vec3(rawX, rawY, rawZ, &lightX, &lightY, &lightZ);
#if PSP_RENDERER_DIAGNOSTICS
        transformedX = lightX;
        transformedY = lightY;
        transformedZ = lightZ;
#endif

        psp_renderer_normalize_vec3(&lightX, &lightY, &lightZ);
#if PSP_RENDERER_DIAGNOSTICS
        if (i == 0) {
            psp_renderer_log_light_transform(light->x, light->y, light->z, transformedX, transformedY, transformedZ,
                                             lightX, lightY, lightZ);
        }
#endif
        dot = (normalX * lightX) + (normalY * lightY) + (normalZ * lightZ);

        if (dot > 0.0f) {
            if (dot > 1.0f) {
                dot = 1.0f;
            }
            r += light->r * dot;
            g += light->g * dot;
            b += light->b * dot;
            dotSum += dot;
            dotCount++;
        }
    }

    color = psp_rgba32(
        (u8) psp_renderer_clamp_s32((s32) r, 0, 255),
        (u8) psp_renderer_clamp_s32((s32) g, 0, 255),
        (u8) psp_renderer_clamp_s32((s32) b, 0, 255),
        psp_renderer_modulate_u8(vertex->alpha, psp_rgba32_alpha(sRenderer.rdp.primColor))
    );
#if PSP_RENDERER_DIAGNOSTICS
    psp_renderer_record_shade_sample(color, dotSum, dotCount);
#endif
    return color;
}

static u32 psp_renderer_shade_vertex_color(const PspRspVertex* vertex) {
    if ((sRenderer.rsp.mode & G_LIGHTING) != 0) {
        return psp_renderer_lit_vertex_color(vertex);
    }
    return vertex->color;
}

static u32 psp_renderer_vertex_color(const PspRspVertex* vertex) {
    u32 shadeColor;

    if ((sRenderer.rsp.mode & G_SHADE) == 0) {
        return sRenderer.rdp.primColor;
    }

    shadeColor = psp_renderer_shade_vertex_color(vertex);
    switch (sRenderer.rdp.combineMode) {
        case PSP_COMBINE_PRIMITIVE:
        case PSP_COMBINE_MODULATE_PRIM_ALPHA:
            return sRenderer.rdp.primColor;
        case PSP_COMBINE_DECAL_RGB:
        case PSP_COMBINE_DECAL_RGBA:
            return psp_rgba32(255, 255, 255, psp_rgba32_alpha(shadeColor));
        case PSP_COMBINE_SHADE:
        case PSP_COMBINE_MODULATE_SHADE_DECAL_ALPHA:
        case PSP_COMBINE_MODULATE_SHADE_ALPHA:
        case PSP_COMBINE_UNKNOWN:
        default:
            return shadeColor;
    }
}

static void psp_renderer_clear_directional_lights(void) {
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sRenderer.rsp.lights); i++) {
        sRenderer.rsp.lights[i].r = 0;
        sRenderer.rsp.lights[i].g = 0;
        sRenderer.rsp.lights[i].b = 0;
        sRenderer.rsp.lights[i].x = 0;
        sRenderer.rsp.lights[i].y = 0;
        sRenderer.rsp.lights[i].z = 0;
    }
}

static int psp_renderer_should_cull_triangle(const PspProjectedVertex projected[3]) {
    f32 area = ((projected[1].x - projected[0].x) * (projected[2].y - projected[0].y)) -
               ((projected[1].y - projected[0].y) * (projected[2].x - projected[0].x));
    u32 cullMode = sRenderer.rsp.mode & G_CULL_BOTH;

    if ((area > -0.0001f) && (area < 0.0001f)) {
        return 1;
    }
    if (cullMode == G_CULL_BOTH) {
        return 1;
    }
    if ((cullMode == G_CULL_BACK) && (area < 0.0f)) {
        return 1;
    }
    if ((cullMode == G_CULL_FRONT) && (area > 0.0f)) {
        return 1;
    }
    return 0;
}

static void psp_renderer_apply_rsp_depth_state(void) {
    int depthCompare = ((sRenderer.rsp.mode & G_ZBUFFER) != 0) && ((sRenderer.rdp.renderMode & Z_CMP) != 0);
    int depthUpdate = ((sRenderer.rsp.mode & G_ZBUFFER) != 0) && ((sRenderer.rdp.renderMode & Z_UPD) != 0);

    if (depthCompare) {
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthFunc(GU_GEQUAL);
    } else {
        sceGuDisable(GU_DEPTH_TEST);
    }
    sceGuDepthMask(depthUpdate ? GU_FALSE : GU_TRUE);
}

static int psp_renderer_should_blend(void) {
    if ((sRenderer.rdp.renderMode & FORCE_BL) != 0) {
        return 1;
    }
    if ((sRenderer.rdp.renderMode & ZMODE_XLU) != 0) {
        return 1;
    }
    if (psp_rgba32_alpha(sRenderer.rdp.primColor) < 255) {
        return 1;
    }
    return 0;
}

static int psp_renderer_should_modulate_texture(void) {
    if ((sRenderer.rdp.combineMode == PSP_COMBINE_DECAL_RGB) ||
        (sRenderer.rdp.combineMode == PSP_COMBINE_DECAL_RGBA)) {
        return 0;
    }
    if ((sRenderer.rdp.combineMode == PSP_COMBINE_MODULATE_SHADE_DECAL_ALPHA) ||
        (sRenderer.rdp.combineMode == PSP_COMBINE_MODULATE_SHADE_ALPHA) ||
        (sRenderer.rdp.combineMode == PSP_COMBINE_MODULATE_PRIM_ALPHA)) {
        return 1;
    }
    if ((sRenderer.rsp.mode & (G_SHADE | G_LIGHTING)) != 0) {
        return 1;
    }
    if (psp_rgba32_alpha(sRenderer.rdp.primColor) < 255) {
        return 1;
    }
    return 0;
}

static int psp_renderer_should_alpha_test(void) {
    return (sRenderer.rdp.renderMode & (CVG_X_ALPHA | ALPHA_CVG_SEL)) == (CVG_X_ALPHA | ALPHA_CVG_SEL);
}

static void psp_renderer_apply_rsp_blend_state(void) {
    if (psp_renderer_should_blend()) {
        sceGuEnable(GU_BLEND);
    } else {
        sceGuDisable(GU_BLEND);
    }
}

static void psp_renderer_apply_rsp_alpha_state(void) {
    if (psp_renderer_should_alpha_test()) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
    } else {
        sceGuDisable(GU_ALPHA_TEST);
    }
}

static void psp_renderer_draw_rsp_triangle(u8 i0, u8 i1, u8 i2) {
    PspRendererTexture texture;
    PspVertex2DTexture* v;
    PspVertex2DColor* colorV;
    PspRspVertex* a;
    PspRspVertex* b;
    PspRspVertex* c;
    PspRspVertex* in[3];
    PspProjectedVertex projected[3];
    u32 textureWidth;
    u32 textureHeight;
    u32 sourceStride;
    u32 i;
    int requestedTexture;
    int useTexture;
    int texModulates;

    if ((i0 >= ARRAY_COUNT(sRenderer.vertices)) ||
        (i1 >= ARRAY_COUNT(sRenderer.vertices)) ||
        (i2 >= ARRAY_COUNT(sRenderer.vertices))) {
        sRenderer.census.validationFailures++;
        return;
    }

    a = &sRenderer.vertices[i0];
    b = &sRenderer.vertices[i1];
    c = &sRenderer.vertices[i2];
    in[0] = a;
    in[1] = b;
    in[2] = c;

    for (i = 0; i < 3; i++) {
        if (!psp_renderer_transform_vertex(in[i], &projected[i].x, &projected[i].y, &projected[i].z)) {
            sRenderer.census.texRectDrawSkipped++;
            return;
        }
        if ((projected[i].x < -512.0f) || (projected[i].x > 832.0f) ||
            (projected[i].y < -512.0f) || (projected[i].y > 752.0f)) {
            sRenderer.census.texRectDrawSkipped++;
            return;
        }
        projected[i].u = in[i]->s >> 5;
        projected[i].v = in[i]->t >> 5;
        projected[i].color = psp_renderer_vertex_color(in[i]);
    }

    if (psp_renderer_should_cull_triangle(projected)) {
        sRenderer.census.texRectDrawSkipped++;
        return;
    }

    psp_renderer_apply_rsp_depth_state();
    psp_renderer_apply_rsp_blend_state();
    psp_renderer_apply_rsp_alpha_state();

    requestedTexture = ((sRenderer.rsp.textureEnabled != 0) && (sRenderer.rdp.textureImage != NULL));
    textureWidth = sRenderer.rdp.tileWidth;
    textureHeight = sRenderer.rdp.tileHeight;
    if (textureWidth == 0) {
        textureWidth = sRenderer.rdp.textureWidth;
    }
    if (textureHeight == 0) {
        textureHeight = 1;
    }
    sourceStride = sRenderer.rdp.textureWidth;
    if (sourceStride == 0) {
        sourceStride = textureWidth;
    }
    if (sourceStride < textureWidth) {
        sourceStride = textureWidth;
    }

    useTexture = requestedTexture &&
        PspRendererTexture_Get(
            sRenderer.rdp.textureImage,
            sRenderer.rdp.textureFmt,
            sRenderer.rdp.textureSiz,
            textureWidth,
            textureHeight,
            sourceStride,
            0,
            0,
            sRenderer.rdp.palette,
            sRenderer.rdp.paletteCount,
            &texture
        );

    if (useTexture) {
        if (texture.cacheHit) {
            sRenderer.census.texCacheHits++;
        } else {
            sRenderer.census.texCacheMisses++;
        }

        v = (PspVertex2DTexture*) sceGuGetMemory(3 * sizeof(PspVertex2DTexture));
        if (v == NULL) {
            sRenderer.census.validationFailures++;
            return;
        }

        for (i = 0; i < 3; i++) {
            v[i].u = projected[i].u;
            v[i].v = projected[i].v;
            v[i].color = projected[i].color;
            v[i].x = (s16) PSP_COORD_X(projected[i].x);
            v[i].y = (s16) PSP_COORD_Y(projected[i].y);
            v[i].z = psp_renderer_depth_to_s16(projected[i].z);
        }

        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
        sceGuTexImage(0, texture.textureWidth, texture.textureHeight, texture.textureWidth, texture.pixels);
        texModulates = psp_renderer_should_modulate_texture();
#if PSP_RENDERER_DIAGNOSTICS
        if (texModulates) {
            sRenderer.census.texFuncModulateCount++;
        } else {
            sRenderer.census.texFuncReplaceCount++;
        }
#endif
        sceGuTexFunc(texModulates ? GU_TFX_MODULATE : GU_TFX_REPLACE, GU_TCC_RGBA);
        sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuTexWrap(GU_CLAMP, GU_CLAMP);
        sceGuTexScale(1.0f / (f32) texture.textureWidth, 1.0f / (f32) texture.textureHeight);
        sceGuTexOffset(0.0f, 0.0f);

        sceKernelDcacheWritebackRange(v, 3 * sizeof(PspVertex2DTexture));

        sceGuDrawArray(
            GU_TRIANGLES,
            GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
            3,
            0,
            v
        );
    } else {
        colorV = (PspVertex2DColor*) sceGuGetMemory(3 * sizeof(PspVertex2DColor));
        if (colorV == NULL) {
            sRenderer.census.validationFailures++;
            return;
        }

        for (i = 0; i < 3; i++) {
            colorV[i].color = projected[i].color;
            colorV[i].x = (s16) PSP_COORD_X(projected[i].x);
            colorV[i].y = (s16) PSP_COORD_Y(projected[i].y);
            colorV[i].z = psp_renderer_depth_to_s16(projected[i].z);
        }

        sceGuDisable(GU_TEXTURE_2D);
        sceKernelDcacheWritebackRange(colorV, 3 * sizeof(PspVertex2DColor));

        sceGuDrawArray(
            GU_TRIANGLES,
            GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
            3,
            0,
            colorV
        );
    }

    sRenderer.census.texturedRectCount++;
}

static u8 psp_renderer_decode_tri_index(u32 packed) {
#if defined(F3DEX_GBI) || defined(F3DLP_GBI)
    return (u8) (packed / 2);
#else
    return (u8) (packed / 10);
#endif
}

static void psp_renderer_handle_tri1(const Gfx* gfx) {
    u32 w1 = gfx->words.w1;
    u8 a = psp_renderer_decode_tri_index((w1 >> 16) & 0xFF);
    u8 b = psp_renderer_decode_tri_index((w1 >> 8) & 0xFF);
    u8 c = psp_renderer_decode_tri_index(w1 & 0xFF);

    psp_renderer_draw_rsp_triangle(a, b, c);
}

static void psp_renderer_handle_vtx(const Gfx* gfx) {
    const Vtx* src = (const Vtx*) gfx->words.w1;
    u32 w0 = gfx->words.w0;
    u32 count = (w0 >> 10) & 0x3F;
#if defined(F3DEX_GBI) || defined(F3DLP_GBI)
    s32 v0 = (w0 >> 17) & 0x7F;
#else
    s32 v0 = ((w0 >> 17) & 0x7F) - count;
#endif
    u32 i;

    if (src == NULL) {
        sRenderer.census.validationFailures++;
        return;
    }

    if (v0 < 0) {
        sRenderer.census.validationFailures++;
        return;
    }

    if (((u32) v0 + count) > ARRAY_COUNT(sRenderer.vertices)) {
        sRenderer.census.validationFailures++;
        return;
    }

    for (i = 0; i < count; i++) {
        const Vtx* in = &src[i];
        PspRspVertex* out = &sRenderer.vertices[v0 + i];

        out->x = in->v.ob[0];
        out->y = in->v.ob[1];
        out->z = in->v.ob[2];
        out->s = in->v.tc[0];
        out->t = in->v.tc[1];
        out->color = psp_rgba32(in->v.cn[0], in->v.cn[1], in->v.cn[2], in->v.cn[3]);
        out->nx = in->n.n[0];
        out->ny = in->n.n[1];
        out->nz = in->n.n[2];
        out->alpha = in->n.a;
    }
}

static void psp_renderer_handle_tri2(const Gfx* gfx) {
    u32 w0 = gfx->words.w0;
    u32 w1 = gfx->words.w1;

    u8 a0 = psp_renderer_decode_tri_index((w0 >> 16) & 0xFF);
    u8 b0 = psp_renderer_decode_tri_index((w0 >> 8) & 0xFF);
    u8 c0 = psp_renderer_decode_tri_index(w0 & 0xFF);

    u8 a1 = psp_renderer_decode_tri_index((w1 >> 16) & 0xFF);
    u8 b1 = psp_renderer_decode_tri_index((w1 >> 8) & 0xFF);
    u8 c1 = psp_renderer_decode_tri_index(w1 & 0xFF);

    psp_renderer_draw_rsp_triangle(a0, b0, c0);
    psp_renderer_draw_rsp_triangle(a1, b1, c1);
}

static s32 psp_renderer_clamp_s32(s32 value, s32 min, s32 max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static void psp_renderer_reset_census(void) {
    u32 i;

    sRenderer.census.taskBytes = 0;
    sRenderer.census.commandCount = 0;
    sRenderer.census.dlCount = 0;
    sRenderer.census.endDlCount = 0;
    sRenderer.census.fillRectCount = 0;
    sRenderer.census.texRectCount = 0;
    sRenderer.census.texRectFlipCount = 0;
    sRenderer.census.texturedRectCount = 0;
    sRenderer.census.placeholderRectCount = 0;
    sRenderer.census.setTimgCount = 0;
    sRenderer.census.setTileCount = 0;
    sRenderer.census.setTileSizeCount = 0;
    sRenderer.census.loadBlockCount = 0;
    sRenderer.census.loadTlutCount = 0;
    sRenderer.census.unsupportedCount = 0;
    sRenderer.census.validationFailures = 0;
    sRenderer.census.rangeRejects = 0;
    sRenderer.census.commandLimitHit = 0;
    sRenderer.census.depthLimitHit = 0;
    sRenderer.census.texCacheHits = 0;
    sRenderer.census.texCacheMisses = 0;
    sRenderer.census.texRectDrawSkipped = 0;
#if PSP_RENDERER_DIAGNOSTICS
    sRenderer.census.lightTransformLogged = 0;
    sRenderer.census.shadeSampleCount = 0;
    sRenderer.census.shadeLitCount = 0;
    sRenderer.census.shadeMinR = 255;
    sRenderer.census.shadeMinG = 255;
    sRenderer.census.shadeMinB = 255;
    sRenderer.census.shadeMaxR = 0;
    sRenderer.census.shadeMaxG = 0;
    sRenderer.census.shadeMaxB = 0;
    sRenderer.census.shadeSumR = 0;
    sRenderer.census.shadeSumG = 0;
    sRenderer.census.shadeSumB = 0;
    sRenderer.census.shadeDotMilliSum = 0;
    sRenderer.census.shadeDotSampleCount = 0;
    sRenderer.census.texFuncModulateCount = 0;
    sRenderer.census.texFuncReplaceCount = 0;
#endif

    for (i = 0; i < ARRAY_COUNT(sRenderer.census.unsupported); i++) {
        sRenderer.census.unsupported[i] = 0;
    }
}

static void psp_renderer_note_unsupported(u8 opcode) {
    if (sRenderer.census.unsupported[opcode] == 0) {
#if PSP_RENDERER_DIAGNOSTICS
        char line[96];
        char* out = line;

        out = psp_renderer_append_text(out, "[psp] renderer unsupported opcode ");
        out = psp_renderer_append_u32(out, opcode);
        *out = '\0';
        PspPlatform_LogLine(line);
#endif

        sRenderer.census.unsupportedCount++;
    }

    sRenderer.census.unsupported[opcode]++;
}

static void psp_renderer_reset_rdp_state(void) {
    sRenderer.rdp.primColor = psp_rgba32(255, 255, 255, 255);
    sRenderer.rdp.fillColor = psp_rgba32(0, 0, 0, 255);
    sRenderer.rdp.envColor = psp_rgba32(255, 255, 255, 255);
    sRenderer.rdp.renderMode = G_RM_AA_OPA_SURF | G_RM_AA_OPA_SURF2;
    sRenderer.rdp.combineMode = PSP_COMBINE_UNKNOWN;
    sRenderer.rdp.textureImage = NULL;
    sRenderer.rdp.textureFmt = 0;
    sRenderer.rdp.textureSiz = 0;
    sRenderer.rdp.textureWidth = 0;
    sRenderer.rdp.tileWidth = 0;
    sRenderer.rdp.tileHeight = 0;
    sRenderer.rdp.paletteCount = 0;
    sRenderer.rsp.mode = 0;
    sRenderer.rsp.textureEnabled = 0;
    sRenderer.rsp.lightCount = 0;
    sRenderer.rsp.ambientR = 255;
    sRenderer.rsp.ambientG = 255;
    sRenderer.rsp.ambientB = 255;
    psp_renderer_clear_directional_lights();
    psp_renderer_identity_mtx(sRenderer.modelview);
    psp_renderer_identity_mtx(sRenderer.projection);
    sRenderer.hasModelview = 0;
    sRenderer.hasProjection = 0;
}

static void psp_renderer_add_range(const Gfx* start, const Gfx* end) {
    if ((start == NULL) || (end <= start) || (sRenderer.rangeCount >= ARRAY_COUNT(sRenderer.ranges))) {
        return;
    }
    sRenderer.ranges[sRenderer.rangeCount].start = start;
    sRenderer.ranges[sRenderer.rangeCount].end = end;
    sRenderer.rangeCount++;
}

static const Gfx* psp_renderer_find_static_dl_end(const Gfx* start, u32 maxCommands) {
    u32 i;

    if (start == NULL) {
        return NULL;
    }

    for (i = 0; i < maxCommands; i++) {
        if (psp_gfx_opcode(&start[i]) == PSP_GBI_OPCODE(G_ENDDL)) {
            return &start[i + 1];
        }
    }

    return NULL;
}

static const Gfx* psp_renderer_try_register_static_dl(const Gfx* start) {
    const Gfx* end;

    if ((start == NULL) || (((uintptr_t) start & 7U) != 0) || !PSP_IS_NATIVE_PTR(start)) {
        return NULL;
    }

    end = psp_renderer_find_static_dl_end(start, PSP_RENDERER_STATIC_DL_SCAN_COMMANDS);
    if (end != NULL) {
        psp_renderer_add_range(start, end);
    }
    return end;
}

static void psp_renderer_setup_task_ranges(SPTask* task) {
    GfxPool* pool = (GfxPool*) task;
    const Gfx* masterStart;
    const Gfx* masterEnd;
    const Gfx* title64End;

    sRenderer.rangeCount = 0;
    if (task == NULL) {
        return;
    }

    masterStart = (const Gfx*) task->task.t.data_ptr;
    masterEnd = masterStart + (task->task.t.data_size / sizeof(Gfx));

    psp_renderer_add_range(masterStart, masterEnd);
    psp_renderer_add_range(pool->unkDL1, pool->unkDL1 + ARRAY_COUNT(pool->unkDL1));
    psp_renderer_add_range(pool->unkDL2, pool->unkDL2 + ARRAY_COUNT(pool->unkDL2));

    title64End = psp_renderer_find_static_dl_end(aTitle64LogoDL, 256);
    if (title64End != NULL) {
        psp_renderer_add_range(aTitle64LogoDL, title64End);
    }
}

#if PSP_RENDERER_DIAGNOSTICS
static void psp_renderer_trace_task_range(SPTask* task, u32 taskIndex) {
    char line[160];
    char* out;

    if ((taskIndex > 4) && ((taskIndex % 30) != 0)) {
        return;
    }

    out = line;
    out = psp_renderer_append_text(out, "[psp] renderer task range:");
    psp_renderer_log_pair(&out, " task ", taskIndex);
    psp_renderer_log_pair(&out, " bytes ", task->task.t.data_size);
    psp_renderer_log_pair(&out, " cmds ", task->task.t.data_size / sizeof(Gfx));
    psp_renderer_log_pair(&out, " start ", (u32) task->task.t.data_ptr);
    *out = '\0';
    PspPlatform_LogLine(line);
}
#else
static void psp_renderer_trace_task_range(SPTask* task, u32 taskIndex) {
    (void) task;
    (void) taskIndex;
}
#endif

static const Gfx* psp_renderer_range_end_for(const Gfx* ptr) {
    u32 i;

    for (i = 0; i < sRenderer.rangeCount; i++) {
        if ((ptr >= sRenderer.ranges[i].start) && (ptr < sRenderer.ranges[i].end)) {
            return sRenderer.ranges[i].end;
        }
    }
    return NULL;
}

static int psp_renderer_ptr_has_commands(const Gfx* ptr, u32 count) {
    const Gfx* end = psp_renderer_range_end_for(ptr);

    if (end == NULL) {
        return 0;
    }
    return (ptr + count) <= end;
}

static void psp_renderer_handle_mtx(const Gfx* gfx) {
    const Mtx* mtx = (const Mtx*) gfx->words.w1;
    u32 flags = (gfx->words.w0 >> 16) & 0xFF;
    f32 loaded[4][4];
    f32 (*target)[4];
    int* hasTarget;
    int isProjection;

    if ((mtx == NULL) || !PSP_IS_NATIVE_PTR(mtx)) {
        sRenderer.census.validationFailures++;
        return;
    }

    isProjection = (flags & G_MTX_PROJECTION) != 0;
    if (isProjection) {
        target = sRenderer.projection;
        hasTarget = &sRenderer.hasProjection;
    } else {
        target = sRenderer.modelview;
        hasTarget = &sRenderer.hasModelview;
    }

    psp_renderer_mtx_l2f(loaded, mtx);
    if ((flags & G_MTX_LOAD) != 0) {
        psp_renderer_mtx_copy(target, loaded);
    } else if (*hasTarget) {
        psp_renderer_mtx_mul(target, target, loaded);
    } else {
        psp_renderer_mtx_copy(target, loaded);
    }
    *hasTarget = 1;
}

static void psp_renderer_init_buffers(void) {
    sceGuStart(GU_DIRECT, sGuList);

    sceGuDrawBuffer(GU_PSM_8888, (void*) 0, PSP_FRAMEBUFFER_WIDTH);
    sceGuDispBuffer(PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, (void*) 0x88000, PSP_FRAMEBUFFER_WIDTH);
    sceGuDepthBuffer((void*) 0x110000, PSP_FRAMEBUFFER_WIDTH);

    sceGuFinish();
    sceGuSync(0, 0);
}

static void psp_renderer_begin_frame(void) {
    sceGuStart(GU_DIRECT, sGuList);

    sceGuOffset(2048 - (PSP_SCREEN_WIDTH / 2), 2048 - (PSP_SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
    sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_TRUE);
    sceGuDepthRange(65535, 0);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);

    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuClearColor(psp_rgba32(0, 0, 0, 255));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

static void psp_renderer_end_frame(void) {
    sceGuFinish();
    sceGuSync(0, 0);
    //sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static int psp_renderer_sanitize_rect(s32* ulx, s32* uly, s32* lrx, s32* lry, int inclusive) {
    s32 x0 = *ulx;
    s32 y0 = *uly;
    s32 x1 = *lrx;
    s32 y1 = *lry;

    if (x1 < x0) {
        s32 temp = x0;
        x0 = x1;
        x1 = temp;
    }
    if (y1 < y0) {
        s32 temp = y0;
        y0 = y1;
        y1 = temp;
    }

    if (inclusive) {
        x1++;
        y1++;
    }

    if ((x1 < -PSP_SCREEN_GUARD) || (y1 < -PSP_SCREEN_GUARD) ||
        (x0 > (N64_SCREEN_WIDTH + PSP_SCREEN_GUARD)) || (y0 > (N64_SCREEN_HEIGHT + PSP_SCREEN_GUARD))) {
        sRenderer.census.validationFailures++;
        return 0;
    }

    x0 = psp_renderer_clamp_s32(x0, -PSP_SCREEN_GUARD, N64_SCREEN_WIDTH + PSP_SCREEN_GUARD);
    y0 = psp_renderer_clamp_s32(y0, -PSP_SCREEN_GUARD, N64_SCREEN_HEIGHT + PSP_SCREEN_GUARD);
    x1 = psp_renderer_clamp_s32(x1, -PSP_SCREEN_GUARD, N64_SCREEN_WIDTH + PSP_SCREEN_GUARD);
    y1 = psp_renderer_clamp_s32(y1, -PSP_SCREEN_GUARD, N64_SCREEN_HEIGHT + PSP_SCREEN_GUARD);

    if ((x1 <= x0) || (y1 <= y0)) {
        sRenderer.census.validationFailures++;
        return 0;
    }

    *ulx = x0;
    *uly = y0;
    *lrx = x1;
    *lry = y1;
    return 1;
}

static void psp_renderer_draw_solid_rect(s32 ulx, s32 uly, s32 lrx, s32 lry, u32 color, int inclusive) {
    PspVertex2DColor* v;
    s16 x0;
    s16 y0;
    s16 x1;
    s16 y1;

    if (!psp_renderer_sanitize_rect(&ulx, &uly, &lrx, &lry, inclusive)) {
        return;
    }

    x0 = (s16) PSP_COORD_X(ulx);
    y0 = (s16) PSP_COORD_Y(uly);
    x1 = (s16) PSP_COORD_X(lrx);
    y1 = (s16) PSP_COORD_Y(lry);

    v = (PspVertex2DColor*) sceGuGetMemory(6 * sizeof(PspVertex2DColor));
    if (v == NULL) {
        sRenderer.census.validationFailures++;
        return;
    }

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuEnable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);

    v[0].color = color;
    v[0].x = x0;
    v[0].y = y0;
    v[0].z = 0;

    v[1].color = color;
    v[1].x = x1;
    v[1].y = y0;
    v[1].z = 0;

    v[2].color = color;
    v[2].x = x0;
    v[2].y = y1;
    v[2].z = 0;

    v[3].color = color;
    v[3].x = x1;
    v[3].y = y0;
    v[3].z = 0;

    v[4].color = color;
    v[4].x = x1;
    v[4].y = y1;
    v[4].z = 0;

    v[5].color = color;
    v[5].x = x0;
    v[5].y = y1;
    v[5].z = 0;

    sceKernelDcacheWritebackRange(v, 6 * sizeof(PspVertex2DColor));

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(
        GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        6,
        0,
        v
    );
}

#include "src/psp/renderer_starfield.inc.c"

static void psp_renderer_draw_textured_rect(s32 ulx, s32 uly, s32 lrx, s32 lry, u32 width, u32 height, u32 sourceS, u32 sourceT) {
    PspRendererTexture texture;
    PspVertex2DTexture* vertices;
    u32 sourceStride;

    if (!psp_renderer_sanitize_rect(&ulx, &uly, &lrx, &lry, 0)) {
        return;
    }
    if ((width == 0) || (height == 0) || (sRenderer.rdp.textureImage == NULL)) {
        sRenderer.census.validationFailures++;
        return;
    }

    sourceStride = sRenderer.rdp.textureWidth;
    if (sourceStride == 0) {
        sourceStride = width;
    }
    if (sourceStride < width) {
        sourceStride = width;
    }

    if (!PspRendererTexture_Get(
        sRenderer.rdp.textureImage,
        sRenderer.rdp.textureFmt,
        sRenderer.rdp.textureSiz,
        width,
        height,
        sourceStride,
        sourceS,
        sourceT,
        sRenderer.rdp.palette,
        sRenderer.rdp.paletteCount,
        &texture
    )) {
        sRenderer.census.validationFailures++;
        psp_renderer_draw_solid_rect(ulx, uly, lrx, lry, sRenderer.rdp.primColor, 0);
        sRenderer.census.placeholderRectCount++;
        return;
    }
    if (texture.cacheHit) {
        sRenderer.census.texCacheHits++;
    } else {
        sRenderer.census.texCacheMisses++;
    }

    vertices = (PspVertex2DTexture*) sceGuGetMemory(4 * sizeof(PspVertex2DTexture));
    if (vertices == NULL) {
        sRenderer.census.validationFailures++;
        return;
    }
    u32 vertexColor = psp_rgba32(255, 255, 255, 255);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuEnable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);

    vertices[0].u = 0;
    vertices[0].v = 0;
    vertices[0].color = vertexColor;
    vertices[0].x = (s16) PSP_COORD_X(ulx);
    vertices[0].y = (s16) PSP_COORD_Y(uly);
    vertices[0].z = 0;

    vertices[1].u = (s16) width;
    vertices[1].v = 0;
    vertices[1].color = vertexColor;
    vertices[1].x = (s16) PSP_COORD_X(lrx);
    vertices[1].y = (s16) PSP_COORD_Y(uly);
    vertices[1].z = 0;

    vertices[2].u = 0;
    vertices[2].v = (s16) height;
    vertices[2].color = vertexColor;
    vertices[2].x = (s16) PSP_COORD_X(ulx);
    vertices[2].y = (s16) PSP_COORD_Y(lry);
    vertices[2].z = 0;

    vertices[3].u = (s16) width;
    vertices[3].v = (s16) height;
    vertices[3].color = vertexColor;
    vertices[3].x = (s16) PSP_COORD_X(lrx);
    vertices[3].y = (s16) PSP_COORD_Y(lry);
    vertices[3].z = 0;

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexImage(0, texture.textureWidth, texture.textureHeight, texture.textureWidth, texture.pixels);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f / (f32) texture.textureWidth, 1.0f / (f32) texture.textureHeight);
    sceGuTexOffset(0.0f, 0.0f);

    sceKernelDcacheWritebackRange(vertices, 4 * sizeof(PspVertex2DTexture));

    sceGuDrawArray(
        GU_TRIANGLE_STRIP,
        GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        4,
        0,
        vertices
    );

    sRenderer.census.texturedRectCount++;
}

static void psp_renderer_handle_setprimcolor(const Gfx* gfx) {
    u32 w1 = gfx->words.w1;
    u8 r = (u8) ((w1 >> 24) & 0xFF);
    u8 g = (u8) ((w1 >> 16) & 0xFF);
    u8 b = (u8) ((w1 >> 8) & 0xFF);
    u8 a = (u8) (w1 & 0xFF);

    sRenderer.rdp.primColor = psp_rgba32(r, g, b, a);
}

static void psp_renderer_handle_setenvcolor(const Gfx* gfx) {
    u32 w1 = gfx->words.w1;
    u8 r = (u8) ((w1 >> 24) & 0xFF);
    u8 g = (u8) ((w1 >> 16) & 0xFF);
    u8 b = (u8) ((w1 >> 8) & 0xFF);
    u8 a = (u8) (w1 & 0xFF);

    sRenderer.rdp.envColor = psp_rgba32(r, g, b, a);
}

static void psp_renderer_handle_setfillcolor(const Gfx* gfx) {
    u16 color16 = (u16) ((gfx->words.w1 >> 16) & 0xFFFF);

    sRenderer.rdp.fillColor = psp_rgba16_to_8888(color16);
}

static int psp_renderer_combine_cycle0_matches(u32 mux0, u32 mux1, u32 a, u32 b, u32 c, u32 d, u32 aa, u32 ab, u32 ac,
                                               u32 ad) {
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

static void psp_renderer_handle_setcombine(const Gfx* gfx) {
    u32 mux0 = gfx->words.w0 & 0x00FFFFFF;
    u32 mux1 = gfx->words.w1;

    if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_SHADE,
                                            G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_SHADE;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                   G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_PRIMITIVE)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_PRIMITIVE;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0,
                                                   G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_DECAL_RGB;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0,
                                                   G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_DECAL_RGBA;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                                   G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_SHADE)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_MODULATE_SHADE_ALPHA;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                                   G_CCMUX_0, G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_SHADE, G_ACMUX_0)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_MODULATE_SHADE_ALPHA;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_SHADE,
                                                   G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_MODULATE_SHADE_DECAL_ALPHA;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                   G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_PRIMITIVE)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_MODULATE_PRIM_ALPHA;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                   G_CCMUX_0, G_ACMUX_TEXEL0, G_ACMUX_0, G_ACMUX_PRIMITIVE,
                                                   G_ACMUX_0)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_MODULATE_PRIM_ALPHA;
    } else if (psp_renderer_combine_cycle0_matches(mux0, mux1, G_CCMUX_TEXEL0, G_CCMUX_0, G_CCMUX_PRIMITIVE,
                                                   G_CCMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_0, G_ACMUX_TEXEL0)) {
        sRenderer.rdp.combineMode = PSP_COMBINE_MODULATE_PRIM_ALPHA;
    } else {
        sRenderer.rdp.combineMode = PSP_COMBINE_UNKNOWN;
    }
}

static void psp_renderer_handle_setothermode_l(const Gfx* gfx) {
    u32 shift = (gfx->words.w0 >> 8) & 0xFF;
    u32 length = gfx->words.w0 & 0xFF;

    if ((shift == G_MDSFT_RENDERMODE) && (length == 29)) {
        sRenderer.rdp.renderMode = gfx->words.w1;
    }
}

static void psp_renderer_handle_settimg(const Gfx* gfx) {
    u32 w0 = gfx->words.w0;

    sRenderer.rdp.textureFmt = (w0 >> 21) & 0x7;
    sRenderer.rdp.textureSiz = (w0 >> 19) & 0x3;
    sRenderer.rdp.textureWidth = (w0 & 0xFFF) + 1;
    sRenderer.rdp.textureImage = (void*) gfx->words.w1;
    sRenderer.census.setTimgCount++;
}

static void psp_renderer_handle_settile(const Gfx* gfx) {
    u32 w0 = gfx->words.w0;
    u32 w1 = gfx->words.w1;
    u32 tile = (w1 >> 24) & 0x7;

    if (tile == G_TX_RENDERTILE) {
        sRenderer.rdp.textureFmt = (w0 >> 21) & 0x7;
        sRenderer.rdp.textureSiz = (w0 >> 19) & 0x3;
    }

    sRenderer.census.setTileCount++;
}

static void psp_renderer_handle_set_geometry_mode(const Gfx* gfx) {
    sRenderer.rsp.mode |= gfx->words.w1;
}

static void psp_renderer_handle_clear_geometry_mode(const Gfx* gfx) {
    sRenderer.rsp.mode &= ~gfx->words.w1;
}

static void psp_renderer_handle_texture(const Gfx* gfx) {
    sRenderer.rsp.textureEnabled = (gfx->words.w0 & 0xFF) != 0;
}

static void psp_renderer_handle_settilesize(const Gfx* gfx) {
    u32 w0 = gfx->words.w0;
    u32 w1 = gfx->words.w1;
    u32 uls = (w0 >> 12) & 0xFFF;
    u32 ult = w0 & 0xFFF;
    u32 lrs = (w1 >> 12) & 0xFFF;
    u32 lrt = w1 & 0xFFF;

    if ((lrs >= uls) && (lrt >= ult)) {
        sRenderer.rdp.tileWidth = ((lrs - uls) >> G_TEXTURE_IMAGE_FRAC) + 1;
        sRenderer.rdp.tileHeight = ((lrt - ult) >> G_TEXTURE_IMAGE_FRAC) + 1;
    }
    sRenderer.census.setTileSizeCount++;
}

static void psp_renderer_handle_loadtlut(const Gfx* gfx) {
    u32 i;
    u32 count = (((gfx->words.w1 >> 14) & 0x3FF) + 1);
    const u16* src = (const u16*) sRenderer.rdp.textureImage;

    if (count > ARRAY_COUNT(sRenderer.rdp.palette)) {
        count = ARRAY_COUNT(sRenderer.rdp.palette);
    }
    if (src != NULL) {
        for (i = 0; i < count; i++) {
            sRenderer.rdp.palette[i] = psp_rgba16_to_8888(src[i]);
        }
        sRenderer.rdp.paletteCount = count;
    }
    sRenderer.census.loadTlutCount++;
}

static void psp_renderer_handle_moveword(const Gfx* gfx) {
    u32 offset = (gfx->words.w0 >> 8) & 0xFFFF;
    u32 index = gfx->words.w0 & 0xFF;
    u32 data = gfx->words.w1;
    u32 lightCount;

    if ((index != G_MW_NUMLIGHT) || (offset != G_MWO_NUMLIGHT)) {
        return;
    }

    lightCount = ((data & 0x7FFFFFFF) / 32U);
    if (lightCount != 0) {
        lightCount--;
    }
    if (lightCount > PSP_RENDERER_MAX_LIGHTS) {
        lightCount = PSP_RENDERER_MAX_LIGHTS;
    }
    sRenderer.rsp.lightCount = lightCount;
    psp_renderer_clear_directional_lights();
#if PSP_RENDERER_DIAGNOSTICS
    {
        char line[160];

        snprintf(line, sizeof(line), "[psp] renderer gSPNumLights raw %lu dirCount %lu mode %lu",
                 (unsigned long) data,
                 (unsigned long) sRenderer.rsp.lightCount,
                 (unsigned long) sRenderer.rsp.mode);
        PspPlatform_LogLine(line);
    }
#endif
}

static void psp_renderer_handle_movemem(const Gfx* gfx) {
    const Light* src = (const Light*) gfx->words.w1;
    u32 index = (gfx->words.w0 >> 16) & 0xFF;
    u32 length = gfx->words.w0 & 0xFFFF;
    u32 lightSlot;

    if ((length < sizeof(Light)) || (src == NULL) || !PSP_IS_NATIVE_PTR(src)) {
        return;
    }

    if ((index < G_MV_L0) || (index > G_MV_L7) || (((index - G_MV_L0) & 1U) != 0)) {
        return;
    }

    lightSlot = (index - G_MV_L0) >> 1;
    if (lightSlot < sRenderer.rsp.lightCount) {
        sRenderer.rsp.lights[lightSlot].r = src->l.col[0];
        sRenderer.rsp.lights[lightSlot].g = src->l.col[1];
        sRenderer.rsp.lights[lightSlot].b = src->l.col[2];
        sRenderer.rsp.lights[lightSlot].x = src->l.dir[0];
        sRenderer.rsp.lights[lightSlot].y = src->l.dir[1];
        sRenderer.rsp.lights[lightSlot].z = src->l.dir[2];
#if PSP_RENDERER_DIAGNOSTICS
        {
            char line[192];

            snprintf(line, sizeof(line),
                     "[psp] renderer gSPLight slot %lu directional rgb %u,%u,%u dir %ld,%ld,%ld mode %lu",
                     (unsigned long) lightSlot,
                     src->l.col[0],
                     src->l.col[1],
                     src->l.col[2],
                     (long) src->l.dir[0],
                     (long) src->l.dir[1],
                     (long) src->l.dir[2],
                     (unsigned long) sRenderer.rsp.mode);
            PspPlatform_LogLine(line);
        }
#endif
    } else if (lightSlot == sRenderer.rsp.lightCount) {
        sRenderer.rsp.ambientR = src->l.col[0];
        sRenderer.rsp.ambientG = src->l.col[1];
        sRenderer.rsp.ambientB = src->l.col[2];
#if PSP_RENDERER_DIAGNOSTICS
        {
            char line[160];

            snprintf(line, sizeof(line), "[psp] renderer gSPLight slot %lu ambient rgb %u,%u,%u mode %lu",
                     (unsigned long) lightSlot,
                     src->l.col[0],
                     src->l.col[1],
                     src->l.col[2],
                     (unsigned long) sRenderer.rsp.mode);
            PspPlatform_LogLine(line);
        }
#endif
    }
}

static void psp_renderer_handle_fillrect(const Gfx* gfx) {
    u32 w0 = gfx->words.w0;
    u32 w1 = gfx->words.w1;
    s32 lrx = (s32) ((w0 >> 14) & 0x3FF) >> G_TEXTURE_IMAGE_FRAC;
    s32 lry = (s32) ((w0 >> 2) & 0x3FF) >> G_TEXTURE_IMAGE_FRAC;
    s32 ulx = (s32) ((w1 >> 14) & 0x3FF) >> G_TEXTURE_IMAGE_FRAC;
    s32 uly = (s32) ((w1 >> 2) & 0x3FF) >> G_TEXTURE_IMAGE_FRAC;

    sRenderer.census.fillRectCount++;
    psp_renderer_draw_solid_rect(ulx, uly, lrx, lry, sRenderer.rdp.fillColor, 1);
}

static int psp_renderer_validate_texrect(const Gfx* gfx) {
    if (!psp_renderer_ptr_has_commands(gfx, 3)) {
        sRenderer.census.validationFailures++;
        return 0;
    }
    if ((psp_gfx_opcode(&gfx[1]) != PSP_GBI_OPCODE(G_RDPHALF_1)) ||
        (psp_gfx_opcode(&gfx[2]) != PSP_GBI_OPCODE(G_RDPHALF_2))) {
        sRenderer.census.validationFailures++;
        return 0;
    }
    return 1;
}

static void psp_renderer_handle_texrect(const Gfx* gfx, int flipped) {
    u32 w0 = gfx[0].words.w0;
    u32 w1 = gfx[0].words.w1;
    s32 lrx = (s32) ((w0 >> 12) & 0xFFF) >> G_TEXTURE_IMAGE_FRAC;
    s32 lry = (s32) (w0 & 0xFFF) >> G_TEXTURE_IMAGE_FRAC;
    s32 ulx = (s32) ((w1 >> 12) & 0xFFF) >> G_TEXTURE_IMAGE_FRAC;
    s32 uly = (s32) (w1 & 0xFFF) >> G_TEXTURE_IMAGE_FRAC;
    s16 texSFixed = (s16) ((gfx[1].words.w1 >> 16) & 0xFFFF);
    s16 texTFixed = (s16) (gfx[1].words.w1 & 0xFFFF);
    s32 rectWidth;
    s32 rectHeight;
    u32 sourceS;
    u32 sourceT;
    u32 width;
    u32 height;

    if (flipped) {
        sRenderer.census.texRectFlipCount++;
    } else {
        sRenderer.census.texRectCount++;
    }

    sourceS = (u32) (texSFixed >> 5);
    sourceT = (u32) (texTFixed >> 5);

    rectWidth = (lrx >= ulx) ? (lrx - ulx) : (ulx - lrx);
    rectHeight = (lry >= uly) ? (lry - uly) : (uly - lry);

    width = sRenderer.rdp.tileWidth;
    height = sRenderer.rdp.tileHeight;

    if ((width == 0) || (height == 0)) {
        width = (u32) rectWidth;
        height = (u32) rectHeight;
    }

    if ((sRenderer.rdp.textureFmt == G_IM_FMT_RGBA) ||
        (sRenderer.rdp.textureFmt == G_IM_FMT_IA) ||
        (sRenderer.rdp.textureFmt == G_IM_FMT_CI)) {
            psp_renderer_draw_textured_rect(ulx, uly, lrx, lry, width, height, sourceS, sourceT);
    } else {
        psp_renderer_draw_solid_rect(ulx, uly, lrx, lry, sRenderer.rdp.primColor, 0);
        sRenderer.census.placeholderRectCount++;
    }
}

static void psp_renderer_execute_dl(const Gfx* start) {
    PspDlFrame stack[PSP_RENDERER_DL_MAX_DEPTH];
    const Gfx* pc = start;
    const Gfx* end = psp_renderer_range_end_for(start);
    u32 depth = 0;

    if (end == NULL) {
        sRenderer.census.rangeRejects++;
        return;
    }

    while (sRenderer.census.commandCount < PSP_RENDERER_DL_MAX_COMMANDS) {
        u8 opcode;

        if (pc >= end) {
            if (depth == 0) {
                return;
            }
            depth--;
            pc = stack[depth].pc;
            end = stack[depth].end;
            continue;
        }

        opcode = psp_gfx_opcode(pc);
        sRenderer.census.commandCount++;

        switch (opcode) {
            case G_NOOP:
            case G_RDPPIPESYNC:
            case G_RDPLOADSYNC:
            case G_RDPTILESYNC:
            case G_RDPFULLSYNC:
            case G_SETCIMG:
            case G_SETZIMG:
            case G_RDPSETOTHERMODE:
            case PSP_GBI_OPCODE(G_SETOTHERMODE_H):
            case G_SETSCISSOR:
            case PSP_GBI_OPCODE(G_RDPHALF_1):
            case PSP_GBI_OPCODE(G_RDPHALF_2):
                pc++;
                break;

            case G_SETCOMBINE:
                psp_renderer_handle_setcombine(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_SETOTHERMODE_L):
                psp_renderer_handle_setothermode_l(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_ENDDL):
                sRenderer.census.endDlCount++;
                if (depth == 0) {
                    return;
                }
                depth--;
                pc = stack[depth].pc;
                end = stack[depth].end;
                break;

            case G_DL: {
                const Gfx* target = (const Gfx*) pc->words.w1;
                const Gfx* targetEnd = psp_renderer_range_end_for(target);
                u32 param = (pc->words.w0 >> 16) & 0xFF;

                sRenderer.census.dlCount++;
#if PSP_RENDERER_DIAGNOSTICS
                if ((sRenderer.taskIndex <= 4) || ((sRenderer.taskIndex % 30) == 0)) {
                    char line[160];
                    char* out = line;

                    out = psp_renderer_append_text(out, "[psp] renderer dl:");
                    psp_renderer_log_pair(&out, " task ", sRenderer.taskIndex);
                    psp_renderer_log_pair(&out, " target ", (u32) target);
                    psp_renderer_log_pair(&out, " param ", param);
                    *out = '\0';
                    PspPlatform_LogLine(line);
                }
#endif
                if (targetEnd == NULL) {
                    targetEnd = psp_renderer_try_register_static_dl(target);
                }
                if (targetEnd == NULL) {
#if PSP_RENDERER_DIAGNOSTICS
                    char line[128];
                    char* out = line;

                    out = psp_renderer_append_text(out, "[psp] renderer: reject dl ");
                    psp_renderer_log_pair(&out, "target ", (u32) target);
                    *out = '\0';
                    PspPlatform_LogLine(line);
#endif

                    sRenderer.census.rangeRejects++;
                    pc++;
                    break;
                }
                if (param == G_DL_NOPUSH) {
                    pc = target;
                    end = targetEnd;
                    break;
                }
                if (depth >= ARRAY_COUNT(stack)) {
                    sRenderer.census.depthLimitHit++;
                    pc++;
                    break;
                }
                stack[depth].pc = pc + 1;
                stack[depth].end = end;
                depth++;
                pc = target;
                end = targetEnd;
                break;
            }

            case G_SETPRIMCOLOR:
                psp_renderer_handle_setprimcolor(pc);
                pc++;
                break;

            case G_SETENVCOLOR:
                psp_renderer_handle_setenvcolor(pc);
                pc++;
                break;

            case G_SETFILLCOLOR:
                psp_renderer_handle_setfillcolor(pc);
                pc++;
                break;

            case G_FILLRECT:
                psp_renderer_handle_fillrect(pc);
                pc++;
                break;

            case G_SETTIMG:
                psp_renderer_handle_settimg(pc);
                pc++;
                break;

            case G_SETTILE:
                psp_renderer_handle_settile(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_SETGEOMETRYMODE):
                psp_renderer_handle_set_geometry_mode(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_CLEARGEOMETRYMODE):
                psp_renderer_handle_clear_geometry_mode(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_TEXTURE):
                psp_renderer_handle_texture(pc);
                pc++;
                break;

            case G_SETTILESIZE:
                psp_renderer_handle_settilesize(pc);
                pc++;
                break;

            case G_LOADBLOCK:
                sRenderer.census.loadBlockCount++;
                pc++;
                break;

            case G_LOADTLUT:
                psp_renderer_handle_loadtlut(pc);
                pc++;
                break;

            case G_TEXRECT:
                if (psp_renderer_validate_texrect(pc)) {
                    psp_renderer_handle_texrect(pc, 0);
                    pc += 3;
                } else {
                    pc++;
                }
                break;

            case G_TEXRECTFLIP:
                if (psp_renderer_validate_texrect(pc)) {
                    psp_renderer_handle_texrect(pc, 1);
                    pc += 3;
                } else {
                    pc++;
                }
                break;

            case PSP_GBI_OPCODE(G_MTX):
                psp_renderer_handle_mtx(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_MOVEMEM):
                psp_renderer_handle_movemem(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_MOVEWORD):
                psp_renderer_handle_moveword(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_VTX):
                psp_renderer_handle_vtx(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_TRI1):
                psp_renderer_handle_tri1(pc);
                pc++;
                break;

            case PSP_GBI_OPCODE(G_TRI2):
                psp_renderer_handle_tri2(pc);
                pc++;
                break;

            default:
                psp_renderer_note_unsupported(opcode);
                pc++;
                break;
        }
    }

    sRenderer.census.commandLimitHit++;
    PspPlatform_LogLine("[psp] renderer: command limit");
}

void PspRenderer_Init(void) {
    if (sRendererReady) {
        return;
    }

    psp_renderer_reset_rdp_state();
    PspRendererTexture_Reset();

    sceGuInit();
    psp_renderer_init_buffers();
    psp_renderer_begin_frame();
    psp_renderer_end_frame();
    sceGuDisplay(GU_TRUE);

    sRendererReady = 1;
    PspPlatform_LogLine("[psp] renderer: init");
}

void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    const Gfx* dl;

    if (!sRendererReady) {
        PspRenderer_Init();
    }

    if (task == NULL) {
        return;
    }

    dl = (const Gfx*) task->task.t.data_ptr;
    psp_renderer_reset_census();
    psp_renderer_reset_rdp_state();
    sRenderer.taskIndex = taskIndex;
    psp_renderer_setup_task_ranges(task);
    sRenderer.census.taskBytes = task->task.t.data_size;
    psp_renderer_trace_task_range(task, taskIndex);

    psp_renderer_begin_frame();
    psp_renderer_draw_starfield_batch();
    psp_renderer_execute_dl(dl);
    psp_renderer_end_frame();

    sRenderer.census.frameCount++;
    psp_renderer_log_census(taskIndex);
}
