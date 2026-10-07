#include "gfx_psp_retained_sources.h"
#include <malloc.h>

#include "retained_assets.inc.c"

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
