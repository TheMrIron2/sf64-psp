#ifndef PSP_DISPLAY_H
#define PSP_DISPLAY_H

float PspDisplay_GetProjectionAspect(void);
int PspDisplay_IsWidescreen(void);
float PspDisplay_GetUiScaleY(void);
float PspDisplay_UiFromLeft(float x);
int PspDisplay_IsUiScalingEnabled(void);
void PspDisplay_ToggleUiScaling(void);

#endif
