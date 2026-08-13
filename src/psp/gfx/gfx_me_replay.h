#ifndef PSP_GFX_ME_REPLAY_H
#define PSP_GFX_ME_REPLAY_H

#include "PR/ultratypes.h"
#include "PR/gbi.h"

#define PSP_GFX_ME_TRANSFORM_TRACE_CAPACITY 4096
#define PSP_GFX_ME_TRIANGLE_TRACE_CAPACITY 8192
#define PSP_GFX_ME_TRIANGLE_PUBLISH_CHUNK 128
#define PSP_GFX_ME_TRANSFORM_VALID 1U
#define PSP_GFX_ME_TRANSFORM_PROJECTED 2U
#define PSP_GFX_ME_TRANSFORM_VME 4U
#define PSP_GFX_ME_TRACE_DONE 0x80000000U
#define PSP_GFX_ME_TRACE_COUNT_MASK 0x7FFFFFFFU
#define PSP_GFX_ME_TRIANGLE_CODE_REJECTED 0x40U
#define PSP_GFX_ME_TRIANGLE_CODE_INVALID 0x80U

typedef enum {
    PSP_GFX_ME_TRIANGLE_INVALID,
    PSP_GFX_ME_TRIANGLE_REJECTED,
    PSP_GFX_ME_TRIANGLE_PARTIAL,
    PSP_GFX_ME_TRIANGLE_DIRECT,
    PSP_GFX_ME_TRIANGLE_DEFERRED,
} PspGfxMeTriangleClass;

typedef struct {
    u8 vertex[3];
    u8 classification;
    u8 sharedClipCode;
    u8 combinedClipCode;
    u16 reserved;
} PspGfxMeTriangleResult;

typedef u8 PspGfxMeTriangleCode;

typedef struct {
    float x;
    float y;
    float z;
    float viewX;
    float viewY;
    float viewZ;
    float viewW;
    float clipX;
    float clipY;
    float clipZ;
    float clipW;
    const float (*projection)[4];
    u32 projectionSerial;
    u32 projectionSnapshot;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    s16 s;
    s16 t;
    u32 clipCode;
    int valid;
} PspGfxMeVertex;

typedef struct {
    float u;
    float v;
    u32 color;
    float x;
    float y;
    float z;
} PspGfxMeReadyVertex;

typedef char PspGfxMeReadyVertexSizeCheck[
    (sizeof(PspGfxMeReadyVertex) == 24) ? 1 : -1
];

typedef struct __attribute__((aligned(64))) {
    const PspGfxMeVertex* source[6];
    void* destination;
    u8 vertexCount;
    u8 combineMode;
    u8 flags;
    u8 primitive[4];
    u8 environment[4];
    float textureScaleS;
    float textureScaleT;
    float textureOffsetS;
    float textureOffsetT;
    PspGfxMeReadyVertex output[6];
} PspGfxMeReadyPacket;

#define PSP_GFX_ME_READY_PRETRANSFORMED 1U
#define PSP_GFX_ME_READY_DEPTH_BIAS 2U
#define PSP_GFX_ME_READY_PREMULTIPLIED 4U
#define PSP_GFX_ME_READY_SHADE 8U
#define PSP_GFX_ME_READY_BAKED_ENV 16U

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
    u32 privateLitVertexCount;
    u32 transformTraceCount;
    u32 transformTraceOverflow;
    u32 triangleResultCount;
    u32 triangleResultOverflow;
} PspGfxMeReplayStats;

int PspGfxMeReplay_Walk(const Gfx* dl, PspGfxMeReplayStats* stats,
                        const void* sourceBase, const void* snapshotBase,
                        u32 snapshotSize, PspGfxMeTransformTrace* trace,
                        u32 traceCapacity, volatile u32* tracePublished,
                        PspGfxMeTriangleCode* triangles,
                        u32 triangleCapacity, volatile u32* trianglesPublished,
                        volatile u32* vmeStage);
void PspGfxMeReplay_VmeInit(volatile u32* vmeStage);

#endif
