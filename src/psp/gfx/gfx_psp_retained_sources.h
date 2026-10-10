#ifndef PSP_GFX_RETAINED_SOURCES_H
#define PSP_GFX_RETAINED_SOURCES_H

#include "gfx_psp_dl.h"
#include <n64psp/native_mesh.h>

#include "retained_assets.h"
#define PSP_RETAINED_CACHE_BYTES (3 * 1024 * 1024)

typedef struct {
    const Gfx* commands;
    u32 count;
    u32 baseline;
} PspGfxDlRetainedSource;

typedef struct {
    n64psp_mesh_packet mesh;
    float bounds[2][3];
} PspGfxDlRetainedCache;
typedef char PspGfxDlRetainedPrefixCheck[
    (((sizeof(PspGfxDlRetainedCache) + 15) & ~(size_t) 15) == 192) ? 1 : -1
];

extern PspGfxDlRetainedStats sRetainedStats;
void PspGfxRetainedSource_Prepare(void);
PspGfxDlRetainedCache* PspGfxRetainedSource_Get(u32 slot);

extern const PspGfxDlRetainedSource sRetainedSources[PSP_RETAINED_CACHE_SLOTS];
typedef struct { u32 slots[2], count; } PspGfxDlRetainedWrites;
extern const PspGfxDlRetainedWrites sRetainedWrites[PSP_RETAINED_CACHE_SLOTS];
extern const n64psp_mesh_command* sRetainedContinuations[32];
extern u32 sRetainedCallerLevels;
int PspGfxRetainedSource_OutputsDead(u32 slot, u32 depth, u32 budget);
void* PspGfxRetainedSource_Allocate(u32 bytes);
u32 PspGfxRetainedSource_Find(const Gfx* commands, u32* count);

#endif
