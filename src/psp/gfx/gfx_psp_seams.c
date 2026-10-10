#include "gfx_psp_seams.h"

void PspGfx_WeldFlatVertices(PspGfxVertex* vertices, u32 count) {
    float minZ;
    float maxZ;
    float eps;
    u32 i;
    u32 j;

    if (!vertices || count < 12 || count > 48) {
        return;
    }
    minZ = maxZ = vertices[0].z;
    for (i = 1; i < count; i++) {
        float z = vertices[i].z;

        if (z < minZ) {
            minZ = z;
        }
        if (z > maxZ) {
            maxZ = z;
        }
    }
    if ((maxZ >= 0.0f) || ((maxZ - minZ) > (0.001f * -minZ))) {
        return;
    }
    eps = 3.0e-4f * -minZ;
    for (i = 1; i < count; i++) {
        PspGfxVertex* b = &vertices[i];

        for (j = 0; j < i; j++) {
            const PspGfxVertex* a = &vertices[j];
            float dx = b->x - a->x;
            float dy = b->y - a->y;

            if (((dx != 0.0f) || (dy != 0.0f)) && (dx < eps) && (dx > -eps) && (dy < eps) && (dy > -eps)) {
                b->x = a->x;
                b->y = a->y;
                break;
            }
        }
    }
}
