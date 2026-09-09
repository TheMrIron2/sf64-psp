#ifndef PSP_FRAME_INTERPOLATION_H
#define PSP_FRAME_INTERPOLATION_H

#include "PR/ultratypes.h"
#include "sf64math.h"
#include "sf64thread.h"

void PspFrameInterpolation_Reset(void);
void PspFrameInterpolation_BeginSimulationFrame(SPTask* task, u32 simulationVi, u8 simulationVIs, s32 record);
void PspFrameInterpolation_SetWorldScope(s32 enabled);
void PspFrameInterpolation_RecordMatrix(const Mtx* matrix, u32 flags);
void PspFrameInterpolation_EndSimulationFrame(SPTask* task, s32 eligible);
SPTask* PspFrameInterpolation_PreparePresentation(SPTask* task, u32 presentationVi, u8 presentationVIs,
                                                  s32 renderOnly);
s32 PspFrameInterpolation_ResolveMatrix(const Mtx* current, u32 flags, Matrix* result);
void PspFrameInterpolation_FinishPresentation(SPTask* task);

#endif
