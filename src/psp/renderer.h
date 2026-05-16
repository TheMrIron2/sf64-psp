#ifndef PSP_RENDERER_H
#define PSP_RENDERER_H

#include "PR/ultratypes.h"
#include "sf64thread.h"

void PspRenderer_Init(void);
void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex);

#endif