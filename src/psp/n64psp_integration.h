#ifndef SF64_PSP_N64PSP_INTEGRATION_H
#define SF64_PSP_N64PSP_INTEGRATION_H

int PspN64psp_Init(void);
void PspN64psp_Shutdown(void);

#if N64PSP_QUEUE_SELFTEST
int PspN64psp_RunPlatformSelfTest(void);
int PspN64psp_RunQueueSelfTest(void);
#endif

#endif
