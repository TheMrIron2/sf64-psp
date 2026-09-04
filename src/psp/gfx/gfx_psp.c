#include "src/psp/gfx/gfx_psp.h"

#include "src/psp/display.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"

#include <GLES/egl.h>

#define PSP_GFX_WIDTH 480
#define PSP_GFX_HEIGHT 272

#ifndef PSPGL_SWAP_INTERVAL
#define PSPGL_SWAP_INTERVAL 0
#endif

static EGLDisplay sDisplay = EGL_NO_DISPLAY;
static EGLSurface sSurface = EGL_NO_SURFACE;
static EGLContext sContext = EGL_NO_CONTEXT;
static int sReady;
static n64psp_display_config sDisplayConfig;

static void psp_gfx_log_failure(const char* phase) {
    PspPlatform_LogLine(phase);
}

int PspGfx_Init(void) {
    EGLConfig config;
    EGLint configCount;
    EGLint major;
    EGLint minor;
    const EGLint configAttribs[] = {
        EGL_RED_SIZE, 5,
        EGL_GREEN_SIZE, 6,
        EGL_BLUE_SIZE, 5,
        EGL_DEPTH_SIZE, 16,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };

    if (sReady) {
        return 1;
    }
    if (!n64psp_display_configure(&sDisplayConfig, N64PSP_DISPLAY_PSP_480X272)) {
        return 0;
    }

    sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (sDisplay == EGL_NO_DISPLAY) {
        psp_gfx_log_failure("[pspgl] eglGetDisplay failed");
        return 0;
    }

    if (!eglInitialize(sDisplay, &major, &minor)) {
        psp_gfx_log_failure("[pspgl] eglInitialize failed");
        return 0;
    }

    if (!eglChooseConfig(sDisplay, configAttribs, &config, 1, &configCount) || configCount == 0) {
        psp_gfx_log_failure("[pspgl] eglChooseConfig failed");
        return 0;
    }

    sSurface = eglCreateWindowSurface(sDisplay, config, 0, NULL);
    if (sSurface == EGL_NO_SURFACE) {
        psp_gfx_log_failure("[pspgl] eglCreateWindowSurface failed");
        return 0;
    }

    sContext = eglCreateContext(sDisplay, config, EGL_NO_CONTEXT, NULL);
    if (sContext == EGL_NO_CONTEXT) {
        psp_gfx_log_failure("[pspgl] eglCreateContext failed");
        return 0;
    }

    if (!eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)) {
        psp_gfx_log_failure("[pspgl] eglMakeCurrent failed");
        return 0;
    }

    eglSwapInterval(sDisplay, PSPGL_SWAP_INTERVAL);
    sReady = 1;
    return 1;
}

int PspGfx_IsReady(void) {
    return sReady;
}

void PspGfx_BeginFrame(void) {
}

void PspGfx_EndFrame(void) {
    if (sReady && sDisplay != EGL_NO_DISPLAY && sSurface != EGL_NO_SURFACE) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_FINISH_SYNC);
        eglSwapBuffers(sDisplay, sSurface);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_FINISH_SYNC);
        PspProfiler_CountSync();
    }
}

void* PspGfx_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    if (!sReady || sSurface == EGL_NO_SURFACE) {
        return NULL;
    }

    return eglGetPresentedFrameBufferPSP(sSurface, stride, pixelFormat);
}

int PspGfx_GetWidth(void) {
    return PSP_GFX_WIDTH;
}

int PspGfx_GetHeight(void) {
    return PSP_GFX_HEIGHT;
}

const n64psp_display_config* PspGfx_GetDisplayConfig(void) {
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

void PspGfx_CycleDisplayMode(void) {
    n64psp_display_mode mode =
        (n64psp_display_mode)((sDisplayConfig.mode + 1) % N64PSP_DISPLAY_PSP_MODE_COUNT);

    if (n64psp_display_configure(&sDisplayConfig, mode)) {
        if (mode == N64PSP_DISPLAY_PSP_320X240) {
            PspPlatform_LogLine("[psp-display] original 320x240");
        } else if (mode == N64PSP_DISPLAY_PSP_362X272) {
            PspPlatform_LogLine("[psp-display] 4:3 362x272");
        } else {
            PspPlatform_LogLine("[psp-display] widescreen 480x272");
        }
    }
}
