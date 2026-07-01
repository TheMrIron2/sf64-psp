#ifndef PSP_RENDERER_H
#define PSP_RENDERER_H

#include "PR/ultratypes.h"
#include "PR/gbi.h"
#include "sf64thread.h"

#define PSP_RENDERER_DL_MARKER_MAGIC 0x50535200u /* "PSR" plus marker byte. */
#define PSP_RENDERER_DL_MARKER_ID_MASK 0xffu
#define PSP_RENDERER_DL_MARKER_STARFIELD 1u
#define PSP_RENDERER_DL_MARKER(tag) \
    (PSP_RENDERER_DL_MARKER_MAGIC | ((u32) (tag) & PSP_RENDERER_DL_MARKER_ID_MASK))
#define PSP_RENDERER_DL_MARKER_MATCH(tag) \
    (((tag) & ~PSP_RENDERER_DL_MARKER_ID_MASK) == PSP_RENDERER_DL_MARKER_MAGIC)
#define PSP_RENDERER_DL_MARKER_ID(tag) \
    ((u32) ((tag) & PSP_RENDERER_DL_MARKER_ID_MASK))
#define PSP_RENDERER_DL_STARFIELD_MARKER(pkt) \
    gDPNoOpTag((pkt), PSP_RENDERER_DL_MARKER(PSP_RENDERER_DL_MARKER_STARFIELD))

void PspRenderer_Init(void);
void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex);

void PspRenderer_BeginStarfield(void);
void PspRenderer_AddStar(s16 x, s16 y, u32 n64FillColor);
void PspRenderer_EndStarfield(void);
void PspRenderer_DrawPendingStarfield(void);

#endif
