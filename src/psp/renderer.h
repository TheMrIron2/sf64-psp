#ifndef PSP_RENDERER_H
#define PSP_RENDERER_H

#include "PR/ultratypes.h"
#include "sf64thread.h"

void PspRenderer_Init(void);
void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex);

void PspRenderer_BeginStarfield(void);
void PspRenderer_AddStar(s16 x, s16 y, u32 n64FillColor);
void PspRenderer_EndStarfield(void);

#endif