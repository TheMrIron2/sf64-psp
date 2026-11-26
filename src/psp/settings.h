#ifndef PSP_SETTINGS_H
#define PSP_SETTINGS_H

#include <n64psp/display.h>
#include "src/psp/gfx/gfx_psp_backend.h"

typedef enum {
    PSP_GRAPHICS_ACCURATE,
    PSP_GRAPHICS_FAST,
} PspGraphicsQuality;

typedef enum {
    PSP_GE_LIGHTING_OFF,
    PSP_GE_LIGHTING_DIFFUSE,
    PSP_GE_LIGHTING_POWERED,
} PspGeLightingMode;

int PspSettings_UseFastGraphics(void);
int PspSettings_UseVisualSine(void);
int PspSettings_ReuseTextureBlock(void);
PspGeLightingMode PspSettings_GetGeLighting(void);
int PspSettings_UseGeSideClip(void);
int PspSettings_PrepareGePersistentVertices(void);
PspGraphicsQuality PspSettings_GetGraphicsQuality(void);
PspGraphicsQuality PspSettings_GetActiveGraphicsQuality(void);
void PspSettings_SetGraphicsQuality(PspGraphicsQuality quality);
void PspSettings_Init(const char* executablePath, PspGfxBackendKind buildDefault);
PspGfxBackendKind PspSettings_GetGraphicsBackend(void);
int PspSettings_SetGraphicsBackend(PspGfxBackendKind backend);
int PspSettings_GraphicsBackendSaveFailed(void);
void PspSettings_SetActiveGraphicsBackend(PspGfxBackendKind backend);
PspGfxBackendKind PspSettings_GetActiveGraphicsBackend(void);
void PspSettings_SetDisplayMode(n64psp_display_mode mode);
void PspSettings_SetPresentationHz(int hz);
int PspSettings_NeedsBoundary(void);
void PspSettings_Apply(void);
int PspSettings_PerformanceVisible(void);
void PspSettings_TogglePerformance(void);

#endif
