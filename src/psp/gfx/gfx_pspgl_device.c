#include "src/psp/gfx/gfx_pspgl_device.h"

#include "src/psp/platform.h"
#include "src/psp/profiler.h"

#include <GLES/egl.h>

#ifndef PSPGL_SWAP_INTERVAL
#define PSPGL_SWAP_INTERVAL 0
#endif

static EGLDisplay sDisplay = EGL_NO_DISPLAY;
static EGLSurface sSurface = EGL_NO_SURFACE;
static EGLContext sContext = EGL_NO_CONTEXT;
static int sReady;

static void psp_gfx_pspgl_device_log_failure(const char* phase) {
    PspPlatform_LogLine(phase);
}

int PspGfxPspglDevice_Init(void) {
    EGLConfig config;
    EGLint configCount;
    EGLint major;
    EGLint minor;
    const EGLint configAttribs[] = {
        EGL_RED_SIZE,   5,       EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5, EGL_DEPTH_SIZE, 16, EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT, EGL_NONE
    };

    if (sReady) {
        return 1;
    }

    sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (sDisplay == EGL_NO_DISPLAY) {
        psp_gfx_pspgl_device_log_failure("[pspgl] eglGetDisplay failed");
        return 0;
    }

    if (!eglInitialize(sDisplay, &major, &minor)) {
        psp_gfx_pspgl_device_log_failure("[pspgl] eglInitialize failed");
        PspGfxPspglDevice_Shutdown();
        return 0;
    }

    if (!eglChooseConfig(sDisplay, configAttribs, &config, 1, &configCount) || configCount == 0) {
        psp_gfx_pspgl_device_log_failure("[pspgl] eglChooseConfig failed");
        PspGfxPspglDevice_Shutdown();
        return 0;
    }

    sSurface = eglCreateWindowSurface(sDisplay, config, 0, NULL);
    if (sSurface == EGL_NO_SURFACE) {
        psp_gfx_pspgl_device_log_failure("[pspgl] eglCreateWindowSurface failed");
        PspGfxPspglDevice_Shutdown();
        return 0;
    }

    sContext = eglCreateContext(sDisplay, config, EGL_NO_CONTEXT, NULL);
    if (sContext == EGL_NO_CONTEXT) {
        psp_gfx_pspgl_device_log_failure("[pspgl] eglCreateContext failed");
        PspGfxPspglDevice_Shutdown();
        return 0;
    }

    if (!eglMakeCurrent(sDisplay, sSurface, sSurface, sContext)) {
        psp_gfx_pspgl_device_log_failure("[pspgl] eglMakeCurrent failed");
        PspGfxPspglDevice_Shutdown();
        return 0;
    }

    eglSwapInterval(sDisplay, PSPGL_SWAP_INTERVAL);
    sReady = 1;
    return 1;
}

int PspGfxPspglDevice_IsReady(void) {
    return sReady;
}

void PspGfxPspglDevice_BeginFrame(void) {
}

int PspGfxPspglDevice_EndFrame(void) {
    EGLBoolean result;

    if (sReady && sDisplay != EGL_NO_DISPLAY && sSurface != EGL_NO_SURFACE) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_FINISH_SYNC);
        result = eglSwapBuffers(sDisplay, sSurface);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_FINISH_SYNC);
        if (result != EGL_FALSE) {
            PspProfiler_CountSync();
        }
        return result != EGL_FALSE;
    }

    return 0;
}

void PspGfxPspglDevice_Shutdown(void) {
    if (sDisplay != EGL_NO_DISPLAY) {
        if (sContext != EGL_NO_CONTEXT) {
            eglMakeCurrent(sDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (sSurface != EGL_NO_SURFACE) {
            eglDestroySurface(sDisplay, sSurface);
        }
        if (sContext != EGL_NO_CONTEXT) {
            eglDestroyContext(sDisplay, sContext);
        }
        eglTerminate(sDisplay);
    }

    sDisplay = EGL_NO_DISPLAY;
    sSurface = EGL_NO_SURFACE;
    sContext = EGL_NO_CONTEXT;
    sReady = 0;
}

void* PspGfxPspglDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    if (!sReady || sSurface == EGL_NO_SURFACE) {
        return NULL;
    }

    return eglGetPresentedFrameBufferPSP(sSurface, stride, pixelFormat);
}
