#include <pspaudio.h>
#include <pspkernel.h>
#include <pspthreadman.h>

#include "src/psp/audio_output.h"
#include "src/psp/platform.h"

#define PSP_AUDIO_RATE 32000
#define PSP_AUDIO_CHANNELS 2
#define PSP_AUDIO_BLOCK_COUNT 8
#define PSP_AUDIO_QUEUE_TARGET 3
#define PSP_AUDIO_MAX_FRAMES 1152
#define PSP_AUDIO_BYTES_PER_FRAME (PSP_AUDIO_CHANNELS * sizeof(short))
#define PSP_AUDIO_SUMMARY_INTERVAL 256

#ifndef PSP_AUDIO_OUTPUT
#define PSP_AUDIO_OUTPUT 1
#endif
#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif

#if PSP_AUDIO_OUTPUT
typedef struct {
    unsigned int frames;
    short samples[PSP_AUDIO_MAX_FRAMES * PSP_AUDIO_CHANNELS];
} PspAudioBlock;

typedef char PspAudioBlockStorageCheck[
    sizeof(((PspAudioBlock*) 0)->samples) >= (PSP_AUDIO_MAX_FRAMES * PSP_AUDIO_BYTES_PER_FRAME) ? 1 : -1
];

static PspAudioBlock sBlocks[PSP_AUDIO_BLOCK_COUNT] __attribute__((aligned(64)));
static volatile unsigned int sReadIndex;
static volatile unsigned int sWriteIndex;
static volatile unsigned int sQueuedBlocks;
static volatile unsigned int sQueuedBytes;
static volatile unsigned int sSubmittedBlocks;
static volatile unsigned int sSubmittedFrames;
static volatile unsigned int sRejectedSubmissions;
static volatile unsigned int sRejectedOversize;
static volatile unsigned int sOverruns;
static volatile unsigned int sStarvations;
static volatile unsigned int sOutputErrors;
static volatile unsigned int sMinSubmittedFrames;
static volatile unsigned int sMaxSubmittedFrames;
static volatile unsigned int sPeakQueuedBlocks;
static volatile unsigned int sSilentBlocks;
static volatile unsigned int sNonzeroSamples;
static volatile unsigned int sPeakSampleMagnitude;
static volatile int sLastOutputError;
static SceUID sMutex = -1;
static SceUID sReady = -1;
static SceUID sSpace = -1;
static SceUID sThread = -1;
static int sChannelReserved;

#if PSP_LOG_ENABLED
static char* psp_audio_append_text(char* out, const char* text) {
    while (*text != '\0') {
        *out++ = *text++;
    }
    return out;
}

static char* psp_audio_append_u32(char* out, unsigned int value) {
    char digits[10];
    int count = 0;

    if (value == 0) {
        *out++ = '0';
        return out;
    }

    while (value != 0) {
        digits[count++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (count > 0) {
        *out++ = digits[--count];
    }
    return out;
}

static char* psp_audio_append_s32(char* out, int value) {
    unsigned int magnitude;

    if (value < 0) {
        *out++ = '-';
        magnitude = (unsigned int) (-(value + 1)) + 1;
    } else {
        magnitude = (unsigned int) value;
    }
    return psp_audio_append_u32(out, magnitude);
}

static int psp_audio_should_log_error(unsigned int count) {
    return (count <= 4) || ((count & (count - 1)) == 0);
}

static void psp_audio_log_summary(void) {
    char line[288];
    char* out = line;

    sceKernelWaitSema(sMutex, 1, NULL);
    out = psp_audio_append_text(out, "[psp-audio] blocks=");
    out = psp_audio_append_u32(out, sSubmittedBlocks);
    out = psp_audio_append_text(out, " frames=");
    out = psp_audio_append_u32(out, sSubmittedFrames);
    out = psp_audio_append_text(out, " rejected=");
    out = psp_audio_append_u32(out, sRejectedSubmissions);
    out = psp_audio_append_text(out, " oversize=");
    out = psp_audio_append_u32(out, sRejectedOversize);
    out = psp_audio_append_text(out, " overruns=");
    out = psp_audio_append_u32(out, sOverruns);
    out = psp_audio_append_text(out, " starved=");
    out = psp_audio_append_u32(out, sStarvations);
    out = psp_audio_append_text(out, " errors=");
    out = psp_audio_append_u32(out, sOutputErrors);
    out = psp_audio_append_text(out, " min=");
    out = psp_audio_append_u32(out, sMinSubmittedFrames);
    out = psp_audio_append_text(out, " max=");
    out = psp_audio_append_u32(out, sMaxSubmittedFrames);
    out = psp_audio_append_text(out, " queued=");
    out = psp_audio_append_u32(out, sQueuedBlocks);
    out = psp_audio_append_text(out, " bytes=");
    out = psp_audio_append_u32(out, sQueuedBytes);
    out = psp_audio_append_text(out, " peak=");
    out = psp_audio_append_u32(out, sPeakQueuedBlocks);
    out = psp_audio_append_text(out, " silent=");
    out = psp_audio_append_u32(out, sSilentBlocks);
    out = psp_audio_append_text(out, " nonzero=");
    out = psp_audio_append_u32(out, sNonzeroSamples);
    out = psp_audio_append_text(out, " samplePeak=");
    out = psp_audio_append_u32(out, sPeakSampleMagnitude);
    out = psp_audio_append_text(out, " last=");
    out = psp_audio_append_s32(out, sLastOutputError);
    *out = '\0';
    sceKernelSignalSema(sMutex, 1);

    PspPlatform_LogLine(line);
}
#endif

static void psp_audio_copy(void* dst, const void* src, unsigned int size) {
    unsigned char* out = dst;
    const unsigned char* in = src;

    while (size-- != 0) {
        *out++ = *in++;
    }
}

static void psp_audio_reset_state(void) {
    sReadIndex = 0;
    sWriteIndex = 0;
    sQueuedBlocks = 0;
    sQueuedBytes = 0;
    sSubmittedBlocks = 0;
    sSubmittedFrames = 0;
    sRejectedSubmissions = 0;
    sRejectedOversize = 0;
    sOverruns = 0;
    sStarvations = 0;
    sOutputErrors = 0;
    sMinSubmittedFrames = 0;
    sMaxSubmittedFrames = 0;
    sPeakQueuedBlocks = 0;
    sSilentBlocks = 0;
    sNonzeroSamples = 0;
    sPeakSampleMagnitude = 0;
    sLastOutputError = 0;
}

static void psp_audio_cleanup_init(void) {
    if (sThread >= 0) {
        sceKernelDeleteThread(sThread);
        sThread = -1;
    }
    if (sChannelReserved) {
        sceAudioSRCChRelease();
        sChannelReserved = 0;
    }
    if (sReady >= 0) {
        sceKernelDeleteSema(sReady);
        sReady = -1;
    }
    if (sSpace >= 0) {
        sceKernelDeleteSema(sSpace);
        sSpace = -1;
    }
    if (sMutex >= 0) {
        sceKernelDeleteSema(sMutex);
        sMutex = -1;
    }
}

static unsigned int psp_audio_record_rejection(int oversize, unsigned int* oversizeCount) {
    unsigned int count;

    if (sMutex >= 0) {
        sceKernelWaitSema(sMutex, 1, NULL);
    }
    sRejectedSubmissions++;
    if (oversize) {
        sRejectedOversize++;
    }
    count = sRejectedSubmissions;
    if (oversizeCount != NULL) {
        *oversizeCount = sRejectedOversize;
    }
    if (sMutex >= 0) {
        sceKernelSignalSema(sMutex, 1);
    }
    return count;
}

static int psp_audio_output_thread(SceSize args, void* argp) {
    (void) args;
    (void) argp;

    while (1) {
        PspAudioBlock* block;
        unsigned int blockBytes;
        unsigned int outputErrorCount = 0;
        int outputResult;

        sceKernelWaitSema(sReady, 1, NULL);
        sceKernelWaitSema(sMutex, 1, NULL);
        if (sQueuedBlocks == 0) {
            sceKernelSignalSema(sMutex, 1);
            continue;
        }

        block = &sBlocks[sReadIndex];
        blockBytes = block->frames * PSP_AUDIO_BYTES_PER_FRAME;
        sceKernelSignalSema(sMutex, 1);

        sceKernelDcacheWritebackInvalidateRange(block->samples, blockBytes);
        outputResult = sceAudioOutput2ChangeLength(block->frames);
        if (outputResult >= 0) {
            outputResult = sceAudioOutput2OutputBlocking(PSP_AUDIO_VOLUME_MAX, block->samples);
        }

        sceKernelWaitSema(sMutex, 1, NULL);
        if (sQueuedBlocks == 1) {
            sStarvations++;
        }
        sReadIndex = (sReadIndex + 1) % PSP_AUDIO_BLOCK_COUNT;
        sQueuedBlocks--;
        sQueuedBytes = sQueuedBytes >= blockBytes ? sQueuedBytes - blockBytes : 0;
        if (outputResult < 0) {
            sOutputErrors++;
            sLastOutputError = outputResult;
            outputErrorCount = sOutputErrors;
        }
        sceKernelSignalSema(sMutex, 1);
        sceKernelSignalSema(sSpace, 1);

#if PSP_LOG_ENABLED
        if ((outputErrorCount != 0) && psp_audio_should_log_error(outputErrorCount)) {
            PspPlatform_LogValue("audio output errors", outputErrorCount);
            PspPlatform_LogValue("audio last output error", (unsigned int) outputResult);
        }
#else
        (void) outputErrorCount;
#endif
    }

    return 0;
}
#endif

int PspAudioOutput_Init(void) {
#if !PSP_AUDIO_OUTPUT
    return 0;
#else
    int result;

    if (sThread >= 0) {
        return 0;
    }

    psp_audio_reset_state();
    sMutex = sceKernelCreateSema("sf64_audio_mutex", 0, 1, 1, NULL);
    sReady = sceKernelCreateSema("sf64_audio_ready", 0, 0, PSP_AUDIO_BLOCK_COUNT, NULL);
    sSpace = sceKernelCreateSema("sf64_audio_space", 0, PSP_AUDIO_QUEUE_TARGET, PSP_AUDIO_QUEUE_TARGET, NULL);
    if ((sMutex < 0) || (sReady < 0) || (sSpace < 0)) {
        psp_audio_cleanup_init();
        return -1;
    }

    result = sceAudioSRCChReserve(PSP_AUDIO_MAX_FRAMES, PSP_AUDIO_RATE, PSP_AUDIO_CHANNELS);
    if (result < 0) {
        psp_audio_cleanup_init();
        return result;
    }
    sChannelReserved = 1;

    sThread = sceKernelCreateThread(
        "sf64_audio_output",
        psp_audio_output_thread,
        0x18,
        0x2000,
        0,
        NULL
    );
    if (sThread < 0) {
        result = sThread;
        sThread = -1;
        psp_audio_cleanup_init();
        return result;
    }

    result = sceKernelStartThread(sThread, 0, NULL);
    if (result < 0) {
        psp_audio_cleanup_init();
        return result;
    }
    return 0;
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
    unsigned int rejectedCount;
    unsigned int oversizeCount;
    unsigned int submittedBlocks;
    unsigned int nonzeroSamples = 0;
    unsigned int peakSampleMagnitude = 0;
    unsigned int firstSignalBlock = 0;
    const short* inputSamples;
    unsigned int sampleCount;
    unsigned int i;

    (void) rejectedCount;
    (void) oversizeCount;
    (void) submittedBlocks;
    (void) firstSignalBlock;

    if ((samples == NULL) || (size == 0) || ((size % PSP_AUDIO_BYTES_PER_FRAME) != 0)) {
        rejectedCount = psp_audio_record_rejection(0, NULL);
#if PSP_LOG_ENABLED
        if (psp_audio_should_log_error(rejectedCount)) {
            PspPlatform_LogValue("audio rejected submissions", rejectedCount);
        }
#endif
        return -1;
    }

    frames = size / PSP_AUDIO_BYTES_PER_FRAME;
    if (frames > PSP_AUDIO_MAX_FRAMES) {
        rejectedCount = psp_audio_record_rejection(1, &oversizeCount);
#if PSP_LOG_ENABLED
        if (psp_audio_should_log_error(oversizeCount)) {
            PspPlatform_LogValue("audio rejected oversize frames", frames);
            PspPlatform_LogValue("audio rejected submissions", rejectedCount);
        }
#endif
        return -1;
    }
    if (sThread < 0) {
        rejectedCount = psp_audio_record_rejection(0, NULL);
#if PSP_LOG_ENABLED
        if (psp_audio_should_log_error(rejectedCount)) {
            PspPlatform_LogValue("audio rejected submissions", rejectedCount);
        }
#endif
        return -1;
    }

    inputSamples = samples;
    sampleCount = frames * PSP_AUDIO_CHANNELS;
    for (i = 0; i < sampleCount; i++) {
        int sample = inputSamples[i];
        unsigned int magnitude = sample < 0 ? (unsigned int) -sample : (unsigned int) sample;

        if (sample != 0) {
            nonzeroSamples++;
        }
        if (magnitude > peakSampleMagnitude) {
            peakSampleMagnitude = magnitude;
        }
    }

    sceKernelWaitSema(sSpace, 1, NULL);
    sceKernelWaitSema(sMutex, 1, NULL);

    if (sQueuedBlocks == PSP_AUDIO_BLOCK_COUNT) {
        sOverruns++;
        sRejectedSubmissions++;
        rejectedCount = sRejectedSubmissions;
        sceKernelSignalSema(sMutex, 1);
        sceKernelSignalSema(sSpace, 1);

#if PSP_LOG_ENABLED
        if (psp_audio_should_log_error(sOverruns)) {
            PspPlatform_LogFrame("audio output overrun", sOverruns);
            PspPlatform_LogValue("audio rejected submissions", rejectedCount);
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
    sSubmittedFrames += frames;
    if ((sMinSubmittedFrames == 0) || (frames < sMinSubmittedFrames)) {
        sMinSubmittedFrames = frames;
    }
    if (frames > sMaxSubmittedFrames) {
        sMaxSubmittedFrames = frames;
    }
    if (sQueuedBlocks > sPeakQueuedBlocks) {
        sPeakQueuedBlocks = sQueuedBlocks;
    }
    if (nonzeroSamples == 0) {
        sSilentBlocks++;
    } else if (sNonzeroSamples == 0) {
        firstSignalBlock = sSubmittedBlocks;
    }
    sNonzeroSamples += nonzeroSamples;
    if (peakSampleMagnitude > sPeakSampleMagnitude) {
        sPeakSampleMagnitude = peakSampleMagnitude;
    }
    submittedBlocks = sSubmittedBlocks;

    sceKernelSignalSema(sMutex, 1);
    sceKernelSignalSema(sReady, 1);

#if PSP_LOG_ENABLED
    if (submittedBlocks == 1) {
        PspPlatform_LogValue("audio first PCM block frames", frames);
    }
    if (firstSignalBlock != 0) {
        PspPlatform_LogValue("audio first signal block", firstSignalBlock);
        PspPlatform_LogValue("audio first signal peak", peakSampleMagnitude);
    }
    if ((submittedBlocks % PSP_AUDIO_SUMMARY_INTERVAL) == 0) {
        psp_audio_log_summary();
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
