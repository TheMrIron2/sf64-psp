#include <pspaudio.h>
#include <pspkernel.h>
#include <pspthreadman.h>

#include "src/psp/audio_output.h"
#include "src/psp/platform.h"

#define PSP_AUDIO_RATE 32000
#define PSP_AUDIO_CHANNELS 2
#define PSP_AUDIO_BLOCK_COUNT 8
#define PSP_AUDIO_MAX_FRAMES 1024
#define PSP_AUDIO_BYTES_PER_FRAME (PSP_AUDIO_CHANNELS * sizeof(short))

#ifndef PSP_AUDIO_OUTPUT
#define PSP_AUDIO_OUTPUT 1
#endif

typedef struct {
    unsigned int frames;
    short samples[PSP_AUDIO_MAX_FRAMES * PSP_AUDIO_CHANNELS];
} PspAudioBlock;

static PspAudioBlock sBlocks[PSP_AUDIO_BLOCK_COUNT] __attribute__((aligned(64)));
static volatile unsigned int sReadIndex;
static volatile unsigned int sWriteIndex;
static volatile unsigned int sQueuedBlocks;
static volatile unsigned int sQueuedBytes;
static volatile unsigned int sSubmittedBlocks;
static volatile unsigned int sOverruns;
static SceUID sMutex = -1;
static SceUID sReady = -1;
static SceUID sThread = -1;

static void psp_audio_copy(void* dst, const void* src, unsigned int size) {
    unsigned char* out = dst;
    const unsigned char* in = src;

    while (size-- != 0) {
        *out++ = *in++;
    }
}

static int psp_audio_output_thread(SceSize args, void* argp) {
    (void) args;
    (void) argp;

    while (1) {
        PspAudioBlock* block;
        unsigned int blockBytes;

        sceKernelWaitSema(sReady, 1, NULL);
        sceKernelWaitSema(sMutex, 1, NULL);
        if (sQueuedBlocks == 0) {
            sceKernelSignalSema(sMutex, 1);
            continue;
        }

        block = &sBlocks[sReadIndex];
        blockBytes = block->frames * PSP_AUDIO_BYTES_PER_FRAME;
        sReadIndex = (sReadIndex + 1) % PSP_AUDIO_BLOCK_COUNT;
        sQueuedBlocks--;
        sceKernelSignalSema(sMutex, 1);

        sceAudioOutput2ChangeLength(block->frames);
        sceKernelDcacheWritebackInvalidateRange(block->samples, blockBytes);
        sceAudioOutput2OutputBlocking(PSP_AUDIO_VOLUME_MAX, block->samples);

        sceKernelWaitSema(sMutex, 1, NULL);
        sQueuedBytes = sQueuedBytes >= blockBytes ? sQueuedBytes - blockBytes : 0;
        sceKernelSignalSema(sMutex, 1);
    }

    return 0;
}

int PspAudioOutput_Init(void) {
#if !PSP_AUDIO_OUTPUT
    return 0;
#else
    int result;

    if (sThread >= 0) {
        return 0;
    }

    sMutex = sceKernelCreateSema("sf64_audio_mutex", 0, 1, 1, NULL);
    sReady = sceKernelCreateSema("sf64_audio_ready", 0, 0, PSP_AUDIO_BLOCK_COUNT, NULL);
    if ((sMutex < 0) || (sReady < 0)) {
        return -1;
    }

    result = sceAudioSRCChReserve(PSP_AUDIO_MAX_FRAMES, PSP_AUDIO_RATE, PSP_AUDIO_CHANNELS);
    if (result < 0) {
        return result;
    }

    sThread = sceKernelCreateThread(
        "sf64_audio_output",
        psp_audio_output_thread,
        0x18,
        0x2000,
        0,
        NULL
    );
    if (sThread < 0) {
        return sThread;
    }

    return sceKernelStartThread(sThread, 0, NULL);
#endif
}

int PspAudioOutput_Submit(const void* samples, unsigned int size) {
#if !PSP_AUDIO_OUTPUT
    (void) samples;
    (void) size;
    return 0;
#else
    PspAudioBlock* block;
    unsigned int frames;

    if ((samples == NULL) || (size == 0) || (sThread < 0)) {
        return -1;
    }

    frames = size / PSP_AUDIO_BYTES_PER_FRAME;
    if ((frames == 0) || (frames > PSP_AUDIO_MAX_FRAMES)) {
        return -1;
    }

    sceKernelWaitSema(sMutex, 1, NULL);

    if (sQueuedBlocks == PSP_AUDIO_BLOCK_COUNT) {
        sOverruns++;
        sceKernelSignalSema(sMutex, 1);

#if PSP_LOG_ENABLED
        if ((sOverruns <= 4) || ((sOverruns & (sOverruns - 1)) == 0)) {
            PspPlatform_LogFrame("audio output overrun", sOverruns);
        }
#endif

        return -1;
    }

    block = &sBlocks[sWriteIndex];
    block->frames = frames;
    psp_audio_copy(block->samples, samples, frames * PSP_AUDIO_BYTES_PER_FRAME);

    sWriteIndex = (sWriteIndex + 1) % PSP_AUDIO_BLOCK_COUNT;
    sQueuedBlocks++;
    sQueuedBytes += frames * PSP_AUDIO_BYTES_PER_FRAME;
    sSubmittedBlocks++;

    sceKernelSignalSema(sMutex, 1);
    sceKernelSignalSema(sReady, 1);

#if PSP_LOG_ENABLED
    if (sSubmittedBlocks == 1) {
        PspPlatform_LogValue("audio first PCM block frames", frames);
    }
#endif

    return 0;
#endif
}

unsigned int PspAudioOutput_GetQueuedBytes(void) {
#if !PSP_AUDIO_OUTPUT
    return 0;
#else
    return sQueuedBytes;
#endif
}
