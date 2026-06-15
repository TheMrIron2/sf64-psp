#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>

#ifdef PSP_FULL
#include "src/psp/platform.h"

void bootproc(void);
#endif

PSP_MODULE_INFO("Star Fox 64 PSP", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

int exit_callback(int arg1, int arg2, void* common) {
    (void) arg1;
    (void) arg2;
    (void) common;

    sceKernelExitGame();
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
#endif

    (void) argc;
    (void) argv;

    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

#ifdef PSP_FULL
    pspDebugScreenPrintf("Star Fox 64 PSP\n");
    pspDebugScreenPrintf("Booting native game loop...\n\n");
    pspDebugScreenPrintf("Select+Start exits\n");

    pspDebugScreenPrintf("[psp] platform init\n");
    PspPlatform_Init();
    pspDebugScreenPrintf("[psp] bootproc enter\n");
    bootproc();
    pspDebugScreenPrintf("[psp] bootproc returned\n");

    while (1) {
        sceDisplayWaitVblankStart();
    }
#else
    pspDebugScreenPrintf("Star Fox 64 PSP\n");
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
