#include <n64psp/display.h>

#include "src/psp/gfx/gfx_psp_gu_device.h"

#include "src/psp/display.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <stdint.h>

#define PSP_GU_SCREEN_WIDTH 480
#define PSP_GU_SCREEN_HEIGHT 272
#define PSP_GU_FRAMEBUFFER_WIDTH 512
#define PSP_GU_BYTES_PER_PIXEL 2
#define PSP_GU_FRAMEBUFFER_BYTES (PSP_GU_FRAMEBUFFER_WIDTH * PSP_GU_SCREEN_HEIGHT * PSP_GU_BYTES_PER_PIXEL)
#define PSP_GU_REQUIRED_VRAM (PSP_GU_FRAMEBUFFER_BYTES * 3)
#define PSP_GU_LIST_WORDS 262144
#define PSP_GU_CLEAR_COLOR GU_RGBA(0, 0, 0, 255)

static unsigned int sGuList[PSP_GU_LIST_WORDS] __attribute__((aligned(64)));
static unsigned int sDrawBufferOffset;
static unsigned int sDisplayBufferOffset;
static unsigned int sDepthBufferOffset;
static unsigned int sPresentedBufferOffset;
static int sGuInitialized;
static int sReady;
static int sFrameActive;
static int sFrameSubmitted;

static void psp_gfx_gu_device_log_failure(const char* line) {
    PspPlatform_LogLine(line);
}

static void psp_gfx_gu_device_reset_state(void) {
    sDrawBufferOffset = 0;
    sDisplayBufferOffset = 0;
    sDepthBufferOffset = 0;
    sPresentedBufferOffset = 0;
    sGuInitialized = 0;
    sReady = 0;
    sFrameActive = 0;
    sFrameSubmitted = 0;
}

static void psp_gfx_gu_device_set_viewport(const n64psp_display_config* display) {
    sceGuOffset(2048 - (display->framebuffer_width / 2), 2048 - (display->framebuffer_height / 2));
    sceGuViewport(2048 - (display->framebuffer_width / 2) + display->viewport_x +
                      (display->viewport_width / 2),
                  2048 - (display->framebuffer_height / 2) + display->viewport_y +
                      (display->viewport_height / 2),
                  display->viewport_width, display->viewport_height);
}

static void psp_gfx_gu_device_clear_color_rect(int x, int y, int width, int height) {
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    sceGuScissor(x, y, width, height);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

static void psp_gfx_gu_device_clear_display_borders(const n64psp_display_config* display) {
    int right = display->viewport_x + display->viewport_width;
    int bottom = display->viewport_y + display->viewport_height;

    if ((display->viewport_x == 0) && (display->viewport_y == 0) &&
        (right == display->framebuffer_width) && (bottom == display->framebuffer_height)) {
        return;
    }

    if (display->viewport_x > 0) {
        psp_gfx_gu_device_clear_color_rect(0, 0, display->viewport_x, display->framebuffer_height);
    }
    if (right < display->framebuffer_width) {
        psp_gfx_gu_device_clear_color_rect(right, 0, display->framebuffer_width - right,
                                           display->framebuffer_height);
    }
    if (display->viewport_y > 0) {
        psp_gfx_gu_device_clear_color_rect(display->viewport_x, 0, display->viewport_width,
                                           display->viewport_y);
    }
    if (bottom < display->framebuffer_height) {
        psp_gfx_gu_device_clear_color_rect(display->viewport_x, bottom, display->viewport_width,
                                           display->framebuffer_height - bottom);
    }
}

int PspGfxGuDevice_Init(void) {
    unsigned int vramSize;
    int listResult;
    int syncResult;

    if (sReady) {
        return 1;
    }

    vramSize = sceGeEdramGetSize();
    if (vramSize < PSP_GU_REQUIRED_VRAM) {
        psp_gfx_gu_device_log_failure("[gu] insufficient VRAM for framebuffer setup");
        PspPlatform_LogValue("[gu] VRAM bytes", vramSize);
        PspPlatform_LogValue("[gu] VRAM required", PSP_GU_REQUIRED_VRAM);
        return 0;
    }

    sDrawBufferOffset = 0;
    sDisplayBufferOffset = PSP_GU_FRAMEBUFFER_BYTES;
    sDepthBufferOffset = PSP_GU_FRAMEBUFFER_BYTES * 2;
    sPresentedBufferOffset = sDisplayBufferOffset;

    if (sceGuInit() < 0) {
        psp_gfx_gu_device_log_failure("[gu] sceGuInit failed");
        psp_gfx_gu_device_reset_state();
        return 0;
    }
    sGuInitialized = 1;

    if (sceGuStart(GU_DIRECT, sGuList) < 0) {
        psp_gfx_gu_device_log_failure("[gu] setup list start failed");
        goto fail;
    }

    sceGuDrawBuffer(GU_PSM_5650, (void*) sDrawBufferOffset, PSP_GU_FRAMEBUFFER_WIDTH);
    sceGuDispBuffer(PSP_GU_SCREEN_WIDTH, PSP_GU_SCREEN_HEIGHT, (void*) sDisplayBufferOffset,
                    PSP_GU_FRAMEBUFFER_WIDTH);
    sceGuDepthBuffer((void*) sDepthBufferOffset, PSP_GU_FRAMEBUFFER_WIDTH);
    sceGuOffset(2048 - (PSP_GU_SCREEN_WIDTH / 2), 2048 - (PSP_GU_SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, PSP_GU_SCREEN_WIDTH, PSP_GU_SCREEN_HEIGHT);
    sceGuScissor(0, 0, PSP_GU_SCREEN_WIDTH, PSP_GU_SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDepthMask(GU_TRUE);
    sceGuPixelMask(0);

    listResult = sceGuFinish();
    if (listResult < 0) {
        psp_gfx_gu_device_log_failure("[gu] setup list finish failed");
        goto fail;
    }

    syncResult = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    if (syncResult < 0) {
        psp_gfx_gu_device_log_failure("[gu] setup list sync failed");
        goto fail;
    }

    if (sceDisplayWaitVblankStart() < 0) {
        psp_gfx_gu_device_log_failure("[gu] initial vblank wait failed");
        goto fail;
    }
    sceGuDisplay(GU_DISPLAY_ON);

    sReady = 1;
    return 1;

fail:
    PspGfxGuDevice_Shutdown();
    return 0;
}

int PspGfxGuDevice_IsReady(void) {
    return sReady;
}

int PspGfxGuDevice_BeginFrame(void) {
    const n64psp_display_config* display;

    if (!sReady || sFrameActive) {
        return 0;
    }

    if (sceGuStart(GU_DIRECT, sGuList) < 0) {
        psp_gfx_gu_device_log_failure("[gu] frame list start failed");
        return 0;
    }

    sFrameActive = 1;
    sFrameSubmitted = 0;
    display = PspDisplay_GetConfig();
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0, 0, display->framebuffer_width, display->framebuffer_height);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuDepthMask(GU_TRUE);
    sceGuPixelMask(0);
    sceGuClearDepth(0);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
    sceGuClearColor(PSP_GU_CLEAR_COLOR);
    psp_gfx_gu_device_clear_display_borders(display);
    sceGuScissor(0, 0, display->framebuffer_width, display->framebuffer_height);
    psp_gfx_gu_device_set_viewport(display);
    return 1;
}

int PspGfxGuDevice_Submit(void) {
    int listResult;
    int syncResult;

    if (!sReady || !sFrameActive) {
        return 0;
    }

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_FINISH_SYNC);
    listResult = sceGuFinish();
    sFrameActive = 0;
    if (listResult < 0) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_FINISH_SYNC);
        psp_gfx_gu_device_log_failure("[gu] frame list finish failed");
        return 0;
    }

    syncResult = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_FINISH_SYNC);
    if (syncResult < 0) {
        psp_gfx_gu_device_log_failure("[gu] frame list sync failed");
        return 0;
    }

    PspProfiler_CountSync();
    sFrameSubmitted = 1;
    return 1;
}

int PspGfxGuDevice_Present(void) {
    if (!sReady || sFrameActive || !sFrameSubmitted) {
        return 0;
    }

    sPresentedBufferOffset = sDrawBufferOffset;
    sDrawBufferOffset = (unsigned int) sceGuSwapBuffers();
    sFrameSubmitted = 0;
    return 1;
}

void PspGfxGuDevice_Shutdown(void) {
    if (sGuInitialized) {
        if (sFrameActive) {
            if (sceGuFinish() >= 0) {
                sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
            }
            sFrameActive = 0;
        }
        sceGuDisplay(GU_DISPLAY_OFF);
        sceGuTerm();
    }

    psp_gfx_gu_device_reset_state();
}

void* PspGfxGuDevice_GetPresentedFrameBuffer(int* stride, int* pixelFormat) {
    if (!sReady) {
        return (void*) 0;
    }

    if (stride != NULL) {
        *stride = PSP_GU_FRAMEBUFFER_WIDTH;
    }
    if (pixelFormat != NULL) {
        *pixelFormat = GU_PSM_5650;
    }

    return (void*) ((uintptr_t) sceGeEdramGetAddr() + (uintptr_t) sPresentedBufferOffset);
}
