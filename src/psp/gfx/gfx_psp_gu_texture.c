#include "src/psp/gfx/gfx_psp_gu_texture.h"

#include "src/psp/gfx/gfx_psp_color.h"
#include "src/psp/hw_counter_profile.h"
#include "src/psp/profiler.h"

#include <malloc.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PSP_GFX_GU_TEXTURE_CACHE_SLOTS 128
#define PSP_GFX_GU_TEXTURE_CACHE_BYTES (2U * 1024U * 1024U)
#define PSP_GFX_GU_TEXTURE_MIN_DIMENSION 8
#define PSP_GFX_GU_TEXTURE_MAX_DIMENSION 512
#define PSP_GFX_GU_TEXTURE_BYTES_PER_PIXEL 4
#define PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT 128
#define PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES 2

typedef char PspGfxGuTextureLookupIndexCheck[
    (PSP_GFX_GU_TEXTURE_CACHE_SLOTS < 256) ? 1 : -1
];
typedef char PspGfxGuTextureLookupSetCheck[
    ((PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT & (PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT - 1)) == 0) ? 1 : -1
];

typedef struct {
    int valid;
    int retired;
    const void* pixels;
    const u16* palette;
    PspGfxTextureFormat format;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    u8 premultiply;
    u8 softCoverage;
    u8 envBlend;
    u8 mirrorS;
    u8 mirrorT;
    u32 primitiveColor;
    u32 environmentColor;
    void* data;
    u32 dataBytes;
    u32 generation;
    u32 lastFrame;
    u32 age;
} PspGfxGuTextureEntry;

static PspGfxGuTextureEntry sPspGfxGuTextureEntries[PSP_GFX_GU_TEXTURE_CACHE_SLOTS];
static u32 sPspGfxGuTextureFrame;
static u32 sPspGfxGuTextureAge;
static u32 sPspGfxGuTextureGeneration;
static u32 sPspGfxGuTextureUsedBytes;
static u32 sPspGfxGuTextureValidEntries;
static int sPspGfxGuTextureInitialized;
static u8 sPspGfxGuTextureLookup[PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT][PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES];
static u8 sPspGfxGuTextureRgba16Lookup[PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT]
                                      [PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES];

static u32 psp_gfx_gu_texture_next_power_of_two(u32 value) {
    u32 result = PSP_GFX_GU_TEXTURE_MIN_DIMENSION;

    if (value > PSP_GFX_GU_TEXTURE_MAX_DIMENSION) {
        return 0;
    }
    while (result < value) {
        result <<= 1;
    }
    return result;
}

static int psp_gfx_gu_texture_dimensions(const PspGfxTextureRequest* request, u32* uploadWidth,
                                         u32* uploadHeight, u32* dataBytes) {
    u32 width;
    u32 height;
    u32 pixels;

    if ((request == NULL) || (request->width == 0) || (request->height == 0) || (uploadWidth == NULL) ||
        (uploadHeight == NULL) || (dataBytes == NULL)) {
        return 0;
    }
    width = psp_gfx_gu_texture_next_power_of_two(request->width);
    height = psp_gfx_gu_texture_next_power_of_two(request->height);
    if ((width == 0) || (height == 0)) {
        return 0;
    }
    if (request->mirrorS) {
        if (width > (PSP_GFX_GU_TEXTURE_MAX_DIMENSION / 2U)) {
            return 0;
        }
        width <<= 1;
    }
    if (request->mirrorT) {
        if (height > (PSP_GFX_GU_TEXTURE_MAX_DIMENSION / 2U)) {
            return 0;
        }
        height <<= 1;
    }
    if ((width > PSP_GFX_GU_TEXTURE_MAX_DIMENSION) || (height > PSP_GFX_GU_TEXTURE_MAX_DIMENSION) ||
        (height > (0xFFFFFFFFU / width))) {
        return 0;
    }
    pixels = width * height;
    if (pixels > (PSP_GFX_GU_TEXTURE_CACHE_BYTES / PSP_GFX_GU_TEXTURE_BYTES_PER_PIXEL)) {
        return 0;
    }
    *uploadWidth = width;
    *uploadHeight = height;
    *dataBytes = pixels * PSP_GFX_GU_TEXTURE_BYTES_PER_PIXEL;
    return 1;
}

static int psp_gfx_gu_texture_request_supported(const PspGfxTextureRequest* request) {
    u32 uploadWidth;
    u32 uploadHeight;
    u32 dataBytes;

    if ((request == NULL) || (request->pixels == NULL)) {
        return 0;
    }
    if ((request->format != PSP_GFX_TEXTURE_CI4) && (request->format != PSP_GFX_TEXTURE_CI8) &&
        (request->format != PSP_GFX_TEXTURE_RGBA16) && (request->format != PSP_GFX_TEXTURE_RGBA32) &&
        (request->format != PSP_GFX_TEXTURE_IA8) && (request->format != PSP_GFX_TEXTURE_IA16)) {
        return 0;
    }
    if (((request->format == PSP_GFX_TEXTURE_CI4) || (request->format == PSP_GFX_TEXTURE_CI8)) &&
        (request->palette == NULL)) {
        return 0;
    }
    return psp_gfx_gu_texture_dimensions(request, &uploadWidth, &uploadHeight, &dataBytes);
}

static u32 psp_gfx_gu_texture_lookup_hash_word(u32 hash, u32 value) {
    hash ^= value;
    hash *= 16777619U;
    return hash;
}

static u32 psp_gfx_gu_texture_lookup_set(const PspGfxTextureRequest* request) {
    u32 hash = 2166136261U;
    u32 pixels = (u32) (uintptr_t) request->pixels;
    u32 palette = (u32) (uintptr_t) request->palette;

    hash = psp_gfx_gu_texture_lookup_hash_word(hash, pixels >> 3);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, palette >> 3);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, (u32) request->format);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->width);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->height);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->premultiply);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->softCoverage);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->envBlend);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->mirrorS);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->mirrorT);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->primitiveColor);
    hash = psp_gfx_gu_texture_lookup_hash_word(hash, request->environmentColor);
    return hash & (PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT - 1);
}

static u32 psp_gfx_gu_texture_rgba16_lookup_set(const PspGfxTextureRequest* request) {
    u32 hash = (u32) (uintptr_t) request->pixels >> 3;

    hash ^= request->width << 3;
    hash ^= request->height << 11;
    hash ^= (u32) request->premultiply << 19;
    hash ^= hash >> 7;
    return hash & (PSP_GFX_GU_TEXTURE_LOOKUP_SET_COUNT - 1);
}

static void psp_gfx_gu_texture_remember_lookup(
    u8 lookup[][PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES], u32 set, u32 index) {
    u8 encodedIndex = (u8) (index + 1);

    if (lookup[set][0] != encodedIndex) {
        lookup[set][1] = lookup[set][0];
        lookup[set][0] = encodedIndex;
    }
}

static PspHwTextureCacheClass psp_gfx_gu_texture_hw_class(PspGfxTextureFormat format) {
    if (format == PSP_GFX_TEXTURE_CI8) {
        return PSP_HW_TEXTURE_CACHE_CI8;
    }
    if (format == PSP_GFX_TEXTURE_RGBA16) {
        return PSP_HW_TEXTURE_CACHE_RGBA16;
    }
    if (format == PSP_GFX_TEXTURE_RGBA32) {
        return PSP_HW_TEXTURE_CACHE_RGBA32;
    }
    return PSP_HW_TEXTURE_CACHE_CONVERTED;
}

#if PROFILE_PHASES
static PspProfileTextureCacheClass psp_gfx_gu_texture_profile_class(PspGfxTextureFormat format) {
    if (format == PSP_GFX_TEXTURE_CI8) {
        return PSP_PROFILE_TEXTURE_CACHE_CI8;
    }
    if (format == PSP_GFX_TEXTURE_RGBA16) {
        return PSP_PROFILE_TEXTURE_CACHE_RGBA16;
    }
    if (format == PSP_GFX_TEXTURE_RGBA32) {
        return PSP_PROFILE_TEXTURE_CACHE_RGBA32;
    }
    return PSP_PROFILE_TEXTURE_CACHE_CONVERTED;
}

static u64 psp_gfx_gu_texture_hash_word(u64 hash, u32 value) {
    u32 i;

    for (i = 0; i < 4; i++) {
        hash ^= (u8) (value >> (i * 8));
        hash *= 1099511628211ULL;
    }
    return hash;
}

static u64 psp_gfx_gu_texture_base_hash(const PspGfxTextureRequest* request) {
    u64 hash = 1469598103934665603ULL;
    u64 pixels = (u64) (uintptr_t) request->pixels;

    hash = psp_gfx_gu_texture_hash_word(hash, (u32) pixels);
    hash = psp_gfx_gu_texture_hash_word(hash, (u32) (pixels >> 32));
    hash = psp_gfx_gu_texture_hash_word(hash, (u32) request->format);
    return hash;
}

static u64 psp_gfx_gu_texture_key_hash(const PspGfxTextureRequest* request) {
    u64 hash = psp_gfx_gu_texture_base_hash(request);
    u64 palette = (u64) (uintptr_t) request->palette;

    hash = psp_gfx_gu_texture_hash_word(hash, (u32) palette);
    hash = psp_gfx_gu_texture_hash_word(hash, (u32) (palette >> 32));
    hash = psp_gfx_gu_texture_hash_word(hash, request->width);
    hash = psp_gfx_gu_texture_hash_word(hash, request->height);
    hash = psp_gfx_gu_texture_hash_word(hash, request->premultiply);
    hash = psp_gfx_gu_texture_hash_word(hash, request->softCoverage);
    hash = psp_gfx_gu_texture_hash_word(hash, request->envBlend);
    hash = psp_gfx_gu_texture_hash_word(hash, request->mirrorS);
    hash = psp_gfx_gu_texture_hash_word(hash, request->mirrorT);
    hash = psp_gfx_gu_texture_hash_word(hash, request->primitiveColor);
    hash = psp_gfx_gu_texture_hash_word(hash, request->environmentColor);
    return hash;
}

static u64 psp_gfx_gu_texture_entry_key_hash(const PspGfxGuTextureEntry* entry) {
    PspGfxTextureRequest request = { 0 };

    request.pixels = entry->pixels;
    request.palette = entry->palette;
    request.format = entry->format;
    request.width = entry->width;
    request.height = entry->height;
    request.premultiply = entry->premultiply;
    request.softCoverage = entry->softCoverage;
    request.envBlend = entry->envBlend;
    request.mirrorS = entry->mirrorS;
    request.mirrorT = entry->mirrorT;
    request.primitiveColor = entry->primitiveColor;
    request.environmentColor = entry->environmentColor;
    return psp_gfx_gu_texture_key_hash(&request);
}
#endif

static int psp_gfx_gu_texture_entry_matches(const PspGfxGuTextureEntry* entry,
                                            const PspGfxTextureRequest* request) {
    return entry->valid && (entry->pixels == request->pixels) && (entry->palette == request->palette) &&
           (entry->format == request->format) && (entry->width == request->width) &&
           (entry->height == request->height) && (entry->premultiply == request->premultiply) &&
           (entry->softCoverage == request->softCoverage) && (entry->envBlend == request->envBlend) &&
           (entry->mirrorS == request->mirrorS) && (entry->mirrorT == request->mirrorT) &&
           (entry->primitiveColor == request->primitiveColor) &&
           (entry->environmentColor == request->environmentColor);
}

static void psp_gfx_gu_texture_release_entry(PspGfxGuTextureEntry* entry) {
    if (entry->data != NULL) {
        free(entry->data);
        sPspGfxGuTextureUsedBytes -= entry->dataBytes;
    }
    if (entry->valid) {
        sPspGfxGuTextureValidEntries--;
    }
    memset(entry, 0, sizeof(*entry));
}

static void psp_gfx_gu_texture_evict_entry(PspGfxGuTextureEntry* entry) {
    if (entry->data != NULL) {
        PspHwTextureCacheClass hwClass = psp_gfx_gu_texture_hw_class(entry->format);

        PspHwCounterProfile_CountTextureCacheEviction(hwClass);
        (void) hwClass;
#if PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(psp_gfx_gu_texture_profile_class(entry->format),
                                               psp_gfx_gu_texture_entry_key_hash(entry));
#endif
    }
    psp_gfx_gu_texture_release_entry(entry);
}

static int psp_gfx_gu_texture_find_free_entry(void) {
    u32 i;

    for (i = 0; i < PSP_GFX_GU_TEXTURE_CACHE_SLOTS; i++) {
        if (!sPspGfxGuTextureEntries[i].valid && !sPspGfxGuTextureEntries[i].retired &&
            (sPspGfxGuTextureEntries[i].data == NULL)) {
            return (int) i;
        }
    }
    return -1;
}

static int psp_gfx_gu_texture_find_victim(void) {
    u32 i;
    int victim = -1;
    u32 oldestAge = 0xFFFFFFFFU;

    for (i = 0; i < PSP_GFX_GU_TEXTURE_CACHE_SLOTS; i++) {
        PspGfxGuTextureEntry* entry = &sPspGfxGuTextureEntries[i];

        if ((entry->data == NULL) || (entry->lastFrame == sPspGfxGuTextureFrame)) {
            continue;
        }
        if ((victim < 0) || (entry->age < oldestAge)) {
            victim = (int) i;
            oldestAge = entry->age;
        }
    }
    return victim;
}

static int psp_gfx_gu_texture_reserve_entry(u32 dataBytes) {
    int index;
    int victim;

    while ((sPspGfxGuTextureUsedBytes > (PSP_GFX_GU_TEXTURE_CACHE_BYTES - dataBytes))) {
        victim = psp_gfx_gu_texture_find_victim();
        if (victim < 0) {
            return -1;
        }
        psp_gfx_gu_texture_evict_entry(&sPspGfxGuTextureEntries[victim]);
    }

    index = psp_gfx_gu_texture_find_free_entry();
    if (index >= 0) {
        return index;
    }
    victim = psp_gfx_gu_texture_find_victim();
    if (victim < 0) {
        return -1;
    }
    psp_gfx_gu_texture_evict_entry(&sPspGfxGuTextureEntries[victim]);
    return victim;
}

static u16 psp_gfx_gu_texture_read_u16(const void* pixels, u32 index) {
    const u8* bytes = (const u8*) pixels + (index * 2);

    return (u16) (bytes[0] | ((u16) bytes[1] << 8));
}

static u32 psp_gfx_gu_texture_read_rgba32(const void* pixels, u32 index) {
    const u8* bytes = (const u8*) pixels + (index * 4);

    return ((u32) bytes[3] << 24) | ((u32) bytes[2] << 16) | ((u32) bytes[1] << 8) | bytes[0];
}

static void psp_gfx_gu_texture_decode_rgba16(u16 color, u8* output) {
    output[0] = psp_gfx_color_transfer_u8((u8) ((((color >> 11) & 31U) * 255U) / 31U));
    output[1] = psp_gfx_color_transfer_u8((u8) ((((color >> 6) & 31U) * 255U) / 31U));
    output[2] = psp_gfx_color_transfer_u8((u8) ((((color >> 1) & 31U) * 255U) / 31U));
    output[3] = (color & 1U) ? 255 : 0;
}

static void psp_gfx_gu_texture_decode_rgba32(u32 color, u8* output) {
    output[0] = psp_gfx_color_transfer_u8((u8) (color >> 24));
    output[1] = psp_gfx_color_transfer_u8((u8) (color >> 16));
    output[2] = psp_gfx_color_transfer_u8((u8) (color >> 8));
    output[3] = (u8) color;
}

static int psp_gfx_gu_texture_is_dark_rgba16_mask(const u16* pixels, u32 width, u32 height) {
    u32 pixelCount = width * height;
    u32 opaqueCount = 0;
    u32 i;

    for (i = 0; i < pixelCount; i++) {
        u16 color = psp_gfx_gu_texture_read_u16(pixels, i);

        if ((color & 1U) == 0) {
            continue;
        }
        opaqueCount++;
        if (((color >> 11) & 31U) > 2U || ((color >> 6) & 31U) > 2U || ((color >> 1) & 31U) > 2U) {
            return 0;
        }
    }
    return (opaqueCount != 0) && (opaqueCount != pixelCount);
}

static u8 psp_gfx_gu_texture_filtered_rgba16_alpha(const u16* pixels, u32 width, u32 height, u32 x, u32 y) {
    static const u32 weights[3] = { 1, 2, 1 };
    u32 alpha = 0;
    s32 ky;
    s32 kx;

    for (ky = -1; ky <= 1; ky++) {
        s32 sampleY = (s32) y + ky;

        if (sampleY < 0) {
            sampleY = 0;
        } else if (sampleY >= (s32) height) {
            sampleY = (s32) height - 1;
        }
        for (kx = -1; kx <= 1; kx++) {
            s32 sampleX = (s32) x + kx;
            u32 weight = weights[kx + 1] * weights[ky + 1];

            if (sampleX < 0) {
                sampleX = 0;
            } else if (sampleX >= (s32) width) {
                sampleX = (s32) width - 1;
            }
            if ((psp_gfx_gu_texture_read_u16(pixels, ((u32) sampleY * width) + (u32) sampleX) & 1U) != 0) {
                alpha += 255U * weight;
            }
        }
    }
    return (u8) ((alpha + 8U) / 16U);
}

static u8 psp_gfx_gu_texture_soft_coverage_alpha(u8 alpha) {
    if (alpha <= 32U) {
        return 0;
    }
    return (u8) ((((u32) (alpha - 32U) * 255U) + 111U) / 223U);
}

static u32 psp_gfx_gu_texture_mirror_source_coord(u32 coord, u32 logicalSize, int mirror) {
    u32 period = psp_gfx_gu_texture_next_power_of_two(logicalSize);

    if (mirror && (coord >= period)) {
        coord = (period * 2U) - 1U - coord;
    }
    return (coord < logicalSize) ? coord : (logicalSize - 1U);
}

static void psp_gfx_gu_texture_premultiply(u8* output) {
    output[0] = (u8) (((u32) output[0] * output[3] + 127U) / 255U);
    output[1] = (u8) (((u32) output[1] * output[3] + 127U) / 255U);
    output[2] = (u8) (((u32) output[2] * output[3] + 127U) / 255U);
}

static void psp_gfx_gu_texture_apply_env_blend(const PspGfxTextureRequest* request, u8 intensity, u8* output) {
    u8 primitiveR = (u8) (request->primitiveColor & 0xFFU);
    u8 primitiveG = (u8) ((request->primitiveColor >> 8) & 0xFFU);
    u8 primitiveB = (u8) ((request->primitiveColor >> 16) & 0xFFU);
    u8 environmentR = (u8) (request->environmentColor & 0xFFU);
    u8 environmentG = (u8) ((request->environmentColor >> 8) & 0xFFU);
    u8 environmentB = (u8) ((request->environmentColor >> 16) & 0xFFU);

    output[0] = (u8) (((u32) environmentR * (255U - intensity) + ((u32) primitiveR * intensity) + 127U) / 255U);
    output[1] = (u8) (((u32) environmentG * (255U - intensity) + ((u32) primitiveG * intensity) + 127U) / 255U);
    output[2] = (u8) (((u32) environmentB * (255U - intensity) + ((u32) primitiveB * intensity) + 127U) / 255U);
}

static u32 psp_gfx_gu_texture_swizzle_offset(u32 offset, u32 log2Width) {
    u32 widthMask;
    u32 blockX;
    u32 blockY;

    if (log2Width <= 4) {
        return offset;
    }
    widthMask = (1U << log2Width) - 1U;
    blockX = offset & widthMask & ~0xFU;
    blockY = offset & (7U << log2Width);
    return (offset & ((~7U << log2Width) | 0xFU)) | (blockX << 3) |
           (blockY >> (log2Width - 4));
}

static void psp_gfx_gu_texture_decode(const PspGfxTextureRequest* request, u32 uploadWidth, u32 uploadHeight,
                                      u8* output) {
    u32 x;
    u32 y;
    u32 uploadByteWidth = uploadWidth * PSP_GFX_GU_TEXTURE_BYTES_PER_PIXEL;
    u32 log2UploadByteWidth = 0;
    int softenAlpha = (request->format == PSP_GFX_TEXTURE_RGBA16) && request->premultiply &&
                      psp_gfx_gu_texture_is_dark_rgba16_mask((const u16*) request->pixels, request->width,
                                                             request->height);

    while ((1U << log2UploadByteWidth) < uploadByteWidth) {
        log2UploadByteWidth++;
    }

    for (y = 0; y < uploadHeight; y++) {
        u32 sourceY = psp_gfx_gu_texture_mirror_source_coord(y, request->height, request->mirrorT);

        for (x = 0; x < uploadWidth; x++) {
            u32 sourceX = psp_gfx_gu_texture_mirror_source_coord(x, request->width, request->mirrorS);
            u32 sourceIndex = sourceY * request->width + sourceX;
            u32 outputIndex = y * uploadWidth + x;
            u32 outputOffset = psp_gfx_gu_texture_swizzle_offset(outputIndex * 4, log2UploadByteWidth);
            u8* outputPixel = &output[outputOffset];

            if (request->format == PSP_GFX_TEXTURE_CI4) {
                u8 packed = ((const u8*) request->pixels)[sourceIndex >> 1];
                u8 paletteIndex = (sourceIndex & 1U) ? (packed & 0xFU) : (packed >> 4);

                psp_gfx_gu_texture_decode_rgba16(psp_gfx_gu_texture_read_u16(request->palette, paletteIndex),
                                                 outputPixel);
            } else if (request->format == PSP_GFX_TEXTURE_CI8) {
                u8 paletteIndex = ((const u8*) request->pixels)[sourceIndex];

                psp_gfx_gu_texture_decode_rgba16(psp_gfx_gu_texture_read_u16(request->palette, paletteIndex),
                                                 outputPixel);
            } else if (request->format == PSP_GFX_TEXTURE_RGBA16) {
                psp_gfx_gu_texture_decode_rgba16(psp_gfx_gu_texture_read_u16(request->pixels, sourceIndex),
                                                 outputPixel);
                if (softenAlpha) {
                    outputPixel[0] = 0;
                    outputPixel[1] = 0;
                    outputPixel[2] = 0;
                    outputPixel[3] = psp_gfx_gu_texture_filtered_rgba16_alpha(
                        (const u16*) request->pixels, request->width, request->height, sourceX, sourceY);
                }
                if (request->premultiply) {
                    psp_gfx_gu_texture_premultiply(outputPixel);
                }
            } else if (request->format == PSP_GFX_TEXTURE_RGBA32) {
                psp_gfx_gu_texture_decode_rgba32(psp_gfx_gu_texture_read_rgba32(request->pixels, sourceIndex),
                                                 outputPixel);
                if (request->envBlend) {
                    psp_gfx_gu_texture_apply_env_blend(request, outputPixel[0], outputPixel);
                } else if (request->premultiply) {
                    psp_gfx_gu_texture_premultiply(outputPixel);
                }
            } else if (request->format == PSP_GFX_TEXTURE_IA8) {
                u8 packed = ((const u8*) request->pixels)[sourceIndex];
                u8 intensity = psp_gfx_color_transfer_u8((u8) ((packed >> 4) * 17U));

                outputPixel[3] = (packed & 0xFU) * 17U;
                if (request->softCoverage) {
                    outputPixel[3] = psp_gfx_gu_texture_soft_coverage_alpha(outputPixel[3]);
                }
                outputPixel[0] = intensity;
                outputPixel[1] = intensity;
                outputPixel[2] = intensity;
                if (request->envBlend) {
                    psp_gfx_gu_texture_apply_env_blend(request, intensity, outputPixel);
                }
            } else {
                u16 packed = psp_gfx_gu_texture_read_u16(request->pixels, sourceIndex);
                u8 intensity = psp_gfx_color_transfer_u8((u8) (packed >> 8));

                outputPixel[3] = (u8) packed;
                if (request->softCoverage) {
                    outputPixel[3] = psp_gfx_gu_texture_soft_coverage_alpha(outputPixel[3]);
                }
                outputPixel[0] = intensity;
                outputPixel[1] = intensity;
                outputPixel[2] = intensity;
            }
        }
    }
}

static void psp_gfx_gu_texture_fill_result(const PspGfxGuTextureEntry* entry, PspGfxTextureResult* result,
                                           PspGfxTextureCacheResult cacheResult, u32 index) {
    result->handle.opaque[0] = index + 1;
    result->handle.opaque[1] = entry->generation;
    result->handle.opaque[2] = (u32) entry->format + 1;
    result->handle.opaque[3] = entry->generation;
    result->uploadWidth = entry->uploadWidth;
    result->uploadHeight = entry->uploadHeight;
    result->uploadX = 0;
    result->uploadY = 0;
    result->cacheResult = cacheResult;
}

static u32 psp_gfx_gu_texture_next_generation(void) {
    sPspGfxGuTextureGeneration++;
    if (sPspGfxGuTextureGeneration == 0) {
        sPspGfxGuTextureGeneration = 1;
    }
    return sPspGfxGuTextureGeneration;
}

static void psp_gfx_gu_texture_touch(PspGfxGuTextureEntry* entry) {
    sPspGfxGuTextureAge++;
    if (sPspGfxGuTextureAge == 0) {
        sPspGfxGuTextureAge = 1;
    }
    entry->lastFrame = sPspGfxGuTextureFrame;
    entry->age = sPspGfxGuTextureAge;
}

int PspGfxGuTexture_Init(void) {
    if (sPspGfxGuTextureInitialized) {
        return 1;
    }
    memset(sPspGfxGuTextureEntries, 0, sizeof(sPspGfxGuTextureEntries));
    memset(sPspGfxGuTextureLookup, 0, sizeof(sPspGfxGuTextureLookup));
    memset(sPspGfxGuTextureRgba16Lookup, 0, sizeof(sPspGfxGuTextureRgba16Lookup));
    sPspGfxGuTextureFrame = 1;
    sPspGfxGuTextureAge = 0;
    sPspGfxGuTextureGeneration = 0;
    sPspGfxGuTextureUsedBytes = 0;
    sPspGfxGuTextureValidEntries = 0;
    PspGfxColor_Init();
    sPspGfxGuTextureInitialized = 1;
    return 1;
}

void PspGfxGuTexture_BeginFrame(void) {
    u32 i;

    if (!sPspGfxGuTextureInitialized) {
        return;
    }
    sPspGfxGuTextureFrame++;
    if (sPspGfxGuTextureFrame == 0) {
        sPspGfxGuTextureFrame = 1;
    }
    for (i = 0; i < PSP_GFX_GU_TEXTURE_CACHE_SLOTS; i++) {
        PspGfxGuTextureEntry* entry = &sPspGfxGuTextureEntries[i];

        if (entry->retired && (entry->lastFrame != sPspGfxGuTextureFrame)) {
            psp_gfx_gu_texture_release_entry(entry);
        }
    }
}

void PspGfxGuTexture_Shutdown(void) {
    u32 i;

    if (!sPspGfxGuTextureInitialized) {
        return;
    }
    for (i = 0; i < PSP_GFX_GU_TEXTURE_CACHE_SLOTS; i++) {
        psp_gfx_gu_texture_release_entry(&sPspGfxGuTextureEntries[i]);
    }
    sPspGfxGuTextureFrame = 0;
    sPspGfxGuTextureAge = 0;
    sPspGfxGuTextureGeneration = 0;
    sPspGfxGuTextureUsedBytes = 0;
    sPspGfxGuTextureValidEntries = 0;
    memset(sPspGfxGuTextureLookup, 0, sizeof(sPspGfxGuTextureLookup));
    memset(sPspGfxGuTextureRgba16Lookup, 0, sizeof(sPspGfxGuTextureRgba16Lookup));
    sPspGfxGuTextureInitialized = 0;
}

int PspGfxGuTexture_Supported(const PspGfxTextureRequest* request) {
    return psp_gfx_gu_texture_request_supported(request);
}

int PspGfxGuTexture_Find(const PspGfxTextureRequest* request, PspGfxTextureResult* result) {
#if PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
    PspProfileTextureCacheClass cacheClass;
#endif
    u32 lookupSet;
    u32 encodedIndex;
    u32 i;
    u8 (*lookup)[PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES];
    PspGfxGuTextureEntry* entry;

    if ((request == NULL) || (result == NULL)) {
        return 0;
    }
    *result = (PspGfxTextureResult) { 0 };
    result->cacheResult = PSP_GFX_TEXTURE_CACHE_MISS;
#if PROFILE_PHASES
    keyHash = psp_gfx_gu_texture_key_hash(request);
    baseHash = psp_gfx_gu_texture_base_hash(request);
    cacheClass = psp_gfx_gu_texture_profile_class(request->format);
#endif
    if (request->format == PSP_GFX_TEXTURE_RGBA16) {
        lookup = sPspGfxGuTextureRgba16Lookup;
        lookupSet = psp_gfx_gu_texture_rgba16_lookup_set(request);
    } else {
        lookup = sPspGfxGuTextureLookup;
        lookupSet = psp_gfx_gu_texture_lookup_set(request);
    }
    for (i = 0; i < PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES; i++) {
        encodedIndex = lookup[lookupSet][i];
        if (encodedIndex == 0) {
            continue;
        }
        entry = &sPspGfxGuTextureEntries[encodedIndex - 1];
        if (psp_gfx_gu_texture_entry_matches(entry, request)) {
            goto hit;
        }
    }
    if (!psp_gfx_gu_texture_request_supported(request)) {
        return 0;
    }
    for (i = 0; i < PSP_GFX_GU_TEXTURE_CACHE_SLOTS; i++) {
        entry = &sPspGfxGuTextureEntries[i];

        if (psp_gfx_gu_texture_entry_matches(entry, request)) {
            goto hit;
        }
    }
#if PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(cacheClass, PSP_GFX_GU_TEXTURE_CACHE_SLOTS,
                                         sPspGfxGuTextureValidEntries, keyHash, baseHash, 0);
#endif
    return 0;

hit:
    i = (u32) (entry - sPspGfxGuTextureEntries);
    psp_gfx_gu_texture_remember_lookup(lookup, lookupSet, i);
    psp_gfx_gu_texture_touch(entry);
    psp_gfx_gu_texture_fill_result(entry, result, PSP_GFX_TEXTURE_CACHE_HIT, i);
#if PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(cacheClass, PSP_GFX_GU_TEXTURE_CACHE_SLOTS,
                                         sPspGfxGuTextureValidEntries, keyHash, baseHash, 1);
#endif
    PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
    return 1;
}

int PspGfxGuTexture_Create(const PspGfxTextureRequest* request, PspGfxTextureResult* result) {
    PspGfxGuTextureEntry* entry;
#if PROFILE_PHASES
    PspProfileTextureCacheClass cacheClass;
#endif
    u32 uploadWidth;
    u32 uploadHeight;
    u32 dataBytes;
    u32 lookupSet;
#if PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    int index;
    u8 (*lookup)[PSP_GFX_GU_TEXTURE_LOOKUP_CANDIDATES];

    if (result == NULL) {
        return 0;
    }
    *result = (PspGfxTextureResult) { 0 };
    result->cacheResult = PSP_GFX_TEXTURE_CACHE_FAILED;
    if (!psp_gfx_gu_texture_request_supported(request) ||
        !psp_gfx_gu_texture_dimensions(request, &uploadWidth, &uploadHeight, &dataBytes)) {
        return 0;
    }

#if PROFILE_PHASES
    cacheClass = psp_gfx_gu_texture_profile_class(request->format);
    keyHash = psp_gfx_gu_texture_key_hash(request);
    baseHash = psp_gfx_gu_texture_base_hash(request);
#endif
    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    index = psp_gfx_gu_texture_reserve_entry(dataBytes);
    if (index < 0) {
        return 0;
    }
    entry = &sPspGfxGuTextureEntries[index];
    entry->data = memalign(16, dataBytes);
    if (entry->data == NULL) {
        return 0;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    psp_gfx_gu_texture_decode(request, uploadWidth, uploadHeight, (u8*) entry->data);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    sceKernelDcacheWritebackRange(entry->data, dataBytes);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);

    entry->pixels = request->pixels;
    entry->palette = request->palette;
    entry->format = request->format;
    entry->width = request->width;
    entry->height = request->height;
    entry->uploadWidth = uploadWidth;
    entry->uploadHeight = uploadHeight;
    entry->premultiply = request->premultiply;
    entry->softCoverage = request->softCoverage;
    entry->envBlend = request->envBlend;
    entry->mirrorS = request->mirrorS;
    entry->mirrorT = request->mirrorT;
    entry->primitiveColor = request->primitiveColor;
    entry->environmentColor = request->environmentColor;
    entry->dataBytes = dataBytes;
    entry->generation = psp_gfx_gu_texture_next_generation();
    entry->valid = 1;
    entry->retired = 0;
    sPspGfxGuTextureUsedBytes += dataBytes;
    sPspGfxGuTextureValidEntries++;
    psp_gfx_gu_texture_touch(entry);
    if (request->format == PSP_GFX_TEXTURE_RGBA16) {
        lookup = sPspGfxGuTextureRgba16Lookup;
        lookupSet = psp_gfx_gu_texture_rgba16_lookup_set(request);
    } else {
        lookup = sPspGfxGuTextureLookup;
        lookupSet = psp_gfx_gu_texture_lookup_set(request);
    }
    psp_gfx_gu_texture_remember_lookup(lookup, lookupSet, (u32) (entry - sPspGfxGuTextureEntries));
    psp_gfx_gu_texture_fill_result(entry, result, PSP_GFX_TEXTURE_CACHE_CREATED, (u32) index);
#if PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(cacheClass, PSP_GFX_GU_TEXTURE_CACHE_SLOTS,
                                            sPspGfxGuTextureValidEntries, keyHash, baseHash);
#endif
    PspProfiler_CountTextureEvent(0, 0, 1, 1, dataBytes);
#if PROFILE_PHASES
    if (request->mirrorS || request->mirrorT) {
        u32 sourceWidth = psp_gfx_gu_texture_next_power_of_two(request->width);
        u32 sourceHeight = psp_gfx_gu_texture_next_power_of_two(request->height);

        PspProfiler_CountMirrorEncodedTexture(request->mirrorS, request->mirrorT,
                                              sourceWidth * sourceHeight * PSP_GFX_GU_TEXTURE_BYTES_PER_PIXEL,
                                              dataBytes, 1, 1, 0, 0, uploadWidth, uploadHeight);
    }
#endif
    return 1;
}

void PspGfxGuTexture_InvalidateRgba16(const u16* pixels) {
    u32 i;

    if (pixels == NULL) {
        return;
    }
    for (i = 0; i < PSP_GFX_GU_TEXTURE_CACHE_SLOTS; i++) {
        PspGfxGuTextureEntry* entry = &sPspGfxGuTextureEntries[i];

        if (entry->valid && (entry->format == PSP_GFX_TEXTURE_RGBA16) && (entry->pixels == pixels)) {
            entry->valid = 0;
            entry->retired = 1;
            sPspGfxGuTextureValidEntries--;
        }
    }
}

int PspGfxGuTexture_Resolve(PspGfxTextureHandle handle, const void** pixels, u32* width, u32* height) {
    u32 index;
    PspGfxGuTextureEntry* entry;

    if (!PspGfxTextureHandle_IsValid(handle) || (pixels == NULL) || (width == NULL) || (height == NULL)) {
        return 0;
    }
    index = handle.opaque[0] - 1;
    if (index >= PSP_GFX_GU_TEXTURE_CACHE_SLOTS) {
        return 0;
    }
    entry = &sPspGfxGuTextureEntries[index];
    if (!entry->valid || (entry->generation == 0) || (handle.opaque[1] != entry->generation) ||
        (handle.opaque[2] != ((u32) entry->format + 1)) || (handle.opaque[3] != entry->generation)) {
        return 0;
    }
    psp_gfx_gu_texture_touch(entry);
    *pixels = entry->data;
    *width = entry->uploadWidth;
    *height = entry->uploadHeight;
    return 1;
}
