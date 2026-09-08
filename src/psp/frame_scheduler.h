#ifndef PSP_FRAME_SCHEDULER_H
#define PSP_FRAME_SCHEDULER_H

#include "PR/ultratypes.h"

typedef struct {
    u32 nextSimulationVi;
    u32 nextPresentationVi;
    u32 lastVi;
    u8 simulationVIs;
    u8 presentationVIs;
} PspFrameScheduler;

typedef struct {
    u32 elapsedVIs;
    u32 missedSimulationDeadlines;
    u32 missedPresentationDeadlines;
    u8 simulationDue;
    u8 presentationDue;
} PspFrameSchedule;

void PspFrameScheduler_Init(PspFrameScheduler* scheduler, u32 currentVi, u8 simulationVIs, u8 presentationVIs);
PspFrameSchedule PspFrameScheduler_Advance(PspFrameScheduler* scheduler, u32 currentVi);
void PspFrameScheduler_SetSimulationVIs(PspFrameScheduler* scheduler, u32 currentVi, u8 simulationVIs);
void PspFrameScheduler_SetPresentationVIs(PspFrameScheduler* scheduler, u32 currentVi, u8 presentationVIs);

#endif
