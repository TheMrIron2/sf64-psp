#include "gfx_psp_retained_sources.h"
#include <malloc.h>

extern Gfx aVenomFighter1DL[];
extern Gfx ast_title_seg6_gfx_2C4B0[];
extern Gfx ast_title_seg6_gfx_20070[];
extern Gfx aAwBodyDL[];
extern Gfx aAwFoxHeadDL[];
extern Gfx ast_title_seg6_gfx_24710[];
extern Gfx ast_title_seg6_gfx_27130[];

// Qualified leaves avoid at least 32 position loads and restore at most one third
const PspGfxDlRetainedSource sRetainedSources[PSP_RETAINED_CACHE_SLOTS] = {
    { aVenomFighter1DL, 59 },
    { ast_title_seg6_gfx_2C4B0, 160 },
    { ast_title_seg6_gfx_20070, 151 },
    { aAwBodyDL, 161 },
    { aAwFoxHeadDL, 187 },
    { ast_title_seg6_gfx_24710, 137 },
    { ast_title_seg6_gfx_27130, 151 },
};

static u32 sRetainedAllocatedBytes;

void* PspGfxRetainedSource_Allocate(u32 bytes) {
    void* allocation;
    if (bytes > PSP_RETAINED_CACHE_BYTES - sRetainedAllocatedBytes) return NULL;
    allocation = memalign(16, bytes);
    if (allocation) sRetainedAllocatedBytes += bytes;
    return allocation;
}

u32 PspGfxRetainedSource_Find(const Gfx* commands, u32* count) {
    u32 slot;
    for (slot = 0; slot < PSP_RETAINED_CACHE_SLOTS; slot++) {
        if (commands == sRetainedSources[slot].commands) {
            *count = sRetainedSources[slot].count;
            return slot;
        }
    }
    return PSP_RETAINED_CACHE_SLOTS;
}
