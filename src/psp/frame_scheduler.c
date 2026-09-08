#include "src/psp/frame_scheduler.h"

static u8 psp_frame_scheduler_clamp_vis(u8 vis) {
    if (vis == 0) {
        return 1;
    }
    if (vis > 4) {
        return 4;
    }
    return vis;
}

static u32 psp_frame_scheduler_advance_deadline(u32 deadline, u32 currentVi, u8 interval, u32* missed) {
    u32 deadlines;

    deadlines = ((currentVi - deadline) / interval) + 1;
    *missed = deadlines - 1;
    return deadline + (deadlines * interval);
}

void PspFrameScheduler_Init(PspFrameScheduler* scheduler, u32 currentVi, u8 simulationVIs, u8 presentationVIs) {
    scheduler->simulationVIs = psp_frame_scheduler_clamp_vis(simulationVIs);
    scheduler->presentationVIs = psp_frame_scheduler_clamp_vis(presentationVIs);
    scheduler->nextSimulationVi = currentVi + scheduler->simulationVIs;
    scheduler->nextPresentationVi = currentVi + scheduler->presentationVIs;
    scheduler->lastVi = currentVi;
}

PspFrameSchedule PspFrameScheduler_Advance(PspFrameScheduler* scheduler, u32 currentVi) {
    PspFrameSchedule schedule = { 0 };

    schedule.elapsedVIs = currentVi - scheduler->lastVi;
    scheduler->lastVi = currentVi;

    if ((s32) (currentVi - scheduler->nextSimulationVi) >= 0) {
        schedule.simulationDue = 1;
        scheduler->nextSimulationVi =
            psp_frame_scheduler_advance_deadline(scheduler->nextSimulationVi, currentVi, scheduler->simulationVIs,
                                                 &schedule.missedSimulationDeadlines);
    }
    if ((s32) (currentVi - scheduler->nextPresentationVi) >= 0) {
        schedule.presentationDue = 1;
        scheduler->nextPresentationVi =
            psp_frame_scheduler_advance_deadline(scheduler->nextPresentationVi, currentVi,
                                                 scheduler->presentationVIs,
                                                 &schedule.missedPresentationDeadlines);
    }
    return schedule;
}

void PspFrameScheduler_SetSimulationVIs(PspFrameScheduler* scheduler, u32 currentVi, u8 simulationVIs) {
    simulationVIs = psp_frame_scheduler_clamp_vis(simulationVIs);
    if (scheduler->simulationVIs != simulationVIs) {
        scheduler->simulationVIs = simulationVIs;
        scheduler->nextSimulationVi = currentVi + simulationVIs;
    }
}

void PspFrameScheduler_SetPresentationVIs(PspFrameScheduler* scheduler, u32 currentVi, u8 presentationVIs) {
    presentationVIs = psp_frame_scheduler_clamp_vis(presentationVIs);
    if (scheduler->presentationVIs != presentationVIs) {
        scheduler->presentationVIs = presentationVIs;
        scheduler->nextPresentationVi = currentVi + presentationVIs;
    }
}
