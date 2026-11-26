#ifndef PSP_GU_CENSUS_H
#define PSP_GU_CENSUS_H

#include "PR/ultratypes.h"

#if PROFILE_CENSUS
void PspCensus_Start(void);
void PspCensus_Save(void);
const char* PspCensus_Status(void);
void PspCensus_Begin(void);
void PspCensus_Interpolation(void* task);
void PspCensus_Mark(int stage);
void PspCensus_End(int presented);
void PspCensus_Simulation(int end, int taskInFlight, s32 pool, u32 deadlineVi);
void PspCensus_GfxWait(int end, u32 reasons);
void PspCensus_Prepare(int end, int renderOnly);
void PspCensus_Event(u32 kind, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h);
#else
#define PspCensus_Start() ((void) 0)
#define PspCensus_Save() ((void) 0)
#define PspCensus_Status() "disabled"
#define PspCensus_Begin() ((void) 0)
#define PspCensus_Interpolation(task) ((void) 0)
#define PspCensus_Mark(stage) ((void) 0)
#define PspCensus_End(presented) ((void) 0)
#define PspCensus_Simulation(end, taskInFlight, pool, deadlineVi) ((void) 0)
#define PspCensus_GfxWait(end, reasons) ((void) 0)
#define PspCensus_Prepare(end, renderOnly) ((void) 0)
#define PspCensus_Event(kind, a, b, c, d, e, f, g, h) ((void) 0)
#endif
#endif
