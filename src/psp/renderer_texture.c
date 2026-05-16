#include "src/psp/renderer_texture.h"

#include "PR/gbi.h"

#include <psputils.h>

#define PSP_RENDERER_TEX_CACHE_SLOTS 16
#define PSP_RENDERER_TEX_MAX_WIDTH 256
#define PSP_RENDERER_TEX_MAX_HEIGHT 128
#define PSP_RENDERER_TEX_MAX_PIXELS (PSP_RENDERER_TEX_MAX_WIDTH * PSP_RENDERER_TEX_MAX_HEIGHT)

typedef enum {
    PSP_TEX_SOURCE_RAW_BYTES,
    PSP_TEX_SOURCE_U64_WORDSWAPPED,
} PspTextureSourceLayout;

typedef struct {
    const void* source;
    u32 fmt;
    u32 siz;
    u32 width;
    u32 height;
    u32 sourceStride;
    u32 sourceS;
    u32 sourceT;
    u32 paletteHash;
    u32 paletteCount;
    u32 valid;
    u32 age;
    u32 pixels[PSP_RENDERER_TEX_MAX_PIXELS] __attribute__((aligned(64)));
} PspTextureCacheEntry;

#define PSP_TEXTURE_SOURCE_LAYOUT PSP_TEX_SOURCE_U64_WORDSWAPPED

static PspTextureCacheEntry sTextureCache[PSP_RENDERER_TEX_CACHE_SLOTS];
static u32 sTextureAge;

static u32 psp_texture_hash_palette(const u32* palette, u32 paletteCount) {
    u32 i;
    u32 hash = 2166136261u;

    if (palette == NULL) {
        return 0;
    }

    for (i = 0; i < paletteCount; i++) {
        hash ^= palette[i];
        hash *= 16777619u;
    }

    return hash;
}

static u32 psp_texture_rgba32(u8 r, u8 g, u8 b, u8 a) {
    return ((u32) a << 24) | ((u32) b << 16) | ((u32) g << 8) | (u32) r;
}

static u32 psp_texture_next_pow2(u32 value) {
    u32 result = 1;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

static u8 psp_texture_read_u8(const u8* src, u32 offset, PspTextureSourceLayout layout) {
    if (layout == PSP_TEX_SOURCE_U64_WORDSWAPPED) {
        offset ^= 7U;
    }

    return src[offset];
}

static u16 psp_texture_read_be16(const u8* src, u32 offset, PspTextureSourceLayout layout) {
    return ((u16) psp_texture_read_u8(src, offset, layout) << 8) |
           (u16) psp_texture_read_u8(src, offset + 1, layout);
}

static u16 psp_texture_read_rgba16(const u8* src, u32 sourceTexel) {
    u32 sourceByte = sourceTexel * 2U;

    /*
     * SF64 title assets currently decode correctly as raw little-endian RGBA16.
     * Keep the alternate reader nearby as the documented N64 byte-swapped path
     * for future asset classes.
     */
    (void) psp_texture_read_be16;
    return ((u16) src[sourceByte + 1] << 8) | (u16) src[sourceByte + 0];
}

static PspTextureCacheEntry* psp_texture_find_slot(const void* source, u32 fmt, u32 siz, u32 width, u32 height,
                                                   u32 sourceStride, u32 sourceS, u32 sourceT, u32 paletteHash,
                                                   u32 paletteCount, int* cacheHit) {
    PspTextureCacheEntry* oldest = &sTextureCache[0];
    u32 i;

    *cacheHit = 0;

    for (i = 0; i < PSP_RENDERER_TEX_CACHE_SLOTS; i++) {
        if ((sTextureCache[i].source == source) &&
            (sTextureCache[i].fmt == fmt) &&
            (sTextureCache[i].siz == siz) &&
            (sTextureCache[i].width == width) &&
            (sTextureCache[i].height == height) &&
            (sTextureCache[i].sourceStride == sourceStride) &&
            (sTextureCache[i].sourceS == sourceS) &&
            (sTextureCache[i].sourceT == sourceT) &&
            (sTextureCache[i].paletteHash == paletteHash) &&
            (sTextureCache[i].paletteCount == paletteCount)) {
            sTextureCache[i].age = ++sTextureAge;
            *cacheHit = sTextureCache[i].valid != 0;
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
    oldest->sourceStride = sourceStride;
    oldest->sourceS = sourceS;
    oldest->sourceT = sourceT;
    oldest->paletteHash = paletteHash;
    oldest->paletteCount = paletteCount;
    oldest->valid = 0;
    oldest->age = ++sTextureAge;
    return oldest;
}

static int psp_texture_convert(PspTextureCacheEntry* entry, u32 width, u32 height, u32 sourceStride, u32 sourceS,
                               u32 sourceT, const u32* palette, u32 paletteCount) {
    u32 x;
    u32 y;
    u32 texWidth = psp_texture_next_pow2(width);
    u32 texHeight = psp_texture_next_pow2(height);

    if (sourceStride == 0) {
        sourceStride = width;
    }
    if (sourceStride < width) {
        sourceStride = width;
    }

    if ((entry == NULL) || (entry->source == NULL) || (texWidth > PSP_RENDERER_TEX_MAX_WIDTH) ||
        (texHeight > PSP_RENDERER_TEX_MAX_HEIGHT)) {
        return 0;
    }

    for (y = 0; y < texHeight; y++) {
        for (x = 0; x < texWidth; x++) {
            entry->pixels[(y * texWidth) + x] = 0;
        }
    }

    if ((entry->fmt == G_IM_FMT_RGBA) && (entry->siz == G_IM_SIZ_16b)) {
        const u8* src = (const u8*) entry->source;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                u32 sourceTexel = ((y + sourceT) * sourceStride) + (x + sourceS);
                u16 packed = psp_texture_read_rgba16(src, sourceTexel);
                u8 r = (u8) (((packed >> 11) & 0x1F) * 255 / 31);
                u8 g = (u8) (((packed >> 6) & 0x1F) * 255 / 31);
                u8 b = (u8) (((packed >> 1) & 0x1F) * 255 / 31);
                u8 a = (packed & 1) ? 255 : 0;

                entry->pixels[(y * texWidth) + x] = psp_texture_rgba32(r, g, b, a);
            }
        }
    } else if ((entry->fmt == G_IM_FMT_IA) && ((entry->siz == G_IM_SIZ_8b) || (entry->siz == G_IM_SIZ_16b))) {
        const u8* src = (const u8*) entry->source;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                u32 sourceTexel = ((y + sourceT) * sourceStride) + (x + sourceS);
                u8 packed = src[sourceTexel];
                u8 intensity = (u8) (((packed >> 4) & 0xF) * 17);
                u8 alpha = (u8) ((packed & 0xF) * 17);

                entry->pixels[(y * texWidth) + x] = psp_texture_rgba32(intensity, intensity, intensity, alpha);
            }
        }
    } else if ((entry->fmt == G_IM_FMT_CI) && (entry->siz == G_IM_SIZ_8b)) {
        const u8* src = (const u8*) entry->source;

        if ((palette == NULL) || (paletteCount == 0)) {
            return 0;
        }

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                u32 sourceTexel = ((y + sourceT) * sourceStride) + (x + sourceS);
                u8 index = src[sourceTexel];

                if (index < paletteCount) {
                    entry->pixels[(y * texWidth) + x] = palette[index];
                } else {
                    entry->pixels[(y * texWidth) + x] = 0;
                }
            }
        }
    } else {
        return 0;
    }

    sceKernelDcacheWritebackRange(entry->pixels, texWidth * texHeight * sizeof(u32));
    entry->valid = 1;
    return 1;
}

void PspRendererTexture_Reset(void) {
    sTextureAge = 0;
}

int PspRendererTexture_Get(const void* source, u32 fmt, u32 siz, u32 width, u32 height, u32 sourceStride, u32 sourceS,
                           u32 sourceT, const u32* palette, u32 paletteCount, PspRendererTexture* out) {
    PspTextureCacheEntry* entry;
    int cacheHit;
    u32 paletteHash;

    if (out == NULL) {
        return 0;
    }

    out->pixels = NULL;
    out->width = width;
    out->height = height;
    out->textureWidth = psp_texture_next_pow2(width);
    out->textureHeight = psp_texture_next_pow2(height);
    out->cacheHit = 0;

    paletteHash = psp_texture_hash_palette(palette, paletteCount);

    entry = psp_texture_find_slot(source, fmt, siz, width, height, sourceStride, sourceS, sourceT, paletteHash,
                                  paletteCount, &cacheHit);

    if (!cacheHit && !psp_texture_convert(entry, width, height, sourceStride, sourceS, sourceT, palette, paletteCount)) {
        return 0;
    }

    out->pixels = entry->pixels;
    out->cacheHit = cacheHit;
    return 1;
}