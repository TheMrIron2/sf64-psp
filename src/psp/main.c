#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>

#ifdef PSP_FULL
#if PSP_AUDIO
#include "src/psp/audio_me.h"
#endif
#include "src/psp/settings.h"
#include "src/psp/gfx/gfx_psp_backend.h"
#include "src/psp/hw_counter_profile.h"
#include "src/psp/n64psp_integration.h"
#include "src/psp/platform.h"
#include "src/psp/eeprom_store.h"
#include "src/psp/profiler.h"

void bootproc(void);
#endif

PSP_MODULE_INFO("Star Fox 64", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
// Leave memory for wireless networking
PSP_HEAP_SIZE_KB(-2048);

int exit_callback(int arg1, int arg2, void* common) {
    (void) arg1;
    (void) arg2;
    (void) common;

#ifdef PSP_FULL
    PspProfiler_RequestExit();
#else
    sceKernelExitGame();
#endif
    return 0;
}

int callback_thread(SceSize args, void* argp) {
    int callbackId;

    (void) args;
    (void) argp;

    callbackId = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(callbackId);
    sceKernelSleepThreadCB();

    return 0;
}

void setup_callbacks(void) {
    SceUID threadId;

    threadId = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (threadId >= 0) {
        sceKernelStartThread(threadId, 0, 0);
    }
}

int main(int argc, char* argv[]) {
#ifndef PSP_FULL
    SceCtrlData pad;
#else
    PspGfxBackendKind buildBackend;
#endif

    (void) argc;
    (void) argv;

    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    
    scePowerSetClockFrequency(333, 333, 166);

#ifdef PSP_FULL
    buildBackend = PspGfxBackend_GetBuildDefault();
    PspEeprom_SetExecutablePath(argc > 0 ? argv[0] : NULL);
    PspSettings_Init(argc > 0 ? argv[0] : NULL, buildBackend);
    if (!PspGfxBackend_Select(PspSettings_GetGraphicsBackend())) {
        PspGfxBackend_Select(buildBackend);
    }
    PspSettings_SetActiveGraphicsBackend(PspGfxBackend_GetActive());

    pspDebugScreenPrintf("Star Fox 64\n");
    pspDebugScreenPrintf("Booting native game loop...\n\n");
    pspDebugScreenPrintf("SELECT opens settings   HOME opens system menu\n");
    pspDebugScreenPrintf("Graphics backend: %s\n", PspGfxBackend_GetName());
#if PROFILE_GPROF || PROFILE_PHASES || PROFILE_HW_COUNTERS
    pspDebugScreenPrintf("Hold D-pad Up+L to capture, Up+R to save\n");
#endif

#if PSP_AUDIO
    (void) PspAudioMe_Boot();
#endif

    if (!PspN64psp_Init()) {
        pspDebugScreenPrintf("[psp] n64psp init failed; boot aborted\n");
        while (1) {
            sceDisplayWaitVblankStart();
        }
    }

    pspDebugScreenPrintf("[psp] platform init\n");
    PspPlatform_Init();
    PspHwCounterProfile_Init();
    pspDebugScreenPrintf("[psp] bootproc enter\n");
    bootproc();
    pspDebugScreenPrintf("[psp] bootproc returned\n");
#if !PROFILE_GPROF
    PspProfiler_Shutdown();
#endif
    PspHwCounterProfile_Shutdown();

    while (1) {
        sceDisplayWaitVblankStart();
        if (PspProfiler_ExitRequested()) {
            PspProfiler_Shutdown();
            PspHwCounterProfile_Shutdown();
            sceKernelExitGame();
        }
    }
#else
    pspDebugScreenPrintf("Star Fox 64\n");
    pspDebugScreenPrintf("PSP build pipeline OK\n\n");
    pspDebugScreenPrintf("Press X to exit\n");

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        if (pad.Buttons & PSP_CTRL_CROSS) {
            break;
        }
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
#endif
    return 0;
}
