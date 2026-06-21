#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/profiler.h"

#include <GLES/gl.h>
#include <stddef.h>
#include <string.h>

#define PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE 64
#define PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE 96
#define PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS (256 * 32)
#define PSP_GFX_PSPGL_MIN_TEXTURE_DIMENSION 8
#define PSP_GFX_PSPGL_VERTEX_STREAM_SETS 2
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET 256
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_VERTICES 256
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET 32
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_VERTICES 3072
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_COUNT \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SETS * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET)
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SETS * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET)
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_VERTICES * sizeof(PspGfxPspglColorVertex))
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_VERTICES * sizeof(PspGfxPspglColorVertex))
#define PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES \
    ((PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES) + \
     (PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES))

#ifndef SF64_PSP_PSPGL_VBO_STREAM
#define SF64_PSP_PSPGL_VBO_STREAM 1
#endif

typedef struct {
    const u8* indices;
    const u16* palette;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    GLuint texture;
} PspGfxTextureCacheEntry;

typedef struct {
    const u16* pixels;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    int premultiplied;
    GLuint texture;
} PspGfxRgba16TextureCacheEntry;

typedef enum {
    PSP_GFX_TEXTURE_CI4,
    PSP_GFX_TEXTURE_IA8,
    PSP_GFX_TEXTURE_IA16,
} PspGfxConvertedTextureFormat;

typedef struct {
    const void* pixels;
    const u16* palette;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    PspGfxConvertedTextureFormat format;
    GLuint texture;
} PspGfxConvertedTextureCacheEntry;

#if SF64_PSP_PSPGL_VBO_STREAM
typedef struct {
    GLuint buffer;
} PspGfxVertexStreamPage;
#endif

static PspGfxTextureCacheEntry sTextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
static PspGfxRgba16TextureCacheEntry sRgba16TextureCache[PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE];
static PspGfxConvertedTextureCacheEntry sConvertedTextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
#if SF64_PSP_PSPGL_VBO_STREAM
static PspGfxVertexStreamPage sVertexStreamSmallPages[PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_COUNT];
static PspGfxVertexStreamPage sVertexStreamLargePages[PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT];
#endif
static u8 sTextureUpload[PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS * 4];
static u32 sTextureCacheCount;
static u32 sRgba16TextureCacheCount;
static u32 sTextureCacheReplaceIndex;
static u32 sRgba16TextureCacheReplaceIndex;
static u32 sConvertedTextureCacheCount;
static u32 sConvertedTextureCacheReplaceIndex;
static u32 sVertexStreamSetIndex;
static u32 sVertexStreamSmallPageIndex;
static u32 sVertexStreamLargePageIndex;
static int sVertexStreamInitialized;
static int sVertexStreamAvailable;

#if SF64_PSP_PROFILE_PHASES
static u64 psp_gfx_pspgl_hash_u64(u64 hash, u64 value) {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

static u64 psp_gfx_pspgl_hash_ptr(u64 hash, const void* ptr) {
    return psp_gfx_pspgl_hash_u64(hash, (u64) (unsigned long) ptr);
}

static u64 psp_gfx_pspgl_hash_start(u32 cacheClass) {
    return psp_gfx_pspgl_hash_u64(0xCBF29CE484222325ULL, cacheClass);
}

static u64 psp_gfx_pspgl_ci8_key_hash(const u8* indices, const u16* palette, u32 width, u32 height) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CI8);

    hash = psp_gfx_pspgl_hash_ptr(hash, indices);
    hash = psp_gfx_pspgl_hash_ptr(hash, palette);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    return hash;
}

static u64 psp_gfx_pspgl_ci8_base_hash(const u8* indices) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CI8);

    return psp_gfx_pspgl_hash_ptr(hash, indices);
}

static u64 psp_gfx_pspgl_rgba16_key_hash(const u16* pixels, u32 width, u32 height, int premultiply) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_RGBA16);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    hash = psp_gfx_pspgl_hash_u64(hash, premultiply ? 1 : 0);
    return hash;
}

static u64 psp_gfx_pspgl_rgba16_base_hash(const u16* pixels) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_RGBA16);

    return psp_gfx_pspgl_hash_ptr(hash, pixels);
}

static u64 psp_gfx_pspgl_converted_key_hash(const void* pixels, const u16* palette, u32 width, u32 height,
                                            PspGfxConvertedTextureFormat format) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CONVERTED);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_ptr(hash, palette);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    hash = psp_gfx_pspgl_hash_u64(hash, format);
    return hash;
}

static u64 psp_gfx_pspgl_converted_base_hash(const void* pixels, PspGfxConvertedTextureFormat format) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CONVERTED);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_u64(hash, format);
    return hash;
}
#endif

static void psp_gfx_pspgl_rgba16_to_rgba8(u16 color, u8* out) {
    out[0] = (u8) (((color >> 11) & 0x1F) * 255 / 31);
    out[1] = (u8) (((color >> 6) & 0x1F) * 255 / 31);
    out[2] = (u8) (((color >> 1) & 0x1F) * 255 / 31);
    out[3] = (color & 1) ? 255 : 0;
}

static u16 psp_gfx_pspgl_read_u16(const void* base, u32 index) {
    const u8* bytes = (const u8*) base + (index * 2);

    return (u16) bytes[0] | ((u16) bytes[1] << 8);
}

static int psp_gfx_pspgl_is_dark_rgba16_mask(const u16* pixels, u32 width, u32 height) {
    u32 pixelCount = width * height;
    u32 opaqueCount = 0;
    u32 i;

    for (i = 0; i < pixelCount; i++) {
        u16 color = psp_gfx_pspgl_read_u16(pixels, i);

        if ((color & 1) == 0) {
            continue;
        }
        opaqueCount++;
        if (((color >> 11) & 0x1F) > 2 || ((color >> 6) & 0x1F) > 2 || ((color >> 1) & 0x1F) > 2) {
            return 0;
        }
    }
    return (opaqueCount != 0) && (opaqueCount != pixelCount);
}

static u8 psp_gfx_pspgl_filtered_rgba16_alpha(const u16* pixels, u32 width, u32 height, u32 x, u32 y) {
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
            if ((psp_gfx_pspgl_read_u16(pixels, ((u32) sampleY * width) + (u32) sampleX) & 1) != 0) {
                alpha += 255U * weight;
            }
        }
    }
    return (u8) ((alpha + 8U) / 16U);
}

static u32 psp_gfx_pspgl_next_power_of_two(u32 value) {
    u32 result = PSP_GFX_PSPGL_MIN_TEXTURE_DIMENSION;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

#if SF64_PSP_PSPGL_VBO_STREAM
static const GLvoid* psp_gfx_pspgl_vertex_offset(u32 offset) {
    return (const GLvoid*) offset;
}
#endif

static void psp_gfx_pspgl_bind_client_arrays(const PspGfxPspglColorVertex* vertices) {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(PspGfxPspglColorVertex), &vertices[0].color);
    glVertexPointer(3, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].x);
}

#if SF64_PSP_PSPGL_VBO_STREAM
static void psp_gfx_pspgl_bind_vbo_arrays(GLuint buffer) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glTexCoordPointer(2, GL_FLOAT, sizeof(PspGfxPspglColorVertex),
                      psp_gfx_pspgl_vertex_offset(offsetof(PspGfxPspglColorVertex, u)));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(PspGfxPspglColorVertex),
                   psp_gfx_pspgl_vertex_offset(offsetof(PspGfxPspglColorVertex, color)));
    glVertexPointer(3, GL_FLOAT, sizeof(PspGfxPspglColorVertex),
                    psp_gfx_pspgl_vertex_offset(offsetof(PspGfxPspglColorVertex, x)));
}
#endif

static void psp_gfx_pspgl_init_vertex_stream(void) {
    u32 i;

    if (sVertexStreamInitialized) {
        return;
    }
    sVertexStreamInitialized = 1;
#if SF64_PSP_PSPGL_VBO_STREAM
    sVertexStreamAvailable = 1;
    glGenBuffers(PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_COUNT, &sVertexStreamSmallPages[0].buffer);
    for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_COUNT; i++) {
        if (sVertexStreamSmallPages[i].buffer == 0) {
            sVertexStreamAvailable = 0;
            break;
        }
        glBindBuffer(GL_ARRAY_BUFFER, sVertexStreamSmallPages[i].buffer);
        glBufferData(GL_ARRAY_BUFFER, PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    glGenBuffers(PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT, &sVertexStreamLargePages[0].buffer);
    for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT; i++) {
        if (sVertexStreamLargePages[i].buffer == 0) {
            sVertexStreamAvailable = 0;
            break;
        }
        glBindBuffer(GL_ARRAY_BUFFER, sVertexStreamLargePages[i].buffer);
        glBufferData(GL_ARRAY_BUFFER, PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!sVertexStreamAvailable) {
        for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_COUNT; i++) {
            if (sVertexStreamSmallPages[i].buffer != 0) {
                glDeleteBuffers(1, &sVertexStreamSmallPages[i].buffer);
                sVertexStreamSmallPages[i].buffer = 0;
            }
        }
        for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT; i++) {
            if (sVertexStreamLargePages[i].buffer != 0) {
                glDeleteBuffers(1, &sVertexStreamLargePages[i].buffer);
                sVertexStreamLargePages[i].buffer = 0;
            }
        }
    }
#else
    (void) i;
    sVertexStreamAvailable = 0;
#endif
}

static void psp_gfx_pspgl_reset_vertex_stream(void) {
    if (!sVertexStreamAvailable) {
        return;
    }
    sVertexStreamSetIndex = (sVertexStreamSetIndex + 1) % PSP_GFX_PSPGL_VERTEX_STREAM_SETS;
    sVertexStreamSmallPageIndex = 0;
    sVertexStreamLargePageIndex = 0;
}

static int psp_gfx_pspgl_find_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                                PspGfxConvertedTextureFormat format, u32* textureId,
                                                u32* uploadWidth, u32* uploadHeight, int countHit) {
    PspGfxConvertedTextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (textureId == NULL) || (uploadWidth == NULL) ||
        (uploadHeight == NULL) || ((format == PSP_GFX_TEXTURE_CI4) && (palette == NULL))) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_converted_key_hash(pixels, palette, width, height, format);
    baseHash = psp_gfx_pspgl_converted_base_hash(pixels, format);
#endif
    for (i = 0; i < sConvertedTextureCacheCount; i++) {
        entry = &sConvertedTextureCache[i];
        if ((entry->pixels == pixels) && (entry->palette == palette) && (entry->width == width) &&
            (entry->height == height) && (entry->format == format)) {
            *textureId = entry->texture;
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
                                                 PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE, sConvertedTextureCacheCount,
                                                 keyHash, baseHash, 1);
#endif
            if (countHit) {
                PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            }
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CONVERTED, PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE,
                                         sConvertedTextureCacheCount, keyHash, baseHash, 0);
#endif
    return 0;
}

static u32 psp_gfx_pspgl_create_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                                  PspGfxConvertedTextureFormat format, u32* uploadWidth,
                                                  u32* uploadHeight) {
    PspGfxConvertedTextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) || (uploadHeight == NULL) ||
        ((format == PSP_GFX_TEXTURE_CI4) && (palette == NULL))) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_converted_key_hash(pixels, palette, width, height, format);
    baseHash = psp_gfx_pspgl_converted_base_hash(pixels, format);
#endif
    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;
            u8* out = &sTextureUpload[dstIndex * 4];

            if (format == PSP_GFX_TEXTURE_CI4) {
                const u8* indices = (const u8*) pixels;
                u8 packed = indices[srcIndex >> 1];
                u8 index = (srcIndex & 1) ? (packed & 0xF) : (packed >> 4);

                psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(palette, index), out);
            } else if (format == PSP_GFX_TEXTURE_IA8) {
                u8 packed = ((const u8*) pixels)[srcIndex];
                u8 intensity = (packed >> 4) * 17;

                out[0] = intensity;
                out[1] = intensity;
                out[2] = intensity;
                out[3] = (packed & 0xF) * 17;
            } else {
                u16 packed = psp_gfx_pspgl_read_u16(pixels, srcIndex);
                u8 intensity = (u8) (packed >> 8);

                out[0] = intensity;
                out[1] = intensity;
                out[2] = intensity;
                out[3] = (u8) packed;
            }
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sConvertedTextureCacheCount < PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE) {
        entry = &sConvertedTextureCache[sConvertedTextureCacheCount++];
    } else {
        entry = &sConvertedTextureCache[sConvertedTextureCacheReplaceIndex++];
        sConvertedTextureCacheReplaceIndex %= PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
            psp_gfx_pspgl_converted_key_hash(entry->pixels, entry->palette, entry->width, entry->height,
                                             entry->format));
#endif
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->palette = palette;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->format = format;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
                                            PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE, sConvertedTextureCacheCount, keyHash,
                                            baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

static u32 psp_gfx_pspgl_get_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                               PspGfxConvertedTextureFormat format, u32* uploadWidth,
                                               u32* uploadHeight) {
    u32 textureId;

    if (psp_gfx_pspgl_find_converted_texture(pixels, palette, width, height, format, &textureId, uploadWidth,
                                             uploadHeight, 1)) {
        return textureId;
    }
    return psp_gfx_pspgl_create_converted_texture(pixels, palette, width, height, format, uploadWidth, uploadHeight);
}

void PspGfxPspgl_Init(void) {
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glShadeModel(GL_SMOOTH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    psp_gfx_pspgl_init_vertex_stream();
}

void PspGfxPspgl_BeginFrame(void) {
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_FALSE);
    psp_gfx_pspgl_reset_vertex_stream();
}

void PspGfxPspgl_Flush(void) {
    /*
     * PSPGL records state and draws in order, copies client-array vertices
     * before glDrawArrays returns, and submits internally when its GE list
     * fills. Keep explicit submission at the frame/task boundary.
     */
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_GL_FLUSH);
    glFlush();
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_GL_FLUSH);
    PspProfiler_CountGlFlush();
}

int PspGfxPspgl_FindCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               u32* uploadWidth, u32* uploadHeight) {
    PspGfxTextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((indices == NULL) || (palette == NULL) || (width == 0) || (height == 0) || (textureId == NULL) ||
        (uploadWidth == NULL) || (uploadHeight == NULL)) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_ci8_key_hash(indices, palette, width, height);
    baseHash = psp_gfx_pspgl_ci8_base_hash(indices);
#endif
    for (i = 0; i < sTextureCacheCount; i++) {
        entry = &sTextureCache[i];
        if ((entry->indices == indices) && (entry->palette == palette) && (entry->width == width) &&
            (entry->height == height)) {
            *textureId = entry->texture;
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CI8, PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE,
                                                 sTextureCacheCount, keyHash, baseHash, 1);
#endif
            PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CI8, PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE,
                                         sTextureCacheCount, keyHash, baseHash, 0);
#endif
    return 0;
}

u32 PspGfxPspgl_CreateCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight) {
    PspGfxTextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;

    if ((indices == NULL) || (palette == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) ||
        (uploadHeight == NULL)) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_ci8_key_hash(indices, palette, width, height);
    baseHash = psp_gfx_pspgl_ci8_base_hash(indices);
#endif

    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;

            psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(palette, indices[srcIndex]), &sTextureUpload[dstIndex * 4]);
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sTextureCacheCount < PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE) {
        entry = &sTextureCache[sTextureCacheCount++];
    } else {
        entry = &sTextureCache[sTextureCacheReplaceIndex++];
        sTextureCacheReplaceIndex %= PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_CI8,
            psp_gfx_pspgl_ci8_key_hash(entry->indices, entry->palette, entry->width, entry->height));
#endif
        glDeleteTextures(1, &entry->texture);
    }
    entry->indices = indices;
    entry->palette = palette;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_CI8, PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE,
                                            sTextureCacheCount, keyHash, baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight) {
    u32 textureId;

    if (PspGfxPspgl_FindCi8Texture(indices, palette, width, height, &textureId, uploadWidth, uploadHeight)) {
        return textureId;
    }
    return PspGfxPspgl_CreateCi8Texture(indices, palette, width, height, uploadWidth, uploadHeight);
}

int PspGfxPspgl_FindCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_find_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, textureId,
                                                uploadWidth, uploadHeight, 1);
}

u32 PspGfxPspgl_CreateCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight) {
    return psp_gfx_pspgl_create_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, uploadWidth,
                                                  uploadHeight);
}

u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight) {
    return psp_gfx_pspgl_get_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, uploadWidth,
                                               uploadHeight);
}

int PspGfxPspgl_FindRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* textureId,
                                  u32* uploadWidth, u32* uploadHeight) {
    PspGfxRgba16TextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (textureId == NULL) || (uploadWidth == NULL) ||
        (uploadHeight == NULL)) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_rgba16_key_hash(pixels, width, height, premultiply);
    baseHash = psp_gfx_pspgl_rgba16_base_hash(pixels);
#endif
    for (i = 0; i < sRgba16TextureCacheCount; i++) {
        entry = &sRgba16TextureCache[i];
        if ((entry->pixels == pixels) && (entry->width == width) && (entry->height == height) &&
            (entry->premultiplied == premultiply)) {
            *textureId = entry->texture;
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_RGBA16,
                                                 PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE, sRgba16TextureCacheCount,
                                                 keyHash, baseHash, 1);
#endif
            PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_RGBA16, PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE,
                                         sRgba16TextureCacheCount, keyHash, baseHash, 0);
#endif
    return 0;
}

u32 PspGfxPspgl_CreateRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                    u32* uploadHeight) {
    PspGfxRgba16TextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;
    int softenAlpha;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) || (uploadHeight == NULL)) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_rgba16_key_hash(pixels, width, height, premultiply);
    baseHash = psp_gfx_pspgl_rgba16_base_hash(pixels);
#endif
    softenAlpha = premultiply && psp_gfx_pspgl_is_dark_rgba16_mask(pixels, width, height);

    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;

            u8* out = &sTextureUpload[dstIndex * 4];

            psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(pixels, srcIndex), out);
            if (softenAlpha) {
                out[0] = 0;
                out[1] = 0;
                out[2] = 0;
                out[3] = psp_gfx_pspgl_filtered_rgba16_alpha(pixels, width, height, srcX, srcY);
            }
            if (premultiply) {
                out[0] = (u8) (((u32) out[0] * out[3] + 127U) / 255U);
                out[1] = (u8) (((u32) out[1] * out[3] + 127U) / 255U);
                out[2] = (u8) (((u32) out[2] * out[3] + 127U) / 255U);
            }
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sRgba16TextureCacheCount < PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE) {
        entry = &sRgba16TextureCache[sRgba16TextureCacheCount++];
    } else {
        entry = &sRgba16TextureCache[sRgba16TextureCacheReplaceIndex++];
        sRgba16TextureCacheReplaceIndex %= PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_RGBA16,
            psp_gfx_pspgl_rgba16_key_hash(entry->pixels, entry->width, entry->height, entry->premultiplied));
#endif
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->premultiplied = premultiply;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_RGBA16,
                                            PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE, sRgba16TextureCacheCount, keyHash,
                                            baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight) {
    u32 textureId;

    if (PspGfxPspgl_FindRgba16Texture(pixels, width, height, premultiply, &textureId, uploadWidth, uploadHeight)) {
        return textureId;
    }
    return PspGfxPspgl_CreateRgba16Texture(pixels, width, height, premultiply, uploadWidth, uploadHeight);
}

int PspGfxPspgl_FindIa8Texture(const u8* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                               u32* uploadHeight) {
    return psp_gfx_pspgl_find_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, textureId,
                                                uploadWidth, uploadHeight, 1);
}

u32 PspGfxPspgl_CreateIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_create_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, uploadWidth,
                                                  uploadHeight);
}

u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_get_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, uploadWidth,
                                               uploadHeight);
}

int PspGfxPspgl_FindIa16Texture(const u16* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                                u32* uploadHeight) {
    return psp_gfx_pspgl_find_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, textureId,
                                                uploadWidth, uploadHeight, 1);
}

u32 PspGfxPspgl_CreateIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_create_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, uploadWidth,
                                                  uploadHeight);
}

u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_get_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, uploadWidth,
                                               uploadHeight);
}

static void psp_gfx_pspgl_draw_client_arrays(const PspGfxPspglColorVertex* vertices, u32 vertexCount) {
    psp_gfx_pspgl_bind_client_arrays(vertices);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    PspProfiler_CountVertexStream(
        0, 0, 0, 0, 1, vertexCount, 0, PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES,
        (sVertexStreamSmallPageIndex * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES) +
            (sVertexStreamLargePageIndex * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES),
        0, 0, 0, 0);
}

static int psp_gfx_pspgl_draw_vbo_stream(const PspGfxPspglColorVertex* vertices, u32 vertexCount) {
#if SF64_PSP_PSPGL_VBO_STREAM
    PspGfxVertexStreamPage* page;
    void* mapped;
    u32 pageIndex;
    u32 bytes;
    u32 highWater;
    u32 pageSwitch;
    u32 smallDraw;
    u32 largeDraw;

    if (!sVertexStreamAvailable) {
        return 0;
    }

    smallDraw = vertexCount <= PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_VERTICES;
    largeDraw = !smallDraw && (vertexCount <= PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_VERTICES);
    if (smallDraw) {
        if (sVertexStreamSmallPageIndex >= PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET) {
            return 0;
        }
        pageIndex = (sVertexStreamSetIndex * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET) +
                    sVertexStreamSmallPageIndex;
        page = &sVertexStreamSmallPages[pageIndex];
        pageSwitch = (sVertexStreamSmallPageIndex != 0) || (sVertexStreamLargePageIndex != 0);
        highWater = ((sVertexStreamSmallPageIndex + 1) * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES) +
                    (sVertexStreamLargePageIndex * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES);
    } else if (largeDraw) {
        if (sVertexStreamLargePageIndex >= PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET) {
            return 0;
        }
        pageIndex = (sVertexStreamSetIndex * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET) +
                    sVertexStreamLargePageIndex;
        page = &sVertexStreamLargePages[pageIndex];
        pageSwitch = (sVertexStreamSmallPageIndex != 0) || (sVertexStreamLargePageIndex != 0);
        highWater = (sVertexStreamSmallPageIndex * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES) +
                    ((sVertexStreamLargePageIndex + 1) * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES);
    } else {
        return 0;
    }

    bytes = vertexCount * sizeof(PspGfxPspglColorVertex);
    (void) pageSwitch;
    (void) highWater;

    glBindBuffer(GL_ARRAY_BUFFER, page->buffer);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    mapped = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    if (mapped == NULL) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return 0;
    }
    memcpy(mapped, vertices, bytes);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);

    psp_gfx_pspgl_bind_vbo_arrays(page->buffer);
    if (smallDraw) {
        sVertexStreamSmallPageIndex++;
    } else {
        sVertexStreamLargePageIndex++;
    }
    PspProfiler_CountVertexStream(0, 0, 1, bytes, 0, 0, pageSwitch, PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES,
                                  highWater, 0, 0, 0, 0);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    PspProfiler_CountDrawCall(vertexCount);
    PspProfiler_CountVertexStream(1, vertexCount, 0, 0, 0, 0, 0, PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES,
                                  highWater, smallDraw, largeDraw, smallDraw ? vertexCount : 0,
                                  largeDraw ? vertexCount : 0);
    return 1;
#else
    (void) vertices;
    (void) vertexCount;
    return 0;
#endif
}

void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount,
    u32 textureId, PspGfxPspglTextureEnv textureEnv, PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT,
    int alphaTest, int blend, int premultiplied, int depthTest, int depthWrite, int fog, const float* fogColor,
    float fogStart, float fogEnd, const float* projectionMatrix, int pretransformed
) {
    GLint glTextureEnv;
    GLint glWrapS;
    GLint glWrapT;

    if ((vertices == NULL) || (vertexCount == 0)) {
        return;
    }

    glMatrixMode(GL_PROJECTION);

    if (pretransformed || (projectionMatrix == NULL)) {
        glLoadIdentity();
    } else {
        glLoadMatrixf(projectionMatrix);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);

    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);

    if (fog &&
        !pretransformed &&
        (fogColor != NULL) &&
        (fogEnd > fogStart)) {

        glEnable(GL_FOG);
        glFogf(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fogColor);
        glFogf(GL_FOG_START, fogStart * 0.45f);
        glFogf(GL_FOG_END, fogEnd * 0.5f);
    } else {
        glDisable(GL_FOG);
    }

    // this controls GL_TEXTURE_2D, but does not disable GL_TEXTURE_COORD_ARRAY for untextured geometry
    if (textureId != 0) {
        glEnable(GL_TEXTURE_2D);

        if (alphaTest) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.0f);
        } else {
            glDisable(GL_ALPHA_TEST);
        }

        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(
                premultiplied ? GL_ONE : GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA
            );
        } else {
            glDisable(GL_BLEND);
        }

        glBindTexture(GL_TEXTURE_2D, textureId);

        glWrapS =
            (wrapS == PSP_GFX_PSPGL_WRAP_CLAMP)
                ? GL_CLAMP_TO_EDGE
                : GL_REPEAT;

        glWrapT =
            (wrapT == PSP_GFX_PSPGL_WRAP_CLAMP)
                ? GL_CLAMP_TO_EDGE
                : GL_REPEAT;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrapT);

        if (textureEnv == PSP_GFX_PSPGL_TEX_MODULATE) {
            glTextureEnv = GL_MODULATE;
        } else {
            glTextureEnv = GL_REPLACE;
        }

        glTexEnvi(
            GL_TEXTURE_ENV,
            GL_TEXTURE_ENV_MODE,
            glTextureEnv
        );
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_BLEND);
    }

    if (!psp_gfx_pspgl_draw_vbo_stream(vertices, vertexCount)) {
        psp_gfx_pspgl_draw_client_arrays(vertices, vertexCount);
    }
}
