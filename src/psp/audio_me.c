#include <pspkernel.h>
#include <pspintrman.h>
#include <pspthreadman.h>

#include "src/psp/audio_me.h"
#include "src/psp/audio_mixer.h"
#include "src/psp/audio_profile.h"

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
#define PSP_AUDIO_ME_CACHE_LINE_SIZE 64
#define PSP_AUDIO_ME_MAX_INPUT_RANGES 16
#define PSP_AUDIO_ME_MAX_WRITE_RANGES 512

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
    PSP_AUDIO_ME_SHARED_COMPLETION_ENABLED,
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
#define sMeCompletionEnabled PSP_AUDIO_ME_SHARED[PSP_AUDIO_ME_SHARED_COMPLETION_ENABLED]

typedef struct {
    void* address;
    u32 size;
} PspAudioMeCacheRange;

static const Acmd* sPendingCommands;
static s32 sPendingCommandCount;
static u32 sPendingStart;
static s32 sBootResult;
static s32 sBootStarted;
static s32 sInitialized;
static s32 sPending;
static s32 sLastError;
static SceUID sCompletionSema = -1;
static s32 sCompletionReady;
static PspAudioMeCacheRange sInputRanges[PSP_AUDIO_ME_MAX_INPUT_RANGES];
static u32 sInputRangeCount;
static s32 sInputRangeOverflow;
static PspAudioMeCacheRange sWriteRanges[PSP_AUDIO_ME_MAX_WRITE_RANGES];
static u32 sWriteRangeCount;
static s32 sWriteRangeOverflow;

static void psp_audio_me_completion_interrupt(int subIntr, void* arg) {
    (void) subIntr;
    (void) arg;
    if (sCompletionSema > 0) {
        sceKernelSignalSema(sCompletionSema, 1);
    }
}

static void psp_audio_me_drain_completion(void) {
    if (!sCompletionReady) {
        return;
    }
    while (sceKernelPollSema(sCompletionSema, 1) == 0) {
    }
}

static void psp_audio_me_enable_completion(void) {
    int result;

    if (sCompletionReady) {
        return;
    }
    sCompletionSema = sceKernelCreateSema("sf64_audio_me_done", 0, 0, 1, NULL);
    if (sCompletionSema <= 0) {
        sCompletionSema = -1;
        return;
    }
    result = sceKernelRegisterSubIntrHandler(PSP_MECODEC_INT, 0, psp_audio_me_completion_interrupt, NULL);
    if (result < 0) {
        sceKernelDeleteSema(sCompletionSema);
        sCompletionSema = -1;
        return;
    }
    result = sceKernelEnableSubIntr(PSP_MECODEC_INT, 0);
    if (result < 0) {
        sceKernelReleaseSubIntrHandler(PSP_MECODEC_INT, 0);
        sceKernelDeleteSema(sCompletionSema);
        sCompletionSema = -1;
        return;
    }
    sMeCompletionEnabled = 1;
    meLibSync();
    sCompletionReady = 1;
}

static u32 psp_audio_me_command_dma_size(u32 w0) {
    return ((w0 >> 16) & 0xFF) << 4;
}

static void psp_audio_me_reset_ranges(void) {
    sInputRangeCount = 0;
    sInputRangeOverflow = false;
    sWriteRangeCount = 0;
    sWriteRangeOverflow = false;
}

static void psp_audio_me_record_range(PspAudioMeCacheRange* ranges, u32* count, s32* overflow,
                                      u32 capacity, void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;
    u32 i;

    if ((address == NULL) || (size == 0) || *overflow) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    for (i = 0; i < *count; i++) {
        uintptr_t rangeStart = (uintptr_t) ranges[i].address;
        uintptr_t rangeEnd = rangeStart + ranges[i].size;

        if ((start <= rangeEnd) && (end >= rangeStart)) {
            if (start < rangeStart) {
                rangeStart = start;
            }
            if (end < rangeEnd) {
                end = rangeEnd;
            }
            ranges[i].address = (void*) rangeStart;
            ranges[i].size = end - rangeStart;
            return;
        }
    }
    if (*count >= capacity) {
        *overflow = true;
        return;
    }
    ranges[*count].address = (void*) start;
    ranges[*count].size = end - start;
    (*count)++;
}

static void psp_audio_me_record_input(const void* address, u32 size) {
    psp_audio_me_record_range(sInputRanges, &sInputRangeCount, &sInputRangeOverflow,
                              PSP_AUDIO_ME_MAX_INPUT_RANGES, (void*) address, size);
}

static void psp_audio_me_record_write(void* address, u32 size) {
    psp_audio_me_record_range(sWriteRanges, &sWriteRangeCount, &sWriteRangeOverflow,
                              PSP_AUDIO_ME_MAX_WRITE_RANGES, address, size);
}

static void psp_audio_me_collect_ranges(const Acmd* commands, s32 commandCount) {
    s32 i;

    psp_audio_me_reset_ranges();
    psp_audio_me_record_input(commands, commandCount * sizeof(*commands));
    for (i = 0; i < commandCount; i++) {
        u32 w0 = commands[i].words.w0;
        u32 w1 = commands[i].words.w1;

        switch (w0 >> 24) {
            case A_ADPCM:
            case A_RESAMPLE:
            case A_S8DEC:
                psp_audio_me_record_input((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                psp_audio_me_record_write((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            case A_FILTER:
                if (((w0 >> 16) & 0xFF) > A_INIT) {
                    psp_audio_me_record_input((void*) (uintptr_t) w1, 8 * sizeof(s16));
                } else {
                    psp_audio_me_record_input((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                    psp_audio_me_record_write((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                }
                break;
            case A_LOADADPCM:
                psp_audio_me_record_input((void*) (uintptr_t) w1, w0 & 0xFFFFFF);
                break;
            case A_LOADBUFF:
                psp_audio_me_record_input((void*) (uintptr_t) w1, psp_audio_me_command_dma_size(w0));
                break;
            case A_SAVEBUFF:
                psp_audio_me_record_input((void*) (uintptr_t) w1, psp_audio_me_command_dma_size(w0));
                psp_audio_me_record_write((void*) (uintptr_t) w1, psp_audio_me_command_dma_size(w0));
                break;
            case A_SETLOOP:
                psp_audio_me_record_input((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            default:
                break;
        }
    }
}

static void psp_audio_me_writeback_inputs(void) {
    u32 i;

    if (sInputRangeOverflow) {
        sceKernelDcacheWritebackAll();
        return;
    }
    for (i = 0; i < sInputRangeCount; i++) {
        sceKernelDcacheWritebackRange(sInputRanges[i].address, sInputRanges[i].size);
    }
}

static void psp_audio_me_invalidate_writes(void) {
    u32 i;

    if (sWriteRangeOverflow) {
        sceKernelDcacheWritebackInvalidateAll();
    } else {
        for (i = 0; i < sWriteRangeCount; i++) {
            sceKernelDcacheInvalidateRange(sWriteRanges[i].address, sWriteRanges[i].size);
        }
    }
    psp_audio_me_reset_ranges();
}

static void psp_audio_me_writeback_range_cpu(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    sceKernelDcacheWritebackRange((void*) start, end - start);
}

static void psp_audio_me_invalidate_range_cpu(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    sceKernelDcacheInvalidateRange((void*) start, end - start);
}

static void psp_audio_me_invalidate_range_me(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    meLibDcacheInvalidateRange((u32) start, end - start);
}

#if !defined(PSP_LOG_ENABLED) || !PSP_LOG_ENABLED
static void psp_audio_me_writeback_range_me(const void* address, u32 size) {
    uintptr_t start;
    uintptr_t end;

    if ((address == NULL) || (size == 0)) {
        return;
    }
    start = (uintptr_t) address & ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    end = ((uintptr_t) address + size + PSP_AUDIO_ME_CACHE_LINE_SIZE - 1) &
          ~(PSP_AUDIO_ME_CACHE_LINE_SIZE - 1);
    meLibDcacheWritebackRange((u32) start, end - start);
}
#endif

#if !defined(PSP_LOG_ENABLED) || !PSP_LOG_ENABLED
static void psp_audio_me_invalidate_inputs_me(const Acmd* commands, s32 commandCount) {
    s32 i;

    psp_audio_me_invalidate_range_me(commands, commandCount * sizeof(*commands));
    for (i = 0; i < commandCount; i++) {
        u32 w0 = commands[i].words.w0;
        u32 w1 = commands[i].words.w1;

        switch (w0 >> 24) {
            case A_ADPCM:
            case A_RESAMPLE:
            case A_S8DEC:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            case A_FILTER:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1,
                                                  ((w0 >> 16) & 0xFF) > A_INIT ?
                                                      8 * sizeof(s16) : sizeof(ADPCM_STATE));
                break;
            case A_LOADADPCM:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1, w0 & 0xFFFFFF);
                break;
            case A_LOADBUFF:
            case A_SAVEBUFF:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1,
                                                  psp_audio_me_command_dma_size(w0));
                break;
            case A_SETLOOP:
                psp_audio_me_invalidate_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            default:
                break;
        }
    }
}

static void psp_audio_me_writeback_outputs_me(const Acmd* commands, s32 commandCount) {
    s32 i;

    for (i = 0; i < commandCount; i++) {
        u32 w0 = commands[i].words.w0;
        u32 w1 = commands[i].words.w1;

        switch (w0 >> 24) {
            case A_ADPCM:
            case A_RESAMPLE:
            case A_S8DEC:
                psp_audio_me_writeback_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                break;
            case A_FILTER:
                if (((w0 >> 16) & 0xFF) <= A_INIT) {
                    psp_audio_me_writeback_range_me((void*) (uintptr_t) w1, sizeof(ADPCM_STATE));
                }
                break;
            case A_SAVEBUFF:
                psp_audio_me_writeback_range_me((void*) (uintptr_t) w1,
                                                 psp_audio_me_command_dma_size(w0));
                break;
            default:
                break;
        }
    }
    psp_audio_me_writeback_range_me(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
}
#endif

__attribute__((noinline, aligned(4))) void meLibOnException(void) {
    sMeState = PSP_AUDIO_ME_FAULT;
    meLibSync();
    if (sMeCompletionEnabled) {
        meLibSendExternalSoftInterrupt();
    }
    meLibHalt();
}

__attribute__((noinline, aligned(4))) void meLibOnExternalInterrupt(void) {
    sMeState = PSP_AUDIO_ME_FAULT;
    meLibSync();
    if (sMeCompletionEnabled) {
        meLibSendExternalSoftInterrupt();
    }
    meLibHalt();
}

__attribute__((noinline, aligned(4))) void meLibOnProcess(void) {
    sMeProgress = 1;
    meLibSync();

    while (sMeState == PSP_AUDIO_ME_BOOTING) {
        meLibDelayPipeline();
    }

    psp_audio_me_invalidate_range_me(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
    sMeProgress = PSP_AUDIO_ME_READY;
    meLibSync();

    while (sMeState != PSP_AUDIO_ME_STOP) {
        if (sMeState == PSP_AUDIO_ME_RUN) {
            const Acmd* commands = (const Acmd*) (uintptr_t) sMeCommands;
            s32 commandCount = (s32) sMeCommandCount;

#if defined(PSP_LOG_ENABLED) && PSP_LOG_ENABLED
            meLibDcacheWritebackInvalidateAll();
#else
            psp_audio_me_invalidate_inputs_me(commands, commandCount);
#endif
            sMeResult = PspAudioMixer_ExecuteCommandListMe(commands, commandCount);
            if ((sMeResult == 0) && !PspAudioMixer_ValidateState()) {
                sMeResult = -3;
            }
#if defined(PSP_LOG_ENABLED) && PSP_LOG_ENABLED
            meLibDcacheWritebackInvalidateAll();
#else
            psp_audio_me_writeback_outputs_me(commands, commandCount);
#endif
            meLibSync();
            sMeState = PSP_AUDIO_ME_IDLE;
            meLibSync();
            if (sMeCompletionEnabled) {
                meLibSendExternalSoftInterrupt();
            }
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
    sMeCompletionEnabled = 0;
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

    psp_audio_me_enable_completion();
    psp_audio_me_writeback_range_cpu(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
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

static void psp_audio_me_wait(PspAudioProfileWaitReason reason) {
#if PSP_AUDIO_PROFILE
    u32 waitStart;
    s32 blocked;
#endif

    if (!sPending) {
        return;
    }

#if PSP_AUDIO_PROFILE
    blocked = psp_audio_me_is_running();
    waitStart = sceKernelGetSystemTimeLow();
#endif

    while (psp_audio_me_is_running()) {
        if (sCompletionReady) {
            u32 elapsed = sceKernelGetSystemTimeLow() - sPendingStart;
            SceUInt timeout;

            if (elapsed >= PSP_AUDIO_ME_TIMEOUT_US) {
                break;
            }
            timeout = PSP_AUDIO_ME_TIMEOUT_US - elapsed;
            sceKernelWaitSema(sCompletionSema, 1, &timeout);
            continue;
        }
        if ((sceKernelGetSystemTimeLow() - sPendingStart) >= PSP_AUDIO_ME_TIMEOUT_US) {
            break;
        }
        sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
    }

    if (psp_audio_me_is_running()) {
        u32 interruptStart;

        meLibEmitSoftwareInterrupt();
        interruptStart = sceKernelGetSystemTimeLow();
        while (psp_audio_me_is_running() &&
               ((sceKernelGetSystemTimeLow() - interruptStart) < PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US)) {
            if (sCompletionReady) {
                SceUInt timeout = PSP_AUDIO_ME_INTERRUPT_TIMEOUT_US;

                sceKernelWaitSema(sCompletionSema, 1, &timeout);
            } else {
                sceKernelDelayThread(PSP_AUDIO_ME_POLL_US);
            }
        }
    }

    PspAudioProfile_RecordCompletion(sceKernelGetSystemTimeLow() - sPendingStart);
    if (sMeState == PSP_AUDIO_ME_IDLE) {
        psp_audio_me_invalidate_writes();
        sLastError = (s32) sMeResult;
        if (sLastError < 0) {
            psp_audio_me_invalidate_range_cpu(PspAudioMixer_GetStateAddress(), PspAudioMixer_GetStateSize());
        }
    } else if (sMeState == PSP_AUDIO_ME_FAULT) {
        sceKernelDcacheWritebackInvalidateAll();
        PspAudioProfile_RecordFallback();
        sLastError = PspAudioMixer_ExecuteCommandList(sPendingCommands, sPendingCommandCount);
        if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
            sLastError = -3;
        }
        psp_audio_me_reset_ranges();
        sInitialized = 0;
    } else {
        sLastError = -2;
        psp_audio_me_reset_ranges();
        sInitialized = 0;
    }

    if (sLastError < 0) {
        sInitialized = 0;
    }
    sPending = 0;
#if PSP_AUDIO_PROFILE
    PspAudioProfile_RecordWait(reason, blocked,
                               sceKernelGetSystemTimeLow() - waitStart);
    PspAudioProfile_Report();
#endif
}

void PspAudioMe_Wait(void) {
    psp_audio_me_wait(PSP_AUDIO_PROFILE_WAIT_PUBLIC);
}

void PspAudioMe_Submit(const Acmd* commands, s32 commandCount) {
    if ((commands == NULL) || (commandCount <= 0)) {
        return;
    }

    psp_audio_me_wait(PSP_AUDIO_PROFILE_WAIT_SUBMIT);
    if (!sInitialized || (sMeState != PSP_AUDIO_ME_IDLE)) {
        PspAudioProfile_RecordFallback();
        sLastError = PspAudioMixer_ExecuteCommandList(commands, commandCount);
        if ((sLastError == 0) && !PspAudioMixer_ValidateState()) {
            sLastError = -3;
        }
        return;
    }

    psp_audio_me_collect_ranges(commands, commandCount);
    psp_audio_me_writeback_inputs();
    psp_audio_me_drain_completion();
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
