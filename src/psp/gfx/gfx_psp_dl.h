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
    u32 firstUnsupportedOpcode;
    u32 mtxCount;
    u32 mtxPushCount;
    u32 mtxPopCount;
    u32 mtxStackRejected;
    u32 viewportCount;
    u32 invalidVertexCount;
    u32 outsideVertexCount;
    u32 textureCount;
    u32 textureRejected;
    u32 rgba16TextureCount;
    u32 ci4TextureCount;
    u32 ia8TextureCount;
    u32 ia16TextureCount;
    u32 texturedTriangleCount;
    u32 textureRectangleCount;
    u32 textureRectangleRejected;
    u32 commandLimitHit;
    u32 depthLimitHit;
    u32 drawVertexCount;
} PspGfxDlStats;

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats);

#endif
