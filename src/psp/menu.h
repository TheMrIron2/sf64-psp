#ifndef PSP_MENU_H
#define PSP_MENU_H

#include "PR/ultratypes.h"

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#define PSP_MENU_DIAGNOSTICS (PROFILE_CENSUS || PROFILE_GPROF || PROFILE_PHASES || PROFILE_HW_COUNTERS || PSP_RENDERER_DIAGNOSTICS || (PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU))
#define PSP_MENU_CAPTURES (PROFILE_CENSUS || PROFILE_GPROF || PROFILE_PHASES || PROFILE_HW_COUNTERS)

typedef enum {
    PSP_MULTIPLAYER_PENDING,
    PSP_MULTIPLAYER_SOLO,
    PSP_MULTIPLAYER_BACK
} PspMultiplayerMenuResult;

void PspMenu_OpenMultiplayer(int matchType);
PspMultiplayerMenuResult PspMenu_GetMultiplayerResult(void);
void PspMenu_Update(u32 pressed);
int PspMenu_IsOpen(void);
int PspMenu_NeedsBoundary(void);
void PspMenu_Apply(void);
void PspMenu_Publish(void);
void PspMenu_Draw(void);
int PspMenu_Visible(void);
#if PSP_MENU_CAPTURES
void PspMenu_RequestCapture(int start);
#endif

#endif
