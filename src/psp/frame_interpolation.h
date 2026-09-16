#ifndef PSP_FRAME_INTERPOLATION_H
#define PSP_FRAME_INTERPOLATION_H

#include "PR/ultratypes.h"
#include "sf64math.h"
#include "sf64thread.h"

typedef enum {
    PSP_FRAME_INTERPOLATION_WRAP_NONE,
    PSP_FRAME_INTERPOLATION_WRAP_X,
    PSP_FRAME_INTERPOLATION_WRAP_Y,
    PSP_FRAME_INTERPOLATION_WRAP_Z,
} PspFrameInterpolationWrapAxis;

void PspFrameInterpolation_Reset(void);
void PspFrameInterpolation_BeginSimulationFrame(SPTask* task, u32 simulationVi, u8 simulationVIs, s32 record);
void PspFrameInterpolation_SetWorldScope(s32 enabled);
void PspFrameInterpolation_SetMatrixIdentity(const void* identity);
void PspFrameInterpolation_SetMatrixWrap(PspFrameInterpolationWrapAxis axis, f32 distance, f32 scale);
void PspFrameInterpolation_RecordMatrix(const Mtx* matrix, u32 flags, const void* drawSite);
void PspFrameInterpolation_EndSimulationFrame(SPTask* task, s32 eligible);
SPTask* PspFrameInterpolation_PreparePresentation(SPTask* task, u32 presentationVi, u8 presentationVIs,
                                                  s32 renderOnly);
s32 PspFrameInterpolation_ResolveMatrix(const Mtx* current, u32 flags, Matrix* result);
s32 PspFrameInterpolation_ShouldPresent(SPTask* task);
void PspFrameInterpolation_FinishPresentation(SPTask* task);

#endif
