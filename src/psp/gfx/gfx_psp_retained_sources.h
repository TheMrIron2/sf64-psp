#ifndef PSP_GFX_RETAINED_SOURCES_H
#define PSP_GFX_RETAINED_SOURCES_H

#include "gfx_psp_dl.h"

#include "retained_assets.h"
#define PSP_RETAINED_CACHE_BYTES (3 * 1024 * 1024)

typedef struct {
    const Gfx* commands;
    u32 count;
} PspGfxDlRetainedSource;

extern const PspGfxDlRetainedSource sRetainedSources[PSP_RETAINED_CACHE_SLOTS];
void* PspGfxRetainedSource_Allocate(u32 bytes);
u32 PspGfxRetainedSource_Find(const Gfx* commands, u32* count);

#endif
