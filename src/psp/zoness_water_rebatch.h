#ifndef PSP_ZONESS_WATER_REBATCH_H
#define PSP_ZONESS_WATER_REBATCH_H

// Included with the game Gfx and u32 definitions
#define PSP_WATER_REBATCH_COMMANDS 382
#define PSP_WATER_REBATCH_PAIRS 256
#define PSP_WATER_REBATCH_SLOTS 63
#define PSP_WATER_REBATCH_GROUPS 8

#include "src/psp/zoness_water_cull.h"

typedef struct {
    u32 slots[16];
    u32 triangles[PSP_WATER_REBATCH_PAIRS][6];
    u32 vertices[PSP_WATER_REBATCH_SLOTS];
} PspWaterRebatchScratch;

static u32 PspWaterRebatch_Index(const u32* vertices, u32 count, u32 address) {
    u32 i;

    for (i = 0; i < count; i++) {
        if (vertices[i] == address) {
            break;
        }
    }
    return i;
}

static int PspWaterRebatch_Load(Gfx* out, u32* output, u32 address, u32 count, u32 slot) {
    if ((*output >= PSP_WATER_REBATCH_COMMANDS) || (count == 0) || (count > 63) || (slot + count > 64)) {
        return 0;
    }
    out[*output].words.w0 = 0x04000000U | (slot << 17) | (count << 10) | (count * 16U - 1U);
    out[(*output)++].words.w1 = address;
    return 1;
}

static int PspWaterRebatch_Build(Gfx* out, const Gfx* source, PspWaterRebatchScratch* scratch,
                                 PspWaterRebatchBounds* bounds, const Vtx* mesh,
                                 PspWaterRebatchXz* xzSnapshot) {
    static const u32 prefixOpcodes[8] = { 0xE8, 0xF5, 0xF2, 0xFD, 0xE8, 0xF5, 0xE6, 0xF3 };
    u32 input;
    u32 output = 8;
    u32 triangle = 0;
    u32 loads = 0;
    u32 loaded = 0;
    u32 first;
    u32 i;
    u32 group = 0;
    u32 meshAddress = (u32) (unsigned long) mesh;
    for (i = 0; i < 289; i++) {
        xzSnapshot[i].x = mesh[i].v.ob[0];
        xzSnapshot[i].z = mesh[i].v.ob[2];
    }

    for (i = 0; i < 16; i++) {
        scratch->slots[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        if ((source[i].words.w0 >> 24) != prefixOpcodes[i]) {
            return 0;
        }
        out[i] = source[i];
    }
    for (input = 8; input < 723; input++) {
        u32 w0 = source[input].words.w0;
        u32 w1 = source[input].words.w1;

        if ((w0 >> 24) == 4) {
            u32 count = (w0 >> 10) & 63U;
            u32 slot = (w0 >> 17) & 127U;

            if ((count == 0) || (slot + count > 16) || (w1 == 0) || (w1 & 15U) ||
                (w1 > 0xFFFFFFFFU - count * 16U) ||
                (w0 != (0x04000000U | (slot << 17) | (count << 10) | (count * 16U - 1U)))) {
                return 0;
            }
            for (i = 0; i < count; i++) {
                scratch->slots[slot + i] = w1 + i * 16U;
            }
            loads++;
            loaded += count;
        } else if ((w0 == 0xBF000000U) && ((w1 & 0xFF000000U) == 0) && (triangle < 512)) {
            for (i = 0; i < 3; i++) {
                u32 index = (w1 >> (16U - i * 8U)) & 255U;

                if ((index & 1U) || (index >= 32) || (scratch->slots[index / 2U] == 0)) {
                    return 0;
                }
                scratch->triangles[triangle / 2U][(triangle & 1U) * 3U + i] = scratch->slots[index / 2U];
            }
            triangle++;
        } else {
            return 0;
        }
    }
    if ((loads != 203) || (loaded != 583) || (triangle != 512) ||
        (source[723].words.w0 != 0xB8000000U) || (source[723].words.w1 != 0)) {
        return 0;
    }
    for (i = 0; i < 16; i++) {
        if (scratch->slots[i] == 0) {
            return 0;
        }
    }

    first = 0;
    while (first < PSP_WATER_REBATCH_PAIRS) {
        u32 end = first;
        u32 count = 0;
        u32 start;
        u32 marker;

        while (end < PSP_WATER_REBATCH_PAIRS) {
            u32 previousCount = count;

            for (i = 0; i < 6; i++) {
                u32 address = scratch->triangles[end][i];

                if (PspWaterRebatch_Index(scratch->vertices, count, address) == count) {
                    if (count == PSP_WATER_REBATCH_SLOTS) {
                        break;
                    }
                    scratch->vertices[count++] = address;
                }
            }
            if (i != 6) {
                count = previousCount;
                break;
            }
            end++;
        }
        for (i = 1; i < count; i++) {
            u32 address = scratch->vertices[i];
            u32 j = i;

            while ((j != 0) && (scratch->vertices[j - 1] > address)) {
                scratch->vertices[j] = scratch->vertices[j - 1];
                j--;
            }
            scratch->vertices[j] = address;
        }
        if (group >= PSP_WATER_REBATCH_GROUPS) {
            return 0;
        }
        bounds[group].minX = bounds[group].minZ = 32767;
        bounds[group].maxX = bounds[group].maxZ = -32768;
        bounds[group].pairs = (u16) (end - first);
        bounds[group].meshAddress = meshAddress;
        bounds[group].xzSnapshotAddress = (u32) (unsigned long) xzSnapshot;
        for (i = 0; i < count; i++) {
            u32 address = scratch->vertices[i];
            u32 index;
            const Vtx* vertex;

            if (address < meshAddress || address - meshAddress >= 289U * 16U ||
                ((address - meshAddress) & 15U) != 0) {
                return 0;
            }
            index = (address - meshAddress) / 16U;
            vertex = &mesh[index];
            if (vertex->v.ob[0] < bounds[group].minX) bounds[group].minX = vertex->v.ob[0];
            if (vertex->v.ob[0] > bounds[group].maxX) bounds[group].maxX = vertex->v.ob[0];
            if (vertex->v.ob[2] < bounds[group].minZ) bounds[group].minZ = vertex->v.ob[2];
            if (vertex->v.ob[2] > bounds[group].maxZ) bounds[group].maxZ = vertex->v.ob[2];
        }
        marker = output++;
        if (marker >= PSP_WATER_REBATCH_COMMANDS) {
            return 0;
        }
        out[marker].words.w1 = (u32) (unsigned long) &bounds[group];
        start = 0;
        while (start < count) {
            u32 next = start + 1;

            while ((next < count) && (scratch->vertices[next] == scratch->vertices[next - 1] + 16U)) {
                next++;
            }
            if (!PspWaterRebatch_Load(out, &output, scratch->vertices[start], next - start, start)) {
                return 0;
            }
            start = next;
        }
        for (; first < end; first++) {
            u32 words[2] = { 0, 0 };

            if (output >= PSP_WATER_REBATCH_COMMANDS) {
                return 0;
            }
            for (i = 0; i < 6; i++) {
                u32 index = PspWaterRebatch_Index(scratch->vertices, count, scratch->triangles[first][i]);

                words[i / 3U] |= (index * 2U) << (16U - (i % 3U) * 8U);
            }
            out[output].words.w0 = 0xB1000000U | words[0];
            out[output++].words.w1 = words[1];
        }
        if (output - marker - 1U > 255U) {
            return 0;
        }
        out[marker].words.w0 = PSP_WATER_GROUP_MARKER | (output - marker - 1U);
        group++;
    }

    if (group != PSP_WATER_REBATCH_GROUPS) {
        return 0;
    }

    // Restore every vertex slot written by the original list
    i = 0;
    while (i < 16) {
        u32 next = i + 1;

        while ((next < 16) && (scratch->slots[next] == scratch->slots[next - 1] + 16U)) {
            next++;
        }
        if (!PspWaterRebatch_Load(out, &output, scratch->slots[i], next - i, i)) {
            return 0;
        }
        i = next;
    }
    if (output >= PSP_WATER_REBATCH_COMMANDS) {
        return 0;
    }
    out[output++] = source[723];
    return (int) output;
}

#endif
