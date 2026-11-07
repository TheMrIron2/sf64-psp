#ifndef PSP_INPUT_H
#define PSP_INPUT_H

#include "PR/os_cont.h"

typedef struct {
    const char* pspName;
    const char* n64Name;
    u32 pspMask;
    u16 n64Mask;
} PspInputBinding;

const PspInputBinding* PspInput_GetBindings(int* count);
void PspInput_Init(void);
void PspInput_Update(void);
void PspInput_Poll(OSContPad* pads);
void PspInput_FilterGameInput(OSContPad* pads);
void PspInput_RequireRelease(void);

#endif
