#ifndef PSP_HW_COUNTER_PROFILE_H
#define PSP_HW_COUNTER_PROFILE_H

#include "PR/ultratypes.h"

/* Hardware-counter capture, see docs/psp_hw_counter_profiling.md
 * Counters come from sceKernelReferThreadProfiler, never the profiler MMIO helpers */

#ifndef PROFILE_HW_COUNTERS
#define PROFILE_HW_COUNTERS 0
#endif

/* Inner frontend scopes, sampled per texture upload and per G_VTX so not free */
#ifndef PROFILE_HW_COUNTER_SCOPES
#define PROFILE_HW_COUNTER_SCOPES 0
#endif

#ifndef PROFILE_HW_COUNTER_FRAMES
#define PROFILE_HW_COUNTER_FRAMES 300
#endif

#ifndef PROFILE_HW_COUNTER_WARMUP_FRAMES
#define PROFILE_HW_COUNTER_WARMUP_FRAMES 120
#endif

typedef enum {
    /* Whole graphics task, same window the FPS overlay reports as GFX time */
    PSP_HW_SCOPE_TASK,
    /* Display list frontend, up to but excluding the final PSPGL flush */
    PSP_HW_SCOPE_FRONTEND,
    /* PSPGL VBO upload, command generation and draw submission */
    PSP_HW_SCOPE_FLUSH,
    /* End of frame swap and sync, outside PSP_HW_SCOPE_TASK */
    PSP_HW_SCOPE_PRESENT,
    /* Nested inside the frontend, PROFILE_HW_COUNTER_SCOPES only */
    PSP_HW_SCOPE_TEXTURE,
    PSP_HW_SCOPE_VERTEX,
    /* PSPGL draw submission, which happens per batch inside the frontend
     * not in the end of task flush, so it needs its own scope */
    PSP_HW_SCOPE_SUBMIT,
    PSP_HW_SCOPE_COUNT
} PspHwCounterScope;

#if PROFILE_HW_COUNTERS

void PspHwCounterProfile_Init(void);
void PspHwCounterProfile_Shutdown(void);
int PspHwCounterProfile_PollControls(u32 rawButtons);
void PspHwCounterProfile_FrameBegin(void);
void PspHwCounterProfile_FrameEnd(u32 commands, u32 loadedVertices, u32 submittedVertices);
void PspHwCounterProfile_ScopeBegin(PspHwCounterScope scope);
void PspHwCounterProfile_ScopeEnd(PspHwCounterScope scope);
void PspHwCounterProfile_DrawStatus(void);

#else

#define PspHwCounterProfile_Init() ((void) 0)
#define PspHwCounterProfile_Shutdown() ((void) 0)
#define PspHwCounterProfile_PollControls(rawButtons) (0)
#define PspHwCounterProfile_FrameBegin() ((void) 0)
#define PspHwCounterProfile_FrameEnd(commands, loadedVertices, submittedVertices) ((void) 0)
#define PspHwCounterProfile_ScopeBegin(scope) ((void) 0)
#define PspHwCounterProfile_ScopeEnd(scope) ((void) 0)
#define PspHwCounterProfile_DrawStatus() ((void) 0)

#endif

#if PROFILE_HW_COUNTERS && PROFILE_HW_COUNTER_SCOPES
#define PspHwCounterProfile_InnerScopeBegin(scope) PspHwCounterProfile_ScopeBegin(scope)
#define PspHwCounterProfile_InnerScopeEnd(scope) PspHwCounterProfile_ScopeEnd(scope)
#else
#define PspHwCounterProfile_InnerScopeBegin(scope) ((void) 0)
#define PspHwCounterProfile_InnerScopeEnd(scope) ((void) 0)
#endif

#endif
