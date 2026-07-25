#ifndef PSP_HW_COUNTER_PROFILE_H
#define PSP_HW_COUNTER_PROFILE_H

#include "PR/ultratypes.h"

void PspHwCounterProfile_Init(void);
void PspHwCounterProfile_Shutdown(void);
int PspHwCounterProfile_PollControls(u32 rawButtons);
void PspHwCounterProfile_BeginFrame(void);
void PspHwCounterProfile_EndFrame(u32 commands, u32 loadedVertices, u32 submittedVertices);
int PspHwCounterProfile_UseProjectedOutput(void);
void PspHwCounterProfile_DrawStatus(void);

#endif
