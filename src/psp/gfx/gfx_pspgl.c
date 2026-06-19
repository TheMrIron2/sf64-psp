#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"

#include <GLES/gl.h>
#include <stddef.h>

#define PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE 64
#define PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS (256 * 32)
#define PSP_GFX_PSPGL_MIN_TEXTURE_DIMENSION 8

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

static PspGfxTextureCacheEntry sTextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
static PspGfxRgba16TextureCacheEntry sRgba16TextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
static PspGfxConvertedTextureCacheEntry sConvertedTextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
static u8 sTextureUpload[PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS * 4];
static u32 sTextureCacheCount;
static u32 sRgba16TextureCacheCount;
static u32 sTextureCacheReplaceIndex;
static u32 sRgba16TextureCacheReplaceIndex;
static u32 sConvertedTextureCacheCount;
static u32 sConvertedTextureCacheReplaceIndex;

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

static u32 psp_gfx_pspgl_get_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                               PspGfxConvertedTextureFormat format, u32* uploadWidth,
                                               u32* uploadHeight) {
    PspGfxConvertedTextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
    u32 x;
    u32 y;
    u32 i;

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
    for (i = 0; i < sConvertedTextureCacheCount; i++) {
        entry = &sConvertedTextureCache[i];
        if ((entry->pixels == pixels) && (entry->palette == palette) && (entry->width == width) &&
            (entry->height == height) && (entry->format == format)) {
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
            return entry->texture;
        }
    }

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

    if (sConvertedTextureCacheCount < PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE) {
        entry = &sConvertedTextureCache[sConvertedTextureCacheCount++];
    } else {
        entry = &sConvertedTextureCache[sConvertedTextureCacheReplaceIndex++];
        sConvertedTextureCacheReplaceIndex %= PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE;
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->palette = palette;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->format = format;
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
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
}

u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight) {
    PspGfxTextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
    u32 x;
    u32 y;
    u32 i;

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

    for (i = 0; i < sTextureCacheCount; i++) {
        entry = &sTextureCache[i];
        if ((entry->indices == indices) && (entry->palette == palette) && (entry->width == width) &&
            (entry->height == height)) {
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
            return entry->texture;
        }
    }
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;

            psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(palette, indices[srcIndex]), &sTextureUpload[dstIndex * 4]);
        }
    }

    if (sTextureCacheCount < PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE) {
        entry = &sTextureCache[sTextureCacheCount++];
    } else {
        entry = &sTextureCache[sTextureCacheReplaceIndex++];
        sTextureCacheReplaceIndex %= PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE;
        glDeleteTextures(1, &entry->texture);
    }
    entry->indices = indices;
    entry->palette = palette;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight) {
    return psp_gfx_pspgl_get_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, uploadWidth,
                                               uploadHeight);
}

u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight) {
    PspGfxRgba16TextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
    u32 x;
    u32 y;
    u32 i;
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
    softenAlpha = premultiply && psp_gfx_pspgl_is_dark_rgba16_mask(pixels, width, height);

    for (i = 0; i < sRgba16TextureCacheCount; i++) {
        entry = &sRgba16TextureCache[i];
        if ((entry->pixels == pixels) && (entry->width == width) && (entry->height == height) &&
            (entry->premultiplied == premultiply)) {
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
            return entry->texture;
        }
    }
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

    if (sRgba16TextureCacheCount < PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE) {
        entry = &sRgba16TextureCache[sRgba16TextureCacheCount++];
    } else {
        entry = &sRgba16TextureCache[sRgba16TextureCacheReplaceIndex++];
        sRgba16TextureCacheReplaceIndex %= PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE;
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->premultiplied = premultiply;
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_get_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, uploadWidth,
                                               uploadHeight);
}

u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_get_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, uploadWidth,
                                               uploadHeight);
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

    glTexCoordPointer(
        2,
        GL_FLOAT,
        sizeof(PspGfxPspglColorVertex),
        &vertices[0].u
    );

    glColorPointer(
        4,
        GL_UNSIGNED_BYTE,
        sizeof(PspGfxPspglColorVertex),
        &vertices[0].color
    );

    glVertexPointer(
        3,
        GL_FLOAT,
        sizeof(PspGfxPspglColorVertex),
        &vertices[0].x
    );

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

    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glFlush();
}
