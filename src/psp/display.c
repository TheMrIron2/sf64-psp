#include <n64psp/display.h>

#include "src/psp/display.h"
#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/platform.h"

static int sReady;
static int sUiScalingEnabled;
static n64psp_display_config sDisplayConfig;

int PspDisplay_Init(void) {
    if (sReady) {
        return 1;
    }
    if (!n64psp_display_configure(&sDisplayConfig, N64PSP_DISPLAY_PSP_480X272)) {
        return 0;
    }
    sReady = 1;
    return 1;
}

const n64psp_display_config* PspDisplay_GetConfig(void) {
    return &sDisplayConfig;
}

float PspDisplay_GetProjectionAspect(void) {
    return sDisplayConfig.display_aspect;
}

int PspDisplay_IsWidescreen(void) {
    return sDisplayConfig.mode == N64PSP_DISPLAY_PSP_480X272;
}

float PspDisplay_GetUiScaleY(void) {
    return (float) sDisplayConfig.ui_viewport_height / (float) sDisplayConfig.logical_height;
}

float PspDisplay_UiFromLeft(float x) {
    return n64psp_ui_from_left(&sDisplayConfig, x);
}

int PspDisplay_IsUiScalingEnabled(void) {
    return sUiScalingEnabled;
}

void PspDisplay_ToggleUiScaling(void) {
    sUiScalingEnabled = !sUiScalingEnabled;
    PspGfxBackend_ReplayCacheInvalidate();
    PspPlatform_LogLine(sUiScalingEnabled ? "[psp-display] UI scaling on" : "[psp-display] UI scaling off");
}

void PspDisplay_CycleMode(void) {
    n64psp_display_mode mode;

    if (!sReady && !PspDisplay_Init()) {
        return;
    }

    mode = (n64psp_display_mode) ((sDisplayConfig.mode + 1) % N64PSP_DISPLAY_PSP_MODE_COUNT);
    if (!n64psp_display_configure(&sDisplayConfig, mode)) {
        return;
    }
    PspGfxBackend_ReplayCacheInvalidate();

    if (mode == N64PSP_DISPLAY_PSP_320X240) {
        PspPlatform_LogLine("[psp-display] original 320x240");
    } else if (mode == N64PSP_DISPLAY_PSP_362X272) {
        PspPlatform_LogLine("[psp-display] 4:3 362x272");
    } else {
        PspPlatform_LogLine("[psp-display] widescreen 480x272");
    }
}
