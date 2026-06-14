#ifndef PSP_INPUT_H
#define PSP_INPUT_H

#include "PR/os_cont.h"

void PspInput_Init(void);
int PspInput_Poll(OSContPad* pads);

#endif
