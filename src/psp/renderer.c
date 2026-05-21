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
#include <stdint.h>

#define PSP_RENDERER_DL_MAX_DEPTH 8
#define PSP_RENDERER_DL_MAX_COMMANDS 8192
#define PSP_RENDERER_UNSUPPORTED_BUCKETS 256
#define PSP_RENDERER_RANGES 32
#define PSP_RENDERER_STATIC_DL_SCAN_COMMANDS 512
#define PSP_SCREEN_WIDTH 480
#define PSP_SCREEN_HEIGHT 272
#define PSP_FRAMEBUFFER_WIDTH 512
#define N64_SCREEN_WIDTH 320
#define N64_SCREEN_HEIGHT 240
#define PSP_SCREEN_GUARD 128
#define PSP_RENDERER_MAX_STARS 512

#define PSP_PRESENT_SCALE ((f32) PSP_SCREEN_HEIGHT / (f32) N64_SCREEN_HEIGHT)
#define PSP_PRESENT_WIDTH ((f32) N64_SCREEN_WIDTH * PSP_PRESENT_SCALE)
#define PSP_PRESENT_HEIGHT ((f32) N64_SCREEN_HEIGHT * PSP_PRESENT_SCALE)
#define PSP_PRESENT_OFFSET_X (((f32) PSP_SCREEN_WIDTH - PSP_PRESENT_WIDTH) * 0.5f)
#define PSP_PRESENT_OFFSET_Y (((f32) PSP_SCREEN_HEIGHT - PSP_PRESENT_HEIGHT) * 0.5f)

#define PSP_COORD_X(x) (PSP_PRESENT_OFFSET_X + ((f32) (x) * PSP_PRESENT_SCALE))
#define PSP_COORD_Y(y) (PSP_PRESENT_OFFSET_Y + ((f32) (y) * PSP_PRESENT_SCALE))
#define PSP_GBI_OPCODE(cmd) ((u8) (cmd))

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
} PspRspVertex;

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
} PspRendererCensus;

typedef struct {
    u32 entered;
    u32 vtxLoads;
    u32 triangles;
    u32 drawn;
    u32 textureFailures;
    u32 transformFailures;
    u32 cullFailures;
    u32 hasModelview;
    u32 hasProjection;
    u32 fmt;
    u32 siz;
    u32 width;
    u32 height;
    u32 paletteCount;
    s32 minX;
    s32 minY;
    s32 maxX;
    s32 maxY;
    int hasBounds;
} PspTitle64Stats;

typedef struct {    
    PspRdpState rdp;
    PspRspVertex vertices[64];
    f32 modelview[4][4];
    f32 projection[4][4];
    int hasModelview;
    int hasProjection;
    PspRendererCensus census;
    PspTitle64Stats title64;
    PspDlRange ranges[PSP_RENDERER_RANGES];
    u32 rangeCount;
    u32 taskIndex;
    int title64Active;
    u32 dlTraceCount;
} PspRendererState;

typedef struct {
    s16 x;
    s16 y;
    u32 color;
} PspStarPoint;

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

static char* psp_renderer_append_text(char* out, const char* text) {
    while ((text != NULL) && (*text != '\0')) {
        *out++ = *text++;
    }
    return out;
}

static void psp_renderer_draw_rsp_triangle(u8 i0, u8 i1, u8 i2) {
    PspRendererTexture texture;
    PspVertex2DTexture* v;
    PspRspVertex* a;
    PspRspVertex* b;
    PspRspVertex* c;
    PspRspVertex* in[3];
    u32 textureWidth;
    u32 textureHeight;
    u32 sourceStride;
    u32 i;

    if (sRenderer.title64Active) {
        sRenderer.title64.triangles++;
        sRenderer.title64.hasModelview = sRenderer.hasModelview != 0;
        sRenderer.title64.hasProjection = sRenderer.hasProjection != 0;
        sRenderer.title64.fmt = sRenderer.rdp.textureFmt;
        sRenderer.title64.siz = sRenderer.rdp.textureSiz;
        sRenderer.title64.paletteCount = sRenderer.rdp.paletteCount;
    }

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

    textureWidth = sRenderer.rdp.tileWidth;
    textureHeight = sRenderer.rdp.tileHeight;
    if (textureWidth == 0) {
        textureWidth = sRenderer.rdp.textureWidth;
    }
    if (textureHeight == 0) {
        textureHeight = 1;
    }
    if (sRenderer.title64Active) {
        sRenderer.title64.width = textureWidth;
        sRenderer.title64.height = textureHeight;
    }
    sourceStride = sRenderer.rdp.textureWidth;
    if (sourceStride == 0) {
        sourceStride = textureWidth;
    }
    if (sourceStride < textureWidth) {
        sourceStride = textureWidth;
    }

    if ((sRenderer.rdp.textureImage == NULL) ||
        !PspRendererTexture_Get(
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
        )) {
        sRenderer.census.validationFailures++;
        if (sRenderer.title64Active) {
            sRenderer.title64.textureFailures++;
        }
        return;
    }
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
        f32 x;
        f32 y;
        f32 z;

        if (!psp_renderer_transform_vertex(in[i], &x, &y, &z)) {
            sRenderer.census.texRectDrawSkipped++;
            if (sRenderer.title64Active) {
                sRenderer.title64.transformFailures++;
            }
            return;
        }
        if ((x < -512.0f) || (x > 832.0f) || (y < -512.0f) || (y > 752.0f)) {
            sRenderer.census.texRectDrawSkipped++;
            if (sRenderer.title64Active) {
                sRenderer.title64.cullFailures++;
            }
            return;
        }
        if (sRenderer.title64Active) {
            s32 sx = (s32) x;
            s32 sy = (s32) y;

            if (!sRenderer.title64.hasBounds) {
                sRenderer.title64.minX = sx;
                sRenderer.title64.maxX = sx;
                sRenderer.title64.minY = sy;
                sRenderer.title64.maxY = sy;
                sRenderer.title64.hasBounds = 1;
            } else {
                if (sx < sRenderer.title64.minX) {
                    sRenderer.title64.minX = sx;
                }
                if (sx > sRenderer.title64.maxX) {
                    sRenderer.title64.maxX = sx;
                }
                if (sy < sRenderer.title64.minY) {
                    sRenderer.title64.minY = sy;
                }
                if (sy > sRenderer.title64.maxY) {
                    sRenderer.title64.maxY = sy;
                }
            }
        }

        v[i].u = in[i]->s >> 5;
        v[i].v = in[i]->t >> 5;
        v[i].color = psp_rgba32(255, 255, 255, 255);
        v[i].x = (s16) PSP_COORD_X(x);
        v[i].y = (s16) PSP_COORD_Y(y);
        v[i].z = (s16) z;
    }

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexImage(0, texture.textureWidth, texture.textureHeight, texture.textureWidth, texture.pixels);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
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

    sRenderer.census.texturedRectCount++;
    if (sRenderer.title64Active) {
        sRenderer.title64.drawn++;
    }
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

    if (sRenderer.title64Active) {
        sRenderer.title64.vtxLoads++;
    }

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

static char* psp_renderer_append_u32(char* out, u32 value) {
    char digits[10];
    s32 count = 0;

    if (value == 0) {
        *out++ = '0';
        return out;
    }

    while (value != 0) {
        digits[count++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        *out++ = digits[--count];
    }
    return out;
}

static char* psp_renderer_append_s32(char* out, s32 value) {
    if (value < 0) {
        *out++ = '-';
        value = -value;
    }
    return psp_renderer_append_u32(out, (u32) value);
}

static char* psp_renderer_append_f32_1000(char* out, f32 value) {
    s32 scaled = (s32) (value * 1000.0f);

    if (scaled < 0) {
        *out++ = '-';
        scaled = -scaled;
    }
    out = psp_renderer_append_u32(out, (u32) (scaled / 1000));
    *out++ = '.';
    *out++ = (char) ('0' + ((scaled / 100) % 10));
    *out++ = (char) ('0' + ((scaled / 10) % 10));
    *out++ = (char) ('0' + (scaled % 10));
    return out;
}

static void psp_renderer_log_pair(char** out, const char* label, u32 value) {
    *out = psp_renderer_append_text(*out, label);
    *out = psp_renderer_append_u32(*out, value);
}

static void psp_renderer_log_pair_s32(char** out, const char* label, s32 value) {
    *out = psp_renderer_append_text(*out, label);
    *out = psp_renderer_append_s32(*out, value);
}

static void psp_renderer_log_matrix(const char* label, f32 mtx[4][4]) {
    char line[256];
    u32 row;

    for (row = 0; row < 4; row++) {
        char* out = line;

        out = psp_renderer_append_text(out, "[psp] renderer title64 ");
        out = psp_renderer_append_text(out, label);
        psp_renderer_log_pair(&out, " r", row);
        out = psp_renderer_append_text(out, " ");
        out = psp_renderer_append_f32_1000(out, mtx[row][0]);
        out = psp_renderer_append_text(out, " ");
        out = psp_renderer_append_f32_1000(out, mtx[row][1]);
        out = psp_renderer_append_text(out, " ");
        out = psp_renderer_append_f32_1000(out, mtx[row][2]);
        out = psp_renderer_append_text(out, " ");
        out = psp_renderer_append_f32_1000(out, mtx[row][3]);
        *out = '\0';
        PspPlatform_LogLine(line);
    }
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
    sRenderer.title64.entered = 0;
    sRenderer.title64.vtxLoads = 0;
    sRenderer.title64.triangles = 0;
    sRenderer.title64.drawn = 0;
    sRenderer.title64.textureFailures = 0;
    sRenderer.title64.transformFailures = 0;
    sRenderer.title64.cullFailures = 0;
    sRenderer.title64.hasModelview = 0;
    sRenderer.title64.hasProjection = 0;
    sRenderer.title64.fmt = 0;
    sRenderer.title64.siz = 0;
    sRenderer.title64.width = 0;
    sRenderer.title64.height = 0;
    sRenderer.title64.paletteCount = 0;
    sRenderer.title64.minX = 0;
    sRenderer.title64.minY = 0;
    sRenderer.title64.maxX = 0;
    sRenderer.title64.maxY = 0;
    sRenderer.title64.hasBounds = 0;
    sRenderer.title64Active = 0;
    sRenderer.dlTraceCount = 0;

    for (i = 0; i < ARRAY_COUNT(sRenderer.census.unsupported); i++) {
        sRenderer.census.unsupported[i] = 0;
    }
}

static void psp_renderer_note_unsupported(u8 opcode) {
    if (sRenderer.census.unsupported[opcode] == 0) {
        char line[96];
        char* out = line;

        out = psp_renderer_append_text(out, "[psp] renderer unsupported opcode ");
        out = psp_renderer_append_u32(out, opcode);
        *out = '\0';
        PspPlatform_LogLine(line);

        sRenderer.census.unsupportedCount++;
    }

    sRenderer.census.unsupported[opcode]++;
}

static u8 psp_renderer_top_unsupported(void) {
    u32 i;
    u32 bestCount = 0;
    u8 bestOpcode = 0;

    for (i = 0; i < ARRAY_COUNT(sRenderer.census.unsupported); i++) {
        if (sRenderer.census.unsupported[i] > bestCount) {
            bestCount = sRenderer.census.unsupported[i];
            bestOpcode = (u8) i;
        }
    }
    return bestOpcode;
}

static void psp_renderer_log_title64(void) {
    char line[256];
    char* out = line;
    f32 originX;
    f32 originY;
    f32 originZ;

    if (sRenderer.title64.entered == 0) {
        return;
    }

    out = psp_renderer_append_text(out, "[psp] renderer title64:");
    psp_renderer_log_pair(&out, " enter ", sRenderer.title64.entered);
    psp_renderer_log_pair(&out, " vtx ", sRenderer.title64.vtxLoads);
    psp_renderer_log_pair(&out, " tri ", sRenderer.title64.triangles);
    psp_renderer_log_pair(&out, " drawn ", sRenderer.title64.drawn);
    psp_renderer_log_pair(&out, " texfail ", sRenderer.title64.textureFailures);
    psp_renderer_log_pair(&out, " xformfail ", sRenderer.title64.transformFailures);
    psp_renderer_log_pair(&out, " cull ", sRenderer.title64.cullFailures);
    *out = '\0';
    PspPlatform_LogLine(line);

    out = line;
    out = psp_renderer_append_text(out, "[psp] renderer title64 state:");
    psp_renderer_log_pair(&out, " mv ", sRenderer.title64.hasModelview);
    psp_renderer_log_pair(&out, " pr ", sRenderer.title64.hasProjection);
    psp_renderer_log_pair(&out, " fmt ", sRenderer.title64.fmt);
    psp_renderer_log_pair(&out, " siz ", sRenderer.title64.siz);
    psp_renderer_log_pair(&out, " w ", sRenderer.title64.width);
    psp_renderer_log_pair(&out, " h ", sRenderer.title64.height);
    psp_renderer_log_pair(&out, " pal ", sRenderer.title64.paletteCount);
    *out = '\0';
    PspPlatform_LogLine(line);

    if ((sRenderer.hasModelview != 0) && (sRenderer.hasProjection != 0) &&
        psp_renderer_project_model_point(0.0f, 0.0f, 0.0f, &originX, &originY, &originZ)) {
        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer title64 origin:");
        out = psp_renderer_append_text(out, " x ");
        out = psp_renderer_append_f32_1000(out, originX);
        out = psp_renderer_append_text(out, " y ");
        out = psp_renderer_append_f32_1000(out, originY);
        out = psp_renderer_append_text(out, " z ");
        out = psp_renderer_append_f32_1000(out, originZ);
        *out = '\0';
        PspPlatform_LogLine(line);
    }

    if (sRenderer.title64.hasBounds) {
        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer title64 bounds:");
        psp_renderer_log_pair_s32(&out, " x0 ", sRenderer.title64.minX);
        psp_renderer_log_pair_s32(&out, " y0 ", sRenderer.title64.minY);
        psp_renderer_log_pair_s32(&out, " x1 ", sRenderer.title64.maxX);
        psp_renderer_log_pair_s32(&out, " y1 ", sRenderer.title64.maxY);
        *out = '\0';
        PspPlatform_LogLine(line);

        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer title64 present:");
        psp_renderer_log_pair_s32(&out, " x0 ", (s32) PSP_COORD_X(sRenderer.title64.minX));
        psp_renderer_log_pair_s32(&out, " y0 ", (s32) PSP_COORD_Y(sRenderer.title64.minY));
        psp_renderer_log_pair_s32(&out, " x1 ", (s32) PSP_COORD_X(sRenderer.title64.maxX));
        psp_renderer_log_pair_s32(&out, " y1 ", (s32) PSP_COORD_Y(sRenderer.title64.maxY));
        *out = '\0';
        PspPlatform_LogLine(line);
    }

    if ((sRenderer.taskIndex <= 35) || ((sRenderer.taskIndex % 30) == 0)) {
        psp_renderer_log_matrix("modelview", sRenderer.modelview);
        psp_renderer_log_matrix("projection", sRenderer.projection);
    }
}

static void psp_renderer_log_census(u32 taskIndex) {
    char line[256];
    char* out = line;

    psp_renderer_log_title64();

    if ((taskIndex > 4) && ((taskIndex % 30) != 0)) {
        return;
    }

    out = psp_renderer_append_text(out, "[psp] renderer:");
    psp_renderer_log_pair(&out, " task ", taskIndex);
    psp_renderer_log_pair(&out, " bytes ", sRenderer.census.taskBytes);
    psp_renderer_log_pair(&out, " cmds ", sRenderer.census.commandCount);
    psp_renderer_log_pair(&out, " dl ", sRenderer.census.dlCount);
    psp_renderer_log_pair(&out, " fill ", sRenderer.census.fillRectCount);
    psp_renderer_log_pair(&out, " tex ", sRenderer.census.texRectCount + sRenderer.census.texRectFlipCount);
    psp_renderer_log_pair(&out, " drawn ", sRenderer.census.texturedRectCount + sRenderer.census.placeholderRectCount);
    psp_renderer_log_pair(&out, " skip ", sRenderer.census.texRectDrawSkipped);
    psp_renderer_log_pair(&out, " bad ", sRenderer.census.validationFailures + sRenderer.census.rangeRejects);
    *out = '\0';
    PspPlatform_LogLine(line);

    if (sRenderer.census.unsupportedCount != 0) {
        out = line;
        out = psp_renderer_append_text(out, "[psp] renderer unsupported:");
        psp_renderer_log_pair(&out, " kinds ", sRenderer.census.unsupportedCount);
        psp_renderer_log_pair(&out, " top ", psp_renderer_top_unsupported());
        *out = '\0';
        PspPlatform_LogLine(line);
    }

}

static void psp_renderer_reset_rdp_state(void) {
    sRenderer.rdp.primColor = psp_rgba32(255, 255, 255, 255);
    sRenderer.rdp.fillColor = psp_rgba32(0, 0, 0, 255);
    sRenderer.rdp.envColor = psp_rgba32(255, 255, 255, 255);
    sRenderer.rdp.textureImage = NULL;
    sRenderer.rdp.textureFmt = 0;
    sRenderer.rdp.textureSiz = 0;
    sRenderer.rdp.textureWidth = 0;
    sRenderer.rdp.tileWidth = 0;
    sRenderer.rdp.tileHeight = 0;
    sRenderer.rdp.paletteCount = 0;
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
        PspPlatform_LogLine("[psp] renderer: registered aTitle64LogoDL");
    }
}

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
    psp_renderer_log_pair(&out, " title64 ", (u32) aTitle64LogoDL);
    *out = '\0';
    PspPlatform_LogLine(line);
}

static const Gfx* psp_renderer_range_end_for(const Gfx* ptr) {
    u32 i;

    for (i = 0; i < sRenderer.rangeCount; i++) {
        if ((ptr >= sRenderer.ranges[i].start) && (ptr < sRenderer.ranges[i].end)) {
            return sRenderer.ranges[i].end;
        }
    }
    return NULL;
}

static int psp_renderer_is_title64_pc(const Gfx* ptr) {
    const Gfx* title64End = psp_renderer_range_end_for(aTitle64LogoDL);

    return (title64End != NULL) && (ptr >= aTitle64LogoDL) && (ptr < title64End);
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

    if ((mtx == NULL) || !PSP_IS_NATIVE_PTR(mtx)) {
        sRenderer.census.validationFailures++;
        return;
    }

    if ((flags & G_MTX_PROJECTION) != 0) {
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

static void psp_renderer_begin_frame(void) {
    sceGuStart(GU_DIRECT, sGuList);

    sceGuDrawBuffer(GU_PSM_8888, (void*) 0, PSP_FRAMEBUFFER_WIDTH);
    sceGuDispBuffer(PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, (void*) 0x88000, PSP_FRAMEBUFFER_WIDTH);
    sceGuDepthBuffer((void*) 0x110000, PSP_FRAMEBUFFER_WIDTH);

    sceGuOffset(2048 - (PSP_SCREEN_WIDTH / 2), 2048 - (PSP_SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
    sceGuScissor(0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);

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

static void psp_renderer_draw_starfield_batch(void) {
    PspVertex2DColor* vertices;
    u32 i;
    u32 out = 0;

    if (!sStarfieldReady || (sStarfieldCount == 0)) {
        return;
    }

    vertices = (PspVertex2DColor*) sceGuGetMemory(sStarfieldCount * 6 * sizeof(PspVertex2DColor));
    if (vertices == NULL) {
        sRenderer.census.validationFailures++;
        sStarfieldReady = 0;
        return;
    }

    for (i = 0; i < sStarfieldCount; i++) {
        PspStarPoint* star = &sStarfieldStars[i];
        s16 x0 = (s16) PSP_COORD_X(star->x);
        s16 y0 = (s16) PSP_COORD_Y(star->y);
        s16 x1 = (s16) PSP_COORD_X(star->x + 1);
        s16 y1 = (s16) PSP_COORD_Y(star->y + 1);
        u32 color = star->color;

        vertices[out].color = color;
        vertices[out].x = x0;
        vertices[out].y = y0;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x1;
        vertices[out].y = y0;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x0;
        vertices[out].y = y1;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x1;
        vertices[out].y = y0;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x1;
        vertices[out].y = y1;
        vertices[out].z = 0;
        out++;

        vertices[out].color = color;
        vertices[out].x = x0;
        vertices[out].y = y1;
        vertices[out].z = 0;
        out++;
    }

    sceKernelDcacheWritebackRange(vertices, out * sizeof(PspVertex2DColor));

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(
        GU_TRIANGLES,
        GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        out,
        0,
        vertices
    );

    // consume once so a frame that does not submit stars does not keep drawing stale stars

    sStarfieldReady = 0;
}

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
        sRenderer.title64Active = psp_renderer_is_title64_pc(pc);
        sRenderer.census.commandCount++;

        switch (opcode) {
            case G_NOOP:
            case G_RDPPIPESYNC:
            case G_RDPLOADSYNC:
            case G_RDPTILESYNC:
            case G_RDPFULLSYNC:
            case G_SETCOMBINE:
            case G_SETCIMG:
            case G_SETZIMG:
            case G_RDPSETOTHERMODE:
            case PSP_GBI_OPCODE(G_SETOTHERMODE_H):
            case PSP_GBI_OPCODE(G_SETOTHERMODE_L):
            case G_SETSCISSOR:
            case PSP_GBI_OPCODE(G_SETGEOMETRYMODE):
            case PSP_GBI_OPCODE(G_CLEARGEOMETRYMODE):
            case PSP_GBI_OPCODE(G_TEXTURE):
            case PSP_GBI_OPCODE(G_RDPHALF_1):
            case PSP_GBI_OPCODE(G_RDPHALF_2):
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
                if (((sRenderer.taskIndex <= 4) || ((sRenderer.taskIndex % 30) == 0) ||
                     (target == aTitle64LogoDL)) && (sRenderer.dlTraceCount < 12)) {
                    char line[160];
                    char* out = line;

                    out = psp_renderer_append_text(out, "[psp] renderer dl:");
                    psp_renderer_log_pair(&out, " task ", sRenderer.taskIndex);
                    psp_renderer_log_pair(&out, " target ", (u32) target);
                    psp_renderer_log_pair(&out, " title64 ", target == aTitle64LogoDL);
                    psp_renderer_log_pair(&out, " param ", param);
                    *out = '\0';
                    PspPlatform_LogLine(line);
                    sRenderer.dlTraceCount++;
                }
                if (target == aTitle64LogoDL) {
                    sRenderer.title64.entered++;
                }
                if (targetEnd == NULL) {
                    targetEnd = psp_renderer_try_register_static_dl(target);
                }
                if (targetEnd == NULL) {
                    char line[128];
                    char* out = line;

                    out = psp_renderer_append_text(out, "[psp] renderer: reject dl ");
                    psp_renderer_log_pair(&out, "target ", (u32) target);
                    *out = '\0';
                    PspPlatform_LogLine(line);

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
            case PSP_GBI_OPCODE(G_MOVEWORD):
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

void PspRenderer_BeginStarfield(void) {
    sStarfieldCount = 0;
    sStarfieldReady = 0;
}

void PspRenderer_AddStar(s16 x, s16 y, u32 color) {
    PspStarPoint* star;

    if (sStarfieldCount >= ARRAY_COUNT(sStarfieldStars)) {
        return;
    }

    star = &sStarfieldStars[sStarfieldCount++];
    star->x = x;
    star->y = y;
    star->color = color;
}

void PspRenderer_EndStarfield(void) {
    sStarfieldReady = 1;
}

void PspRenderer_Init(void) {
    if (sRendererReady) {
        return;
    }

    psp_renderer_reset_rdp_state();
    PspRendererTexture_Reset();

    sceGuInit();
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
    psp_renderer_setup_task_ranges(task);
    sRenderer.census.taskBytes = task->task.t.data_size;
    sRenderer.taskIndex = taskIndex;
    psp_renderer_trace_task_range(task, taskIndex);

    psp_renderer_begin_frame();
    psp_renderer_draw_starfield_batch();
    psp_renderer_execute_dl(dl);
    psp_renderer_end_frame();

    sRenderer.census.frameCount++;
    psp_renderer_log_census(taskIndex);
}
