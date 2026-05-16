#include "PR/ultratypes.h"
#include "libultra/ultra64.h"
#include "sf64thread.h"
#include "macros.h"
#include "src/psp/platform.h"
#include "src/psp/renderer.h"

#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psputils.h>

#define PSP_RENDERER_DL_MAX_DEPTH 8
#define PSP_RENDERER_DL_MAX_COMMANDS 8192
#define PSP_RENDERER_UNSUPPORTED_BUCKETS 256
#define PSP_RENDERER_RANGES 4
#define PSP_RENDERER_TEX_CACHE_SLOTS 16
#define PSP_RENDERER_TEX_MAX_WIDTH 128
#define PSP_RENDERER_TEX_MAX_HEIGHT 128
#define PSP_RENDERER_TEX_MAX_PIXELS (PSP_RENDERER_TEX_MAX_WIDTH * PSP_RENDERER_TEX_MAX_HEIGHT)

#define PSP_SCREEN_WIDTH 480
#define PSP_SCREEN_HEIGHT 272
#define PSP_FRAMEBUFFER_WIDTH 512
#define N64_SCREEN_WIDTH 320
#define N64_SCREEN_HEIGHT 240
#define PSP_SCREEN_GUARD 128
#define PSP_RENDERER_TEXRECT_SKIP 0
#define PSP_RENDERER_TEXRECT_SOLID 1
#define PSP_RENDERER_TEXRECT_TEXTURED 2
#define PSP_RENDERER_TEXRECT_MODE PSP_RENDERER_TEXRECT_SOLID

#define PSP_COORD_X(x) ((f32) (x) * 1.5f)
#define PSP_COORD_Y(y) ((f32) (y) + 16.0f)
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
    const Gfx* start;
    const Gfx* end;
} PspDlRange;

typedef struct {
    const Gfx* pc;
    const Gfx* end;
} PspDlFrame;

typedef struct {
    const void* source;
    u32 fmt;
    u32 siz;
    u32 width;
    u32 height;
    u32 age;
    u32 pixels[PSP_RENDERER_TEX_MAX_PIXELS] __attribute__((aligned(64)));
} PspTextureCacheEntry;

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
    u32 texRectDebugLogged;
} PspRendererCensus;

typedef struct {    
    PspRdpState rdp;
    PspRendererCensus census;
    PspDlRange ranges[PSP_RENDERER_RANGES];
    u32 rangeCount;
    u32 textureAge;
    u32 taskIndex;
} PspRendererState;

static unsigned int sGuList[262144] __attribute__((aligned(64)));
static PspTextureCacheEntry sTextureCache[PSP_RENDERER_TEX_CACHE_SLOTS];
static PspRendererState sRenderer;
static int sRendererReady;
static u32 sTexRectLogBudget = 12;

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

static char* psp_renderer_append_text(char* out, const char* text) {
    while ((text != NULL) && (*text != '\0')) {
        *out++ = *text++;
    }
    return out;
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

static void psp_renderer_log_pair(char** out, const char* label, u32 value) {
    *out = psp_renderer_append_text(*out, label);
    *out = psp_renderer_append_u32(*out, value);
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
    sRenderer.census.texRectDebugLogged = 0;

    for (i = 0; i < ARRAY_COUNT(sRenderer.census.unsupported); i++) {
        sRenderer.census.unsupported[i] = 0;
    }
}

static void psp_renderer_log_texrect(s32 ulx, s32 uly, s32 lrx, s32 lry, u32 width, u32 height) {
    char line[192];
    char* out = line;

    if ((sTexRectLogBudget == 0) && ((sRenderer.taskIndex > 4) && ((sRenderer.taskIndex % 30) != 0))) {
        return;
    }
    if (sTexRectLogBudget != 0) {
        sTexRectLogBudget--;
    } else {
        if (sRenderer.census.texRectDebugLogged >= 6) {
            return;
        }
        sRenderer.census.texRectDebugLogged++;
    }

    out = psp_renderer_append_text(out, "[psp] texrect:");
    psp_renderer_log_pair(&out, " fmt ", sRenderer.rdp.textureFmt);
    psp_renderer_log_pair(&out, " siz ", sRenderer.rdp.textureSiz);
    psp_renderer_log_pair(&out, " tw ", sRenderer.rdp.tileWidth);
    psp_renderer_log_pair(&out, " th ", sRenderer.rdp.tileHeight);
    psp_renderer_log_pair(&out, " w ", width);
    psp_renderer_log_pair(&out, " h ", height);
    out = psp_renderer_append_text(out, " xy ");
    out = psp_renderer_append_s32(out, ulx);
    out = psp_renderer_append_text(out, ",");
    out = psp_renderer_append_s32(out, uly);
    out = psp_renderer_append_text(out, " -> ");
    out = psp_renderer_append_s32(out, lrx);
    out = psp_renderer_append_text(out, ",");
    out = psp_renderer_append_s32(out, lry);
    *out = '\0';
    PspPlatform_LogLine(line);
}

static void psp_renderer_note_unsupported(u8 opcode) {
    if (sRenderer.census.unsupported[opcode] == 0) {
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

static void psp_renderer_log_census(u32 taskIndex) {
    char line[256];
    char* out = line;

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
}

static void psp_renderer_add_range(const Gfx* start, const Gfx* end) {
    if ((start == NULL) || (end <= start) || (sRenderer.rangeCount >= ARRAY_COUNT(sRenderer.ranges))) {
        return;
    }
    sRenderer.ranges[sRenderer.rangeCount].start = start;
    sRenderer.ranges[sRenderer.rangeCount].end = end;
    sRenderer.rangeCount++;
}

static void psp_renderer_setup_task_ranges(SPTask* task) {
    GfxPool* pool = (GfxPool*) task;
    const Gfx* masterStart;
    const Gfx* masterEnd;

    sRenderer.rangeCount = 0;
    if (task == NULL) {
        return;
    }

    masterStart = (const Gfx*) task->task.t.data_ptr;
    masterEnd = masterStart + (task->task.t.data_size / sizeof(Gfx));

    psp_renderer_add_range(masterStart, masterEnd);
    psp_renderer_add_range(pool->unkDL1, pool->unkDL1 + ARRAY_COUNT(pool->unkDL1));
    psp_renderer_add_range(pool->unkDL2, pool->unkDL2 + ARRAY_COUNT(pool->unkDL2));
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

static int psp_renderer_ptr_has_commands(const Gfx* ptr, u32 count) {
    const Gfx* end = psp_renderer_range_end_for(ptr);

    if (end == NULL) {
        return 0;
    }
    return (ptr + count) <= end;
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

static void psp_renderer_draw_texrect_solid(s32 ulx, s32 uly, s32 lrx, s32 lry, u32 color) {
    psp_renderer_draw_solid_rect(ulx, uly, lrx, lry, color, 0);
}

static int psp_renderer_validate_texrect_bounds(s32 ulx, s32 uly, s32 lrx, s32 lry) {
    s32 width;
    s32 height;

    if ((ulx < 0) || (uly < 0) || (lrx <= ulx) || (lry <= uly) || (lrx > N64_SCREEN_WIDTH) ||
        (lry > N64_SCREEN_HEIGHT)) {
        sRenderer.census.validationFailures++;
        return 0;
    }

    width = lrx - ulx;
    height = lry - uly;
    if ((width > N64_SCREEN_WIDTH) || (height > N64_SCREEN_HEIGHT)) {
        sRenderer.census.validationFailures++;
        return 0;
    }
    return 1;
}

static u32 psp_renderer_next_pow2(u32 value) {
    u32 result = 1;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

static PspTextureCacheEntry* psp_renderer_find_texture_slot(const void* source, u32 fmt, u32 siz, u32 width, u32 height) {
    PspTextureCacheEntry* oldest = &sTextureCache[0];
    u32 i;

    for (i = 0; i < ARRAY_COUNT(sTextureCache); i++) {
        if ((sTextureCache[i].source == source) && (sTextureCache[i].fmt == fmt) && (sTextureCache[i].siz == siz) &&
            (sTextureCache[i].width == width) && (sTextureCache[i].height == height)) {
            sTextureCache[i].age = ++sRenderer.textureAge;
            sRenderer.census.texCacheHits++;
            return &sTextureCache[i];
        }
        if ((sTextureCache[i].source == NULL) || (sTextureCache[i].age < oldest->age)) {
            oldest = &sTextureCache[i];
        }
    }

    oldest->source = source;
    oldest->fmt = fmt;
    oldest->siz = siz;
    oldest->width = width;
    oldest->height = height;
    oldest->age = ++sRenderer.textureAge;
    sRenderer.census.texCacheMisses++;
    return oldest;
}

static int psp_renderer_convert_texture(PspTextureCacheEntry* entry, u32 width, u32 height) {
    u32 x;
    u32 y;
    u32 texWidth = psp_renderer_next_pow2(width);
    u32 texHeight = psp_renderer_next_pow2(height);

    if ((entry == NULL) || (entry->source == NULL) || (texWidth > PSP_RENDERER_TEX_MAX_WIDTH) ||
        (texHeight > PSP_RENDERER_TEX_MAX_HEIGHT)) {
        sRenderer.census.validationFailures++;
        return 0;
    }

    for (y = 0; y < texHeight; y++) {
        for (x = 0; x < texWidth; x++) {
            entry->pixels[(y * texWidth) + x] = 0;
        }
    }

    if ((entry->fmt == G_IM_FMT_RGBA) && (entry->siz == G_IM_SIZ_16b)) {
        const u16* src = (const u16*) entry->source;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                entry->pixels[(y * texWidth) + x] = psp_rgba16_to_8888(src[(y * width) + x]);
            }
        }
    } else if ((entry->fmt == G_IM_FMT_IA) && (entry->siz == G_IM_SIZ_8b)) {
        const u8* src = (const u8*) entry->source;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                u8 packed = src[(y * width) + x];
                u8 intensity = (u8) (((packed >> 4) & 0xF) * 17);
                u8 alpha = (u8) ((packed & 0xF) * 17);

                entry->pixels[(y * texWidth) + x] = psp_rgba32(intensity, intensity, intensity, alpha);
            }
        }
        
    } else if ((entry->fmt == G_IM_FMT_IA) && (entry->siz == G_IM_SIZ_16b)) {
        const u16* src = (const u16*) entry->source;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                u16 packed = src[(y * width) + x];
                u8 intensity = (u8) ((packed >> 8) & 0xFF);
                u8 alpha = (u8) (packed & 0xFF);

                entry->pixels[(y * texWidth) + x] = psp_rgba32(intensity, intensity, intensity, alpha);
            }
        }
    } else {
        return 0;
    }

    sceKernelDcacheWritebackRange(entry->pixels, texWidth * texHeight * sizeof(u32));
    return 1;
}

static void psp_renderer_draw_textured_rect(s32 ulx, s32 uly, s32 lrx, s32 lry, u32 width, u32 height) {
    PspTextureCacheEntry* entry;
    PspVertex2DTexture* vertices;
    u32 texWidth;
    u32 texHeight;

    if (!psp_renderer_sanitize_rect(&ulx, &uly, &lrx, &lry, 0)) {
        return;
    }
    if ((width == 0) || (height == 0) || (sRenderer.rdp.textureImage == NULL)) {
        sRenderer.census.validationFailures++;
        return;
    }

    texWidth = psp_renderer_next_pow2(width);
    texHeight = psp_renderer_next_pow2(height);
    entry = psp_renderer_find_texture_slot(sRenderer.rdp.textureImage, sRenderer.rdp.textureFmt, sRenderer.rdp.textureSiz, width,
                                           height);
    if (!psp_renderer_convert_texture(entry, width, height)) {
        psp_renderer_draw_solid_rect(ulx, uly, lrx, lry, sRenderer.rdp.primColor, 0);
        sRenderer.census.placeholderRectCount++;
        return;
    }

    vertices = (PspVertex2DTexture*) sceGuGetMemory(4 * sizeof(PspVertex2DTexture));
    if (vertices == NULL) {
        sRenderer.census.validationFailures++;
        return;
    }

    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[0].color = sRenderer.rdp.primColor;
    vertices[0].x = PSP_COORD_X(ulx);
    vertices[0].y = PSP_COORD_Y(uly);
    vertices[0].z = 0.0f;

    vertices[1].u = (f32) width;
    vertices[1].v = 0.0f;
    vertices[1].color = sRenderer.rdp.primColor;
    vertices[1].x = PSP_COORD_X(lrx);
    vertices[1].y = PSP_COORD_Y(uly);
    vertices[1].z = 0.0f;

    vertices[2].u = 0.0f;
    vertices[2].v = (f32) height;
    vertices[2].color = sRenderer.rdp.primColor;
    vertices[2].x = PSP_COORD_X(ulx);
    vertices[2].y = PSP_COORD_Y(lry);
    vertices[2].z = 0.0f;

    vertices[3].u = (f32) width;
    vertices[3].v = (f32) height;
    vertices[3].color = sRenderer.rdp.primColor;
    vertices[3].x = PSP_COORD_X(lrx);
    vertices[3].y = PSP_COORD_Y(lry);
    vertices[3].z = 0.0f;

    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexImage(0, texWidth, texHeight, texWidth, entry->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, 0,
                   vertices);
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
    u32 width;
    u32 height;

    if (flipped) {
        sRenderer.census.texRectFlipCount++;
    } else {
        sRenderer.census.texRectCount++;
    }

    width = sRenderer.rdp.tileWidth;
    height = sRenderer.rdp.tileHeight;
    if ((width == 0) || (height == 0)) {
        width = (u32) ((lrx >= ulx) ? (lrx - ulx) : (ulx - lrx));
        height = (u32) ((lry >= uly) ? (lry - uly) : (uly - lry));
    }

#if PSP_RENDERER_TEXRECT_MODE == PSP_RENDERER_TEXRECT_SKIP
    (void) width;
    (void) height;
    sRenderer.census.texRectDrawSkipped++;
    return;
#endif

#if PSP_RENDERER_TEXRECT_MODE == PSP_RENDERER_TEXRECT_SOLID
    psp_renderer_log_texrect(ulx, uly, lrx, lry, width, height);
    psp_renderer_draw_texrect_solid(ulx, uly, lrx, lry, sRenderer.rdp.primColor);
    sRenderer.census.placeholderRectCount++;
    return;
#endif

    psp_renderer_log_texrect(ulx, uly, lrx, lry, width, height);
    if ((sRenderer.rdp.textureFmt == G_IM_FMT_RGBA) || (sRenderer.rdp.textureFmt == G_IM_FMT_IA)) {
        psp_renderer_draw_textured_rect(ulx, uly, lrx, lry, width, height);
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
            case G_SETCOMBINE:
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
                if (targetEnd == NULL) {
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
                sRenderer.census.setTileCount++;
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

    psp_renderer_begin_frame();
    psp_renderer_execute_dl(dl);
    psp_renderer_end_frame();

    sRenderer.census.frameCount++;
    psp_renderer_log_census(taskIndex);
}
