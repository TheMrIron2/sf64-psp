#ifndef PSP_GFX_PSPGL_H
#define PSP_GFX_PSPGL_H

#include "PR/ultratypes.h"

typedef struct {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
} PspGfxPspglColorVertex;

void PspGfxPspgl_Init(void);
void PspGfxPspgl_BeginFrame(void);
void PspGfxPspgl_DrawTestTriangle(void);
void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount);

#endif
