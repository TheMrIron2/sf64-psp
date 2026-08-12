#ifndef PSP_GFX_ME_REPLAY_H
#define PSP_GFX_ME_REPLAY_H

#include "PR/ultratypes.h"
#include "PR/gbi.h"

#define PSP_GFX_ME_TRANSFORM_TRACE_CAPACITY 4096
#define PSP_GFX_ME_TRANSFORM_VALID 1U
#define PSP_GFX_ME_TRANSFORM_PROJECTED 2U
#define PSP_GFX_ME_TRANSFORM_VME 4U
#define PSP_GFX_ME_TRACE_DONE 0x80000000U
#define PSP_GFX_ME_TRACE_COUNT_MASK 0x7FFFFFFFU

typedef struct {
    float view[4];
    float clip[4];
    u32 slot;
    u32 flags;
} PspGfxMeTransformTrace;

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
    u32 transformedVertexCount;
    u32 transformTraceCount;
    u32 transformTraceOverflow;
} PspGfxMeReplayStats;

int PspGfxMeReplay_Walk(const Gfx* dl, PspGfxMeReplayStats* stats,
                        const void* sourceBase, const void* snapshotBase,
                        u32 snapshotSize, PspGfxMeTransformTrace* trace,
                        u32 traceCapacity, volatile u32* tracePublished,
                        volatile u32* vmeStage);
void PspGfxMeReplay_VmeInit(volatile u32* vmeStage);

#endif
