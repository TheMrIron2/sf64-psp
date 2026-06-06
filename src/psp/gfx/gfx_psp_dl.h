#ifndef PSP_GFX_PSP_DL_H
#define PSP_GFX_PSP_DL_H

#include "PR/ultratypes.h"
#include "libultra/ultra64.h"

typedef struct {
    u32 commandCount;
    u32 vertexCount;
    u32 triangleCount;
    u32 nestedDlFollowed;
    u32 nestedDlRejected;
    u32 unsupportedCount;
    u32 mtxCount;
    u32 commandLimitHit;
    u32 depthLimitHit;
    u32 drawVertexCount;
} PspGfxDlStats;

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats);

#endif
