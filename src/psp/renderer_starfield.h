#ifndef PSP_RENDERER_STARFIELD_H
#define PSP_RENDERER_STARFIELD_H

#include "PR/ultratypes.h"
#include "sf64thread.h"

void PspStarfield_BeginSimulationFrame(s32 pool);
void PspStarfield_Begin(void);
void PspStarfield_Add(u16 sourceIndex, s16 x, s16 y, u32 n64FillColor);
void PspStarfield_End(void);
void PspStarfield_PreparePresentation(s32 pool, s32 previousPool, f32 alpha);
void PspStarfield_DrawPending(void);

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#if PSP_RENDERER_DIAGNOSTICS
void PspStarfield_DiagCounts(u32 requested, u32 traversed);
#endif

#endif
