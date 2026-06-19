/*
 * LEGACY RENDERER NOTICE
 *
 * This texture-cache interface belongs to the retired direct-GU renderer and
 * is not part of the current PSP build. The active renderer is the PSPGL path,
 * and the authoritative PSP source list is src/psp/sources.mk. This file is
 * retained only as legacy/reference code.
 */

#ifndef PSP_RENDERER_TEXTURE_H
#define PSP_RENDERER_TEXTURE_H

#include "PR/ultratypes.h"

typedef struct {
    u32* pixels;
    u32 width;
    u32 height;
    u32 textureWidth;
    u32 textureHeight;
    int cacheHit;
} PspRendererTexture;

void PspRendererTexture_Reset(void);

int PspRendererTexture_Get(
    const void* source,
    u32 fmt,
    u32 siz,
    u32 width,
    u32 height,
    u32 sourceStride,
    u32 sourceS,
    u32 sourceT,
    const u32* palette,
    u32 paletteCount,
    PspRendererTexture* out
);

#endif
