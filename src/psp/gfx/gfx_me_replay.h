#ifndef PSP_GFX_ME_REPLAY_H
#define PSP_GFX_ME_REPLAY_H

#include "PR/ultratypes.h"
#include "PR/gbi.h"

typedef struct {
    u32 commandCount;
    u32 nestedDlCount;
    u32 gvtxCommandCount;
    u32 loadedVertexCount;
    u32 matrixCommandCount;
    u32 tri1CommandCount;
    u32 tri2CommandCount;
    u32 inputTriangleCount;
    u32 textureRectangleCount;
    u32 commandHash;
    u32 commandLimitHit;
    u32 depthLimitHit;
} PspGfxMeReplayStats;

int PspGfxMeReplay_Walk(const Gfx* dl, PspGfxMeReplayStats* stats);

#endif
