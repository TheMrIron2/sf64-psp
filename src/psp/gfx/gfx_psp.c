#include "src/psp/gfx/gfx_psp.h"

#include "src/psp/platform.h"

#include <GLES/egl.h>

#define PSP_GFX_WIDTH 480
#define PSP_GFX_HEIGHT 272

static EGLDisplay sDisplay = EGL_NO_DISPLAY;
static EGLSurface sSurface = EGL_NO_SURFACE;
static EGLContext sContext = EGL_NO_CONTEXT;
static int sReady;

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

    eglSwapInterval(sDisplay, 1);
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
        eglSwapBuffers(sDisplay, sSurface);
    }
}

int PspGfx_GetWidth(void) {
    return PSP_GFX_WIDTH;
}

int PspGfx_GetHeight(void) {
    return PSP_GFX_HEIGHT;
}
