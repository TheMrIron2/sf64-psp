#ifndef PSP_ZONESS_WATER_CULL_H
#define PSP_ZONESS_WATER_CULL_H

#include "PR/ultratypes.h"
#include "PR/gbi.h"

#define PSP_WATER_GROUP_MARKER (((u32) G_NOOP << 24) | 0x00574700U)
#define PSP_WATER_GROUP_MARKER_MATCH(word) (((word) & 0xFFFFFF00U) == PSP_WATER_GROUP_MARKER)

typedef struct {
    s16 x;
    s16 z;
} PspWaterRebatchXz;

typedef struct {
    s16 minX;
    s16 maxX;
    s16 minZ;
    s16 maxZ;
    u16 pairs;
    u32 meshAddress;
    u32 xzSnapshotAddress;
} PspWaterRebatchBounds;

#endif
