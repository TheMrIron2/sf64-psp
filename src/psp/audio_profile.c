#include "src/psp/audio_profile.h"

#if PSP_AUDIO_PROFILE

#include "PR/abi.h"
#include "src/psp/platform.h"

#include <stdio.h>
#include <stdint.h>

#define PSP_AUDIO_PROFILE_OPCODE_COUNT 32
#define PSP_AUDIO_PROFILE_UNCACHED 0x40000000U
#define PSP_AUDIO_PROFILE_REPORT_FIRST 256
#define PSP_AUDIO_PROFILE_ENV_SIZE_BINS 12
#define PSP_AUDIO_PROFILE_ENV_JOB_BINS 17
#define PSP_AUDIO_PROFILE_ENV_LAYOUTS 8
#define PSP_AUDIO_PROFILE_ENV_RUN_BINS 9
#define PSP_AUDIO_PROFILE_ROUND_UP_16(v) (((v) + 15) & ~15)

typedef struct {
    u16 inAddr;
    u16 dryLeft;
    u16 dryRight;
    u16 wetLeft;
    u16 wetRight;
    u16 pad;
    u32 calls;
} PspAudioProfileEnvLayout;

typedef struct {
    u32 jobs;
    u32 commands;
    u64 jobTicks;
    u32 jobMaxTicks;
    u32 lastJobTicks;
    u64 opcodeCalls[PSP_AUDIO_PROFILE_OPCODE_COUNT];
    u64 opcodeWork[PSP_AUDIO_PROFILE_OPCODE_COUNT];
    u64 opcodeTicks[PSP_AUDIO_PROFILE_OPCODE_COUNT];
    u32 opcodeMaxTicks[PSP_AUDIO_PROFILE_OPCODE_COUNT];
    u32 pairs[PSP_AUDIO_PROFILE_OPCODE_COUNT][PSP_AUDIO_PROFILE_OPCODE_COUNT];
    u32 resampleEnvMixerChains;
    u32 resampleEnvMixerRangeMatches;
    u32 resampleEnvMixerRangeMismatches;
    u32 envCalls;
    u64 envSamples;
    u32 envSizeBins[PSP_AUDIO_PROFILE_ENV_SIZE_BINS];
    u32 envJobBins[PSP_AUDIO_PROFILE_ENV_JOB_BINS];
    u32 envMaxPerJob;
    u32 envStereoCalls;
    u32 envOtherChannelCalls;
    u32 envFlagBitCalls[5];
    u32 envFlagCombinations;
    u32 envNonzeroRateCalls;
    u32 envVolumeChangeCalls;
    u32 envRateChangeCalls;
    u32 envSameDestinationCalls;
    u32 envSameDestSampleChanges;
    u32 envSameDestFlagChanges;
    u32 envSameDestVolLeftChanges;
    u32 envSameDestVolRightChanges;
    u32 envSameDestVolWetChanges;
    u32 envSameDestRateLeftChanges;
    u32 envSameDestRateRightChanges;
    u32 envSameDestRateWetChanges;
    u32 envMaxDestinationRun;
    u32 envDestinationRunBins[PSP_AUDIO_PROFILE_ENV_RUN_BINS];
    u16 envMinVolLeft;
    u16 envMaxVolLeft;
    u16 envMinVolRight;
    u16 envMaxVolRight;
    u16 envMinVolWet;
    u16 envMaxVolWet;
    s16 envMinRateLeft;
    s16 envMaxRateLeft;
    s16 envMinRateRight;
    s16 envMaxRateRight;
    s16 envMinRateWet;
    s16 envMaxRateWet;
    u32 envLayoutOverflow;
    PspAudioProfileEnvLayout envLayouts[PSP_AUDIO_PROFILE_ENV_LAYOUTS];
    u32 waitCalls[PSP_AUDIO_PROFILE_WAIT_REASON_COUNT];
    u32 waitBlocked[PSP_AUDIO_PROFILE_WAIT_REASON_COUNT];
    u64 waitUs[PSP_AUDIO_PROFILE_WAIT_REASON_COUNT];
    u32 waitMaxUs[PSP_AUDIO_PROFILE_WAIT_REASON_COUNT];
    u32 completions;
    u64 completionUs;
    u32 completionMaxUs;
    u32 fallbacks;
    u32 readOverheadTicks;
} PspAudioProfileStats;

static volatile PspAudioProfileStats sProfileStorage
    __attribute__((aligned(64), section(".uncached")));

#define PSP_AUDIO_PROFILE_STATS \
    ((volatile PspAudioProfileStats*) \
        (PSP_AUDIO_PROFILE_UNCACHED | (u32) (uintptr_t) &sProfileStorage))

static u32 sJobStart;
static u32 sCommandStart;
static u32 sPreviousOpcode;
static u32 sPreviousPreviousOpcode;
static u32 sPreviousPreviousPreviousOpcode;
static u32 sLastReportedJobs;
static s32 sCounterReady;
static s32 sProfileActive;
static u16 sDmemOut;
static u16 sDmemBytes;
static u16 sLastResampleOut;
static u16 sLastResampleSamples;
static s32 sLastResampleValid;
static u32 sEnvCallsThisJob;
static u32 sEnvDestinationRun;
static u32 sPreviousEnvDestinations;
static s32 sPreviousEnvDestinationValid;
static u16 sPreviousEnvVolLeft;
static u16 sPreviousEnvVolRight;
static u16 sPreviousEnvVolWet;
static u16 sPreviousEnvRateLeft;
static u16 sPreviousEnvRateRight;
static u16 sPreviousEnvRateWet;
static u16 sPreviousEnvSamples;
static u32 sPreviousEnvFlags;
static s32 sPreviousEnvParametersValid;

static inline u32 psp_audio_profile_read_count(void) {
    u32 count;

    __asm__ volatile("mfc0 %0, $9" : "=r"(count));
    return count;
}

static void psp_audio_profile_start_count(void) {
    u32 compare = 0xFFFFFFFFU;
    u32 best = 0xFFFFFFFFU;
    s32 i;

    __asm__ volatile(
        ".set push\n"
        ".set noreorder\n"
        "mtc0 $zero, $9\n"
        "mtc0 %0, $11\n"
        "sync\n"
        "nop\n"
        "nop\n"
        "nop\n"
        ".set pop\n"
        :
        : "r"(compare)
        : "memory");
    for (i = 0; i < 32; i++) {
        u32 start = psp_audio_profile_read_count();
        u32 elapsed = psp_audio_profile_read_count() - start;

        if (elapsed < best) {
            best = elapsed;
        }
    }
    PSP_AUDIO_PROFILE_STATS->readOverheadTicks = best;
    sCounterReady = 1;
}

static void psp_audio_profile_record_env_run(void) {
    u32 run = sEnvDestinationRun;
    u32 bin;

    if (run == 0) {
        return;
    }
    if (run <= 4) {
        bin = run - 1;
    } else if (run <= 8) {
        bin = 4;
    } else if (run <= 16) {
        bin = 5;
    } else if (run <= 32) {
        bin = 6;
    } else if (run <= 64) {
        bin = 7;
    } else {
        bin = 8;
    }
    PSP_AUDIO_PROFILE_STATS->envDestinationRunBins[bin]++;
}

void PspAudioProfile_MeBeginJob(u32 commandCount) {
    if (!sCounterReady) {
        psp_audio_profile_start_count();
    }
    PSP_AUDIO_PROFILE_STATS->commands += commandCount;
    sPreviousOpcode = PSP_AUDIO_PROFILE_OPCODE_COUNT;
    sPreviousPreviousOpcode = PSP_AUDIO_PROFILE_OPCODE_COUNT;
    sPreviousPreviousPreviousOpcode = PSP_AUDIO_PROFILE_OPCODE_COUNT;
    sDmemOut = 0;
    sDmemBytes = 0;
    sLastResampleValid = 0;
    sEnvCallsThisJob = 0;
    sEnvDestinationRun = 0;
    sPreviousEnvDestinationValid = 0;
    sProfileActive = 1;
    sJobStart = psp_audio_profile_read_count();
}

void PspAudioProfile_MeBeginCommand(u32 w0, u32 w1, u32 work) {
    u32 opcode = w0 >> 24;

    if (opcode >= PSP_AUDIO_PROFILE_OPCODE_COUNT) {
        opcode = PSP_AUDIO_PROFILE_OPCODE_COUNT - 1;
    }
    if (sPreviousOpcode < PSP_AUDIO_PROFILE_OPCODE_COUNT) {
        PSP_AUDIO_PROFILE_STATS->pairs[sPreviousOpcode][opcode]++;
    }
    if ((sPreviousPreviousPreviousOpcode == A_RESAMPLE) &&
        (sPreviousPreviousOpcode == A_ENVSETUP1) &&
        (sPreviousOpcode == A_ENVSETUP2) && (opcode == A_ENVMIXER)) {
        u16 envInput = ((w0 >> 16) & 0xFF) << 4;
        u16 envSamples =
            PSP_AUDIO_PROFILE_ROUND_UP_16((w0 >> 8) & 0xFF);

        if (envSamples > 192) {
            envSamples = 192;
        }
        PSP_AUDIO_PROFILE_STATS->resampleEnvMixerChains++;
        if (sLastResampleValid && (envInput == sLastResampleOut) &&
            (envSamples == sLastResampleSamples)) {
            PSP_AUDIO_PROFILE_STATS->resampleEnvMixerRangeMatches++;
        } else {
            PSP_AUDIO_PROFILE_STATS->resampleEnvMixerRangeMismatches++;
        }
    }
    if (opcode == A_SETBUFF) {
        sDmemOut = w1 >> 16;
        sDmemBytes = w1 & 0xFFFF;
    } else if (opcode == A_RESAMPLE) {
        sLastResampleOut = sDmemOut;
        sLastResampleSamples =
            PSP_AUDIO_PROFILE_ROUND_UP_16(sDmemBytes) / sizeof(s16);
        sLastResampleValid = 1;
    }
    sPreviousPreviousPreviousOpcode = sPreviousPreviousOpcode;
    sPreviousPreviousOpcode = sPreviousOpcode;
    sPreviousOpcode = opcode;
    PSP_AUDIO_PROFILE_STATS->opcodeCalls[opcode]++;
    PSP_AUDIO_PROFILE_STATS->opcodeWork[opcode] += work;
    sCommandStart = psp_audio_profile_read_count();
}

void PspAudioProfile_MeEndCommand(u32 opcode) {
    u32 elapsed = psp_audio_profile_read_count() - sCommandStart;
    u32 overhead = PSP_AUDIO_PROFILE_STATS->readOverheadTicks;

    if (opcode >= PSP_AUDIO_PROFILE_OPCODE_COUNT) {
        opcode = PSP_AUDIO_PROFILE_OPCODE_COUNT - 1;
    }
    if (elapsed > overhead) {
        elapsed -= overhead;
    }
    PSP_AUDIO_PROFILE_STATS->opcodeTicks[opcode] += elapsed;
    if (elapsed > PSP_AUDIO_PROFILE_STATS->opcodeMaxTicks[opcode]) {
        PSP_AUDIO_PROFILE_STATS->opcodeMaxTicks[opcode] = elapsed;
    }
}

void PspAudioProfile_MeEndJob(void) {
    u32 elapsed = psp_audio_profile_read_count() - sJobStart;

    PSP_AUDIO_PROFILE_STATS->jobs++;
    PSP_AUDIO_PROFILE_STATS->jobTicks += elapsed;
    PSP_AUDIO_PROFILE_STATS->lastJobTicks = elapsed;
    if (elapsed > PSP_AUDIO_PROFILE_STATS->jobMaxTicks) {
        PSP_AUDIO_PROFILE_STATS->jobMaxTicks = elapsed;
    }
    if (sEnvCallsThisJob >= PSP_AUDIO_PROFILE_ENV_JOB_BINS - 1) {
        PSP_AUDIO_PROFILE_STATS
            ->envJobBins[PSP_AUDIO_PROFILE_ENV_JOB_BINS - 1]++;
    } else {
        PSP_AUDIO_PROFILE_STATS->envJobBins[sEnvCallsThisJob]++;
    }
    if (sEnvCallsThisJob > PSP_AUDIO_PROFILE_STATS->envMaxPerJob) {
        PSP_AUDIO_PROFILE_STATS->envMaxPerJob = sEnvCallsThisJob;
    }
    psp_audio_profile_record_env_run();
    sProfileActive = 0;
}

void PspAudioProfile_RecordEnvMixer(
    u16 inAddr, u16 samples, u32 flags, u32 destinations, u32 channels,
    u16 volLeft, u16 volRight, u16 volWet, u16 rateLeft, u16 rateRight,
    u16 rateWet) {
    volatile PspAudioProfileStats* stats = PSP_AUDIO_PROFILE_STATS;
    u16 dryLeft = ((destinations >> 24) & 0xFF) << 4;
    u16 dryRight = ((destinations >> 16) & 0xFF) << 4;
    u16 wetLeft = ((destinations >> 8) & 0xFF) << 4;
    u16 wetRight = (destinations & 0xFF) << 4;
    s32 sameDestination = sPreviousEnvDestinationValid &&
        (destinations == sPreviousEnvDestinations);
    u32 bin;
    u32 index;

    if (!sProfileActive) {
        return;
    }
    stats->envCalls++;
    stats->envSamples += samples;
    sEnvCallsThisJob++;
    bin = samples == 0 ? 0 : (samples - 1) / 16;
    if (bin >= PSP_AUDIO_PROFILE_ENV_SIZE_BINS) {
        bin = PSP_AUDIO_PROFILE_ENV_SIZE_BINS - 1;
    }
    stats->envSizeBins[bin]++;
    if (channels == 2) {
        stats->envStereoCalls++;
    } else {
        stats->envOtherChannelCalls++;
    }
    stats->envFlagCombinations |= 1U << (flags & 31);
    for (index = 0; index < 5; index++) {
        if (flags & (1U << index)) {
            stats->envFlagBitCalls[index]++;
        }
    }
    if ((rateLeft != 0) || (rateRight != 0) || (rateWet != 0)) {
        stats->envNonzeroRateCalls++;
    }
    if (sPreviousEnvParametersValid) {
        if ((volLeft != sPreviousEnvVolLeft) ||
            (volRight != sPreviousEnvVolRight) ||
            (volWet != sPreviousEnvVolWet)) {
            stats->envVolumeChangeCalls++;
        }
        if ((rateLeft != sPreviousEnvRateLeft) ||
            (rateRight != sPreviousEnvRateRight) ||
            (rateWet != sPreviousEnvRateWet)) {
            stats->envRateChangeCalls++;
        }
        if (sameDestination) {
            stats->envSameDestSampleChanges +=
                samples != sPreviousEnvSamples;
            stats->envSameDestFlagChanges += flags != sPreviousEnvFlags;
            stats->envSameDestVolLeftChanges +=
                volLeft != sPreviousEnvVolLeft;
            stats->envSameDestVolRightChanges +=
                volRight != sPreviousEnvVolRight;
            stats->envSameDestVolWetChanges +=
                volWet != sPreviousEnvVolWet;
            stats->envSameDestRateLeftChanges +=
                rateLeft != sPreviousEnvRateLeft;
            stats->envSameDestRateRightChanges +=
                rateRight != sPreviousEnvRateRight;
            stats->envSameDestRateWetChanges +=
                rateWet != sPreviousEnvRateWet;
        }
    } else {
        stats->envMinVolLeft = volLeft;
        stats->envMaxVolLeft = volLeft;
        stats->envMinVolRight = volRight;
        stats->envMaxVolRight = volRight;
        stats->envMinVolWet = volWet;
        stats->envMaxVolWet = volWet;
        stats->envMinRateLeft = (s16) rateLeft;
        stats->envMaxRateLeft = (s16) rateLeft;
        stats->envMinRateRight = (s16) rateRight;
        stats->envMaxRateRight = (s16) rateRight;
        stats->envMinRateWet = (s16) rateWet;
        stats->envMaxRateWet = (s16) rateWet;
        sPreviousEnvParametersValid = 1;
    }
    if (volLeft < stats->envMinVolLeft) {
        stats->envMinVolLeft = volLeft;
    }
    if (volLeft > stats->envMaxVolLeft) {
        stats->envMaxVolLeft = volLeft;
    }
    if (volRight < stats->envMinVolRight) {
        stats->envMinVolRight = volRight;
    }
    if (volRight > stats->envMaxVolRight) {
        stats->envMaxVolRight = volRight;
    }
    if (volWet < stats->envMinVolWet) {
        stats->envMinVolWet = volWet;
    }
    if (volWet > stats->envMaxVolWet) {
        stats->envMaxVolWet = volWet;
    }
    if ((s16) rateLeft < stats->envMinRateLeft) {
        stats->envMinRateLeft = (s16) rateLeft;
    }
    if ((s16) rateLeft > stats->envMaxRateLeft) {
        stats->envMaxRateLeft = (s16) rateLeft;
    }
    if ((s16) rateRight < stats->envMinRateRight) {
        stats->envMinRateRight = (s16) rateRight;
    }
    if ((s16) rateRight > stats->envMaxRateRight) {
        stats->envMaxRateRight = (s16) rateRight;
    }
    if ((s16) rateWet < stats->envMinRateWet) {
        stats->envMinRateWet = (s16) rateWet;
    }
    if ((s16) rateWet > stats->envMaxRateWet) {
        stats->envMaxRateWet = (s16) rateWet;
    }
    sPreviousEnvVolLeft = volLeft;
    sPreviousEnvVolRight = volRight;
    sPreviousEnvVolWet = volWet;
    sPreviousEnvRateLeft = rateLeft;
    sPreviousEnvRateRight = rateRight;
    sPreviousEnvRateWet = rateWet;
    sPreviousEnvSamples = samples;
    sPreviousEnvFlags = flags;

    if (sameDestination) {
        stats->envSameDestinationCalls++;
        sEnvDestinationRun++;
    } else {
        psp_audio_profile_record_env_run();
        sEnvDestinationRun = 1;
    }
    if (sEnvDestinationRun > stats->envMaxDestinationRun) {
        stats->envMaxDestinationRun = sEnvDestinationRun;
    }
    sPreviousEnvDestinations = destinations;
    sPreviousEnvDestinationValid = 1;

    for (index = 0; index < PSP_AUDIO_PROFILE_ENV_LAYOUTS; index++) {
        volatile PspAudioProfileEnvLayout* layout = &stats->envLayouts[index];

        if (layout->calls == 0) {
            layout->inAddr = inAddr;
            layout->dryLeft = dryLeft;
            layout->dryRight = dryRight;
            layout->wetLeft = wetLeft;
            layout->wetRight = wetRight;
            layout->calls = 1;
            return;
        }
        if ((layout->inAddr == inAddr) && (layout->dryLeft == dryLeft) &&
            (layout->dryRight == dryRight) &&
            (layout->wetLeft == wetLeft) &&
            (layout->wetRight == wetRight)) {
            layout->calls++;
            return;
        }
    }
    stats->envLayoutOverflow++;
}

void PspAudioProfile_RecordWait(PspAudioProfileWaitReason reason, s32 blocked,
                                u32 elapsedUs) {
    if ((u32) reason >= PSP_AUDIO_PROFILE_WAIT_REASON_COUNT) {
        return;
    }
    PSP_AUDIO_PROFILE_STATS->waitCalls[reason]++;
    if (blocked) {
        PSP_AUDIO_PROFILE_STATS->waitBlocked[reason]++;
        PSP_AUDIO_PROFILE_STATS->waitUs[reason] += elapsedUs;
        if (elapsedUs > PSP_AUDIO_PROFILE_STATS->waitMaxUs[reason]) {
            PSP_AUDIO_PROFILE_STATS->waitMaxUs[reason] = elapsedUs;
        }
    }
}

void PspAudioProfile_RecordCompletion(u32 elapsedUs) {
    PSP_AUDIO_PROFILE_STATS->completions++;
    PSP_AUDIO_PROFILE_STATS->completionUs += elapsedUs;
    if (elapsedUs > PSP_AUDIO_PROFILE_STATS->completionMaxUs) {
        PSP_AUDIO_PROFILE_STATS->completionMaxUs = elapsedUs;
    }
}

void PspAudioProfile_RecordFallback(void) {
    PSP_AUDIO_PROFILE_STATS->fallbacks++;
}

static const char* psp_audio_profile_opcode_name(u32 opcode) {
    switch (opcode) {
        case A_ADPCM: return "ADPCM";
        case A_CLEARBUFF: return "CLEAR";
        case A_ADDMIXER: return "ADDMIX";
        case A_RESAMPLE: return "RESAMPLE";
        case A_RESAMPLE_ZOH: return "RESAMP_ZOH";
        case A_FILTER: return "FILTER";
        case A_SETBUFF: return "SETBUFF";
        case A_DUPLICATE: return "DUPLICATE";
        case A_DMEMMOVE: return "DMEMMOVE";
        case A_LOADADPCM: return "LOADADPCM";
        case A_MIXER: return "MIXER";
        case A_INTERLEAVE: return "INTERLEAVE";
        case A_SETLOOP: return "SETLOOP";
        case A_INTERL: return "INTERL";
        case A_ENVSETUP1: return "ENVSETUP1";
        case A_ENVMIXER: return "ENVMIXER";
        case A_LOADBUFF: return "LOADBUFF";
        case A_SAVEBUFF: return "SAVEBUFF";
        case A_ENVSETUP2: return "ENVSETUP2";
        case A_S8DEC: return "S8DEC";
        case A_HILOGAIN: return "HILOGAIN";
        default: return "OTHER";
    }
}

static void psp_audio_profile_report_wait(PspAudioProfileWaitReason reason,
                                          const char* name) {
    volatile PspAudioProfileStats* stats = PSP_AUDIO_PROFILE_STATS;
    char line[192];
    u32 blocked = stats->waitBlocked[reason];

    snprintf(line, sizeof(line),
             "[audio-prof] wait=%s calls=%lu blocked=%lu total_us=%llu avg_us=%llu max_us=%lu",
             name, (unsigned long) stats->waitCalls[reason],
             (unsigned long) blocked, (unsigned long long) stats->waitUs[reason],
             (unsigned long long) (blocked ? stats->waitUs[reason] / blocked : 0),
             (unsigned long) stats->waitMaxUs[reason]);
    PspPlatform_LogAudioProfileLine(line);
}

static void psp_audio_profile_report_env(void) {
    volatile PspAudioProfileStats* stats = PSP_AUDIO_PROFILE_STATS;
    char line[256];
    u32 index;

    if (stats->envCalls == 0) {
        return;
    }
    snprintf(line, sizeof(line),
             "[audio-prof-env] calls=%lu samples=%llu avg=%llu stereo=%lu other=%lu same_dest=%lu max_run=%lu chain_range=%lu/%lu",
             (unsigned long) stats->envCalls,
             (unsigned long long) stats->envSamples,
             (unsigned long long) (stats->envSamples / stats->envCalls),
             (unsigned long) stats->envStereoCalls,
             (unsigned long) stats->envOtherChannelCalls,
             (unsigned long) stats->envSameDestinationCalls,
             (unsigned long) stats->envMaxDestinationRun,
             (unsigned long) stats->resampleEnvMixerRangeMatches,
             (unsigned long) stats->resampleEnvMixerRangeMismatches);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] run_bins_1_2_3_4_5to8_9to16_17to32_33to64_65plus=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
             (unsigned long) stats->envDestinationRunBins[0],
             (unsigned long) stats->envDestinationRunBins[1],
             (unsigned long) stats->envDestinationRunBins[2],
             (unsigned long) stats->envDestinationRunBins[3],
             (unsigned long) stats->envDestinationRunBins[4],
             (unsigned long) stats->envDestinationRunBins[5],
             (unsigned long) stats->envDestinationRunBins[6],
             (unsigned long) stats->envDestinationRunBins[7],
             (unsigned long) stats->envDestinationRunBins[8]);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] same_dest_changes samples=%lu flags=%lu vol=%lu,%lu,%lu rate=%lu,%lu,%lu",
             (unsigned long) stats->envSameDestSampleChanges,
             (unsigned long) stats->envSameDestFlagChanges,
             (unsigned long) stats->envSameDestVolLeftChanges,
             (unsigned long) stats->envSameDestVolRightChanges,
             (unsigned long) stats->envSameDestVolWetChanges,
             (unsigned long) stats->envSameDestRateLeftChanges,
             (unsigned long) stats->envSameDestRateRightChanges,
             (unsigned long) stats->envSameDestRateWetChanges);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] size_bins_16_to_192=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
             (unsigned long) stats->envSizeBins[0],
             (unsigned long) stats->envSizeBins[1],
             (unsigned long) stats->envSizeBins[2],
             (unsigned long) stats->envSizeBins[3],
             (unsigned long) stats->envSizeBins[4],
             (unsigned long) stats->envSizeBins[5],
             (unsigned long) stats->envSizeBins[6],
             (unsigned long) stats->envSizeBins[7],
             (unsigned long) stats->envSizeBins[8],
             (unsigned long) stats->envSizeBins[9],
             (unsigned long) stats->envSizeBins[10],
             (unsigned long) stats->envSizeBins[11]);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] job_bins_0_to_7=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
             (unsigned long) stats->envJobBins[0],
             (unsigned long) stats->envJobBins[1],
             (unsigned long) stats->envJobBins[2],
             (unsigned long) stats->envJobBins[3],
             (unsigned long) stats->envJobBins[4],
             (unsigned long) stats->envJobBins[5],
             (unsigned long) stats->envJobBins[6],
             (unsigned long) stats->envJobBins[7]);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] job_bins_8_to_15=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu job_16_plus=%lu max=%lu",
             (unsigned long) stats->envJobBins[8],
             (unsigned long) stats->envJobBins[9],
             (unsigned long) stats->envJobBins[10],
             (unsigned long) stats->envJobBins[11],
             (unsigned long) stats->envJobBins[12],
             (unsigned long) stats->envJobBins[13],
             (unsigned long) stats->envJobBins[14],
             (unsigned long) stats->envJobBins[15],
             (unsigned long) stats->envJobBins[16],
             (unsigned long) stats->envMaxPerJob);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] flags_seen=%08lx swap=%lu x0=%lu x1=%lu x2=%lu x3=%lu nonzero_rate=%lu vol_changes=%lu rate_changes=%lu",
             (unsigned long) stats->envFlagCombinations,
             (unsigned long) stats->envFlagBitCalls[4],
             (unsigned long) stats->envFlagBitCalls[3],
             (unsigned long) stats->envFlagBitCalls[2],
             (unsigned long) stats->envFlagBitCalls[1],
             (unsigned long) stats->envFlagBitCalls[0],
             (unsigned long) stats->envNonzeroRateCalls,
             (unsigned long) stats->envVolumeChangeCalls,
             (unsigned long) stats->envRateChangeCalls);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] volume_ranges left=%u..%u right=%u..%u wet=%u..%u layout_overflow=%lu",
             stats->envMinVolLeft, stats->envMaxVolLeft,
             stats->envMinVolRight, stats->envMaxVolRight,
             stats->envMinVolWet, stats->envMaxVolWet,
             (unsigned long) stats->envLayoutOverflow);
    PspPlatform_LogAudioProfileLine(line);
    snprintf(line, sizeof(line),
             "[audio-prof-env] rate_ranges left=%d..%d right=%d..%d wet=%d..%d",
             stats->envMinRateLeft, stats->envMaxRateLeft,
             stats->envMinRateRight, stats->envMaxRateRight,
             stats->envMinRateWet, stats->envMaxRateWet);
    PspPlatform_LogAudioProfileLine(line);
    for (index = 0; index < PSP_AUDIO_PROFILE_ENV_LAYOUTS; index++) {
        volatile PspAudioProfileEnvLayout* layout = &stats->envLayouts[index];

        if (layout->calls == 0) {
            break;
        }
        snprintf(line, sizeof(line),
                 "[audio-prof-env] layout=%lu calls=%lu in=%04x dry=%04x,%04x wet=%04x,%04x",
                 (unsigned long) index, (unsigned long) layout->calls,
                 layout->inAddr, layout->dryLeft, layout->dryRight,
                 layout->wetLeft, layout->wetRight);
        PspPlatform_LogAudioProfileLine(line);
    }
}

void PspAudioProfile_Report(void) {
    volatile PspAudioProfileStats* stats = PSP_AUDIO_PROFILE_STATS;
    char line[224];
    u32 jobs = stats->jobs;
    u32 opcode;
    u32 pair;
    u32 topPair[4] = { 0, 0, 0, 0 };
    u32 topCount[4] = { 0, 0, 0, 0 };

    if ((jobs < PSP_AUDIO_PROFILE_REPORT_FIRST) ||
        ((jobs & (jobs - 1)) != 0) || (jobs == sLastReportedJobs)) {
        return;
    }
    sLastReportedJobs = jobs;
    snprintf(line, sizeof(line),
             "[audio-prof] jobs=%lu commands=%lu avg_ticks=%llu max_ticks=%lu last_ticks=%lu read_ticks=%lu fallbacks=%lu",
             (unsigned long) jobs, (unsigned long) stats->commands,
             (unsigned long long) (stats->jobTicks / jobs),
             (unsigned long) stats->jobMaxTicks,
             (unsigned long) stats->lastJobTicks,
             (unsigned long) stats->readOverheadTicks,
             (unsigned long) stats->fallbacks);
    PspPlatform_LogAudioProfileLine(line);

    snprintf(line, sizeof(line),
             "[audio-prof] completion n=%lu total_us=%llu avg_us=%llu max_us=%lu",
             (unsigned long) stats->completions,
             (unsigned long long) stats->completionUs,
             (unsigned long long) (stats->completions ?
                 stats->completionUs / stats->completions : 0),
             (unsigned long) stats->completionMaxUs);
    PspPlatform_LogAudioProfileLine(line);
    psp_audio_profile_report_wait(PSP_AUDIO_PROFILE_WAIT_PUBLIC, "public");
    psp_audio_profile_report_wait(PSP_AUDIO_PROFILE_WAIT_SUBMIT, "submit");

    for (opcode = 0; opcode < PSP_AUDIO_PROFILE_OPCODE_COUNT; opcode++) {
        u64 calls = stats->opcodeCalls[opcode];

        if (calls == 0) {
            continue;
        }
        snprintf(line, sizeof(line),
                 "[audio-prof] op=%s calls=%llu work=%llu ticks=%llu avg_ticks=%llu max_ticks=%lu",
                 psp_audio_profile_opcode_name(opcode),
                 (unsigned long long) calls,
                 (unsigned long long) stats->opcodeWork[opcode],
                 (unsigned long long) stats->opcodeTicks[opcode],
                 (unsigned long long) (stats->opcodeTicks[opcode] / calls),
                 (unsigned long) stats->opcodeMaxTicks[opcode]);
        PspPlatform_LogAudioProfileLine(line);
    }

    for (pair = 0; pair < PSP_AUDIO_PROFILE_OPCODE_COUNT * PSP_AUDIO_PROFILE_OPCODE_COUNT; pair++) {
        u32 count = stats->pairs[pair / PSP_AUDIO_PROFILE_OPCODE_COUNT]
                                 [pair % PSP_AUDIO_PROFILE_OPCODE_COUNT];
        s32 rank;

        for (rank = 0; rank < 4; rank++) {
            if (count > topCount[rank]) {
                s32 move;

                for (move = 3; move > rank; move--) {
                    topCount[move] = topCount[move - 1];
                    topPair[move] = topPair[move - 1];
                }
                topCount[rank] = count;
                topPair[rank] = pair;
                break;
            }
        }
    }
    for (pair = 0; (pair < 4) && (topCount[pair] != 0); pair++) {
        u32 before = topPair[pair] / PSP_AUDIO_PROFILE_OPCODE_COUNT;
        u32 after = topPair[pair] % PSP_AUDIO_PROFILE_OPCODE_COUNT;

        snprintf(line, sizeof(line), "[audio-prof] pair=%s->%s calls=%lu",
                 psp_audio_profile_opcode_name(before),
                 psp_audio_profile_opcode_name(after),
                 (unsigned long) topCount[pair]);
        PspPlatform_LogAudioProfileLine(line);
    }
    snprintf(line, sizeof(line),
             "[audio-prof] chain=RESAMPLE->ENVSETUP1->ENVSETUP2->ENVMIXER calls=%lu",
             (unsigned long) stats->resampleEnvMixerChains);
    PspPlatform_LogAudioProfileLine(line);
    psp_audio_profile_report_env();
}

#endif
