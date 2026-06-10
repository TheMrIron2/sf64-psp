#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"

#include <GLES/gl.h>
#include <stddef.h>

#define PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE 64
#define PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS (64 * 64)
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
    GLuint texture;
} PspGfxRgba16TextureCacheEntry;

static PspGfxTextureCacheEntry sTextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
static PspGfxRgba16TextureCacheEntry sRgba16TextureCache[PSP_GFX_PSPGL_TEXTURE_CACHE_SIZE];
static u8 sTextureUpload[PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS * 4];
static u32 sTextureCacheCount;
static u32 sRgba16TextureCacheCount;
static u32 sTextureCacheReplaceIndex;
static u32 sRgba16TextureCacheReplaceIndex;

static void psp_gfx_pspgl_rgba16_to_rgba8(u16 color, u8* out) {
    out[0] = (u8) (((color >> 11) & 0x1F) * 255 / 31);
    out[1] = (u8) (((color >> 6) & 0x1F) * 255 / 31);
    out[2] = (u8) (((color >> 1) & 0x1F) * 255 / 31);
    out[3] = (color & 1) ? 255 : 0;
}

static u32 psp_gfx_pspgl_next_power_of_two(u32 value) {
    u32 result = PSP_GFX_PSPGL_MIN_TEXTURE_DIMENSION;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

void PspGfxPspgl_Init(void) {
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

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
    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

            psp_gfx_pspgl_rgba16_to_rgba8(palette[indices[srcIndex]], &sTextureUpload[dstIndex * 4]);
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

u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight) {
    PspGfxRgba16TextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
    u32 x;
    u32 y;
    u32 i;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) || (uploadHeight == NULL)) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }

    for (i = 0; i < sRgba16TextureCacheCount; i++) {
        entry = &sRgba16TextureCache[i];
        if ((entry->pixels == pixels) && (entry->width == width) && (entry->height == height)) {
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

            psp_gfx_pspgl_rgba16_to_rgba8(pixels[srcIndex], &sTextureUpload[dstIndex * 4]);
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

void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount, u32 textureId) {
    if ((vertices == NULL) || (vertexCount == 0)) {
        return;
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (textureId != 0) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(GL_GREATER, 0.0f);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexCoordPointer(2, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].u);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    } else {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_ALPHA_TEST);
    }

    glVertexPointer(3, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].x);
    glColorPointer(4, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].r);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glFlush();
}
