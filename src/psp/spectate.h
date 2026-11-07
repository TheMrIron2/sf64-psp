#ifndef PSP_SPECTATE_H
#define PSP_SPECTATE_H

void PspSpectate_Start(void);
void PspSpectate_Stop(void);
int PspSpectate_IsWatching(void);
const char* PspSpectate_Status(void);
void PspSpectate_BeforeGame(void);
int PspSpectate_OverrideSimulation(void);
void PspSpectate_Capture(void);

#endif
