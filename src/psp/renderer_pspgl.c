#include "PR/ultratypes.h"
#include "sf64thread.h"
#include "src/psp/platform.h"
#include "src/psp/renderer.h"

#include <GLES/egl.h>
#include <GLES/gl.h>

#define PSPGL_SCREEN_WIDTH 480
#define PSPGL_SCREEN_HEIGHT 272

static EGLDisplay sPspglDisplay = EGL_NO_DISPLAY;
static EGLSurface sPspglSurface = EGL_NO_SURFACE;
static EGLContext sPspglContext = EGL_NO_CONTEXT;
static int sPspglReady;
static int sPspglLoggedIgnoredTask;

static void pspgl_renderer_log_failure(const char* phase) {
    PspPlatform_LogLine(phase);
}

static void pspgl_renderer_clear_and_swap(void) {
    glViewport(0, 0, PSPGL_SCREEN_WIDTH, PSPGL_SCREEN_HEIGHT);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.02f, 0.45f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFlush();

    if (sPspglDisplay != EGL_NO_DISPLAY && sPspglSurface != EGL_NO_SURFACE) {
        eglSwapBuffers(sPspglDisplay, sPspglSurface);
    }
}

void PspRenderer_Init(void) {
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

    if (sPspglReady) {
        return;
    }

    sPspglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (sPspglDisplay == EGL_NO_DISPLAY) {
        pspgl_renderer_log_failure("[pspgl] eglGetDisplay failed");
        return;
    }

    if (!eglInitialize(sPspglDisplay, &major, &minor)) {
        pspgl_renderer_log_failure("[pspgl] eglInitialize failed");
        return;
    }

    if (!eglChooseConfig(sPspglDisplay, configAttribs, &config, 1, &configCount) || configCount == 0) {
        pspgl_renderer_log_failure("[pspgl] eglChooseConfig failed");
        return;
    }

    sPspglSurface = eglCreateWindowSurface(sPspglDisplay, config, 0, NULL);
    if (sPspglSurface == EGL_NO_SURFACE) {
        pspgl_renderer_log_failure("[pspgl] eglCreateWindowSurface failed");
        return;
    }

    sPspglContext = eglCreateContext(sPspglDisplay, config, EGL_NO_CONTEXT, NULL);
    if (sPspglContext == EGL_NO_CONTEXT) {
        pspgl_renderer_log_failure("[pspgl] eglCreateContext failed");
        return;
    }

    if (!eglMakeCurrent(sPspglDisplay, sPspglSurface, sPspglSurface, sPspglContext)) {
        pspgl_renderer_log_failure("[pspgl] eglMakeCurrent failed");
        return;
    }

    eglSwapInterval(sPspglDisplay, 1);
    sPspglReady = 1;
    PspPlatform_LogLine("[pspgl] renderer init");
    pspgl_renderer_clear_and_swap();
}

void PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex) {
    (void) task;
    (void) taskIndex;

    if (!sPspglReady) {
        PspRenderer_Init();
    }
    if (!sPspglReady) {
        return;
    }

    if (!sPspglLoggedIgnoredTask) {
        sPspglLoggedIgnoredTask = 1;
        PspPlatform_LogLine("[pspgl] render task ignored");
    }

    pspgl_renderer_clear_and_swap();
}

void PspRenderer_BeginStarfield(void) {
}

void PspRenderer_AddStar(s16 x, s16 y, u32 n64FillColor) {
    (void) x;
    (void) y;
    (void) n64FillColor;
}

void PspRenderer_EndStarfield(void) {
}
