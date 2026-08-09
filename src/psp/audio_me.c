#include <pspkernel.h>

#include "src/psp/audio_me.h"
#include "src/psp/audio_mixer.h"

#ifndef PSP_AUDIO
#define PSP_AUDIO 0
#endif

#if PSP_AUDIO
#include <me-core-mapper/me-core.h>

#define PSP_AUDIO_ME_TIMEOUT_US 250000
#define PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US 10000
#define PSP_AUDIO_ME_POLL_US 100
#define PSP_AUDIO_ME_READY 0x100
#define PSP_AUDIO_ME_UNCACHED 0x40000000

typedef enum {
    PSP_AUDIO_ME_BOOTING,
    PSP_AUDIO_ME_IDLE,
    PSP_AUDIO_ME_RUN,
    PSP_AUDIO_ME_STOP,
    PSP_AUDIO_ME_HALTED,
    PSP_AUDIO_ME_FAULT,
} PspAudioMeState;

enum {
    PSP_AUDIO_ME_SHARED_STATE,
    PSP_AUDIO_ME_SHARED_COMMANDS,
    PSP_AUDIO_ME_SHARED_COMMAND_COUNT,
    PSP_AUDIO_ME_SHARED_RESULT,
    PSP_AUDIO_ME_SHARED_PROGRESS,
    PSP_AUDIO_ME_SHARED_COUNT,
};

static volatile u32 sSharedStorage[PSP_AUDIO_ME_SHARED_COUNT]
    __attribute__((aligned(64), section(".uncached")));

#define PSP_AUDIO_ME_SHARED \
    ((volatile u32*) (PSP_AUDIO_ME_UNCACHED | (u32) (uintptr_t) sSharedStorage))
#define sMeState PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_STATE]
#define sMeCommands PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_COMMANDS]
#define sMeCommandCount PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_COMMAND_COUNT]
#define sMeResult PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_RESULT]
#define sMeProgress PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_PROGRESS]

static const Acmd* sPendingCommands;
static s32 sPendingCommandCount;
static u32 sPendingStart;
static s32 sBootResult;
static s32 sBootStarted;
static s32 sInitialized;
static s32 sPending;
static s32 sLastError;

__attribute__((noinline, aligned(4))) void meLibOnException(void) {
    sMeState = PSP_AUDIO_ME_FAULT;
    meLibSync();
    meLibHalt();
}

__attribute__((noinline, aligned(4))) void meLibOnExternalInterrupt(void) {
    sMeState = PSP_AUDIO_ME_FAULT;
    meLibSync();
    meLibHalt();
}

__attribute__((noinline, aligned(4))) void meLibOnProcess(void) {
    sMeProgress = 1;
    meLibSync();

    while (sMeState == PSP_AUDIO_ME_BOOTING) {
        meLibDelayPipeline();
    }

    sMeProgress = PSP_AUDIO_ME_READY;
    meLibSync();

    while (sMeState != PSP_AUDIO_ME_STOP) {
        if (sMeState == PSP_AUDIO_ME_RUN) {
            const Acmd* commands = (const Acmd*) (uintptr_t) sMeCommands;
            s32 commandCount = (s32) sMeCommandCount;

            meLibDcacheWritebackInvalidateAll();
            sMeResult = PspAudioMixer_ExecuteCommandList(commands, commandCount);
            meLibDcacheWritebackInvalidateAll();
            meLibSync();
            sMeState = PSP_AUDIO_ME_IDLE;
            meLibSync();
        } else {
            meLibDelayPipeline();
        }
    }

    sMeState = PSP_AUDIO_ME_HALTED;
    meLibSync();
    meLibHalt();
}

int PspAudioMe_Boot(void) {
    if (sBootStarted) {
        return sBootResult;
    }

    sMeCommands = 0;
    sMeCommandCount = 0;
    sMeResult = 0;
    sMeProgress = 0;
    sMeState = PSP_AUDIO_ME_BOOTING;
    meLibSync();

    sBootStarted = 1;
    sBootResult = meLibDefaultInit();
    if (sBootResult < 0) {
        sLastError = sBootResult;
    }
    return sBootResult;
}

int PspAudioMe_Init(void) {
    u32 start;
    int result;

    if (sInitialized) {
        return 0;
    }

    result = PspAudioMe_Boot();
    if (result < 0) {
        return result;
    }

    sMeState = PSP_AUDIO_ME_IDLE;
    meLibSync();
    start = sceKernelGetSystemTimeLow();
    while (sMeProgress != PSP_AUDIO_ME_READY) {
        if ((sceKernelGetSystemTimeLow() - start) >= PSP_AUDIO_ME_TIMEOUT_US) {
            sMeState = PSP_AUDIO_ME_STOP;
            meLibSync();
            sLastError = -1;
            return sLastError;
        }
        sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
    }

    sInitialized = 1;
    return 0;
}

static s32 psp_audio_me_is_running(void) {
    return sMeState == PSP_AUDIO_ME_RUN;
}

void PspAudioMe_Wait(void) {
    if (!sPending) {
        return;
    }

    while (psp_audio_me_is_running()) {
        if ((sceKernelGetSystemTimeLow() - sPendingStart) >= PSP_AUDIO_ME_TIMEOUT_US) {
            u32 interruptStart;

            meLibEmitSoftwareInterrupt();
            interruptStart = sceKernelGetSystemTimeLow();
            while (psp_audio_me_is_running() &&
                   ((sceKernelGetSystemTimeLow() - interruptStart) < PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US)) {
                sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
            }
            break;
        }
        sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
    }

    if (sMeState == PSP_AUDIO_ME_IDLE) {
        sceKernelDcacheWritebackInvalidateAll();
        sLastError = (s32) sMeResult;
    } else if (sMeState == PSP_AUDIO_ME_FAULT) {
        sceKernelDcacheWritebackInvalidateAll();
        sLastError = PspAudioMixer_ExecuteCommandList(sPendingCommands, sPendingCommandCount);
        sInitialized = 0;
    } else {
        sLastError = -2;
        sInitialized = 0;
    }

    if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
        sLastError = -3;
    }
    if (sLastError < 0) {
        sInitialized = 0;
    }
    sPending = 0;
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    if ((commands == NULL) || (commandCount <= 0)) {
        return;
    }

    PspAudioMe_Wait();
    if (!sInitialized || (sMeState != PSP_AUDIO_ME_IDLE)) {
        sLastError = PspAudioMixer_ExecuteCommandList(commands, commandCount);
        if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
            sLastError = -3;
        }
        return;
    }

    sceKernelDcacheWritebackInvalidateAll();
    sMeCommands = (u32) (uintptr_t) commands;
    sMeCommandCount = commandCount;
    sMeResult = 0;
    sPendingCommands = commands;
    sPendingCommandCount = commandCount;
    sPendingStart = sceKernelGetSystemTimeLow();
    sPending = 1;
    meLibSync();
    sMeState = PSP_AUDIO_ME_RUN;
    meLibSync();
}

int PspAudioMe_IsActive(void) {
    return sInitialized;
}

int PspAudioMe_GetLastError(void) {
    return sLastError;
}

#else

int PspAudioMe_Boot(void) {
    return 0;
}

int PspAudioMe_Init(void) {
    return 0;
}

void PspAudioMe_Wait(void) {
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    (void) commands;
    (void) commandCount;
}

int PspAudioMe_IsActive(void) {
    return 0;
}

int PspAudioMe_GetLastError(void) {
    return 0;
}

#endif
