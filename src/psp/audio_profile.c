#include "src/psp/audio_profile.h"

#if PSP_AUDIO_PROFILE

#include "PR/abi.h"
#include "src/psp/platform.h"

#include <stdio.h>
#include <stdint.h>

#define PSP_AUDIO_PROFILE_OPCODE_COUNT 32
#define PSP_AUDIO_PROFILE_UNCACHED 0x40000000U
#define PSP_AUDIO_PROFILE_REPORT_FIRST 256

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

void PspAudioProfile_MeBeginJob(u32 commandCount) {
    if (!sCounterReady) {
        psp_audio_profile_start_count();
    }
    PSP_AUDIO_PROFILE_STATS->commands += commandCount;
    sPreviousOpcode = PSP_AUDIO_PROFILE_OPCODE_COUNT;
    sPreviousPreviousOpcode = PSP_AUDIO_PROFILE_OPCODE_COUNT;
    sPreviousPreviousPreviousOpcode = PSP_AUDIO_PROFILE_OPCODE_COUNT;
    sJobStart = psp_audio_profile_read_count();
}

void PspAudioProfile_MeBeginCommand(u32 opcode, u32 work) {
    if (opcode >= PSP_AUDIO_PROFILE_OPCODE_COUNT) {
        opcode = PSP_AUDIO_PROFILE_OPCODE_COUNT - 1;
    }
    if (sPreviousOpcode < PSP_AUDIO_PROFILE_OPCODE_COUNT) {
        PSP_AUDIO_PROFILE_STATS->pairs[sPreviousOpcode][opcode]++;
    }
    if ((sPreviousPreviousPreviousOpcode == A_RESAMPLE) &&
        (sPreviousPreviousOpcode == A_ENVSETUP1) &&
        (sPreviousOpcode == A_ENVSETUP2) && (opcode == A_ENVMIXER)) {
        PSP_AUDIO_PROFILE_STATS->resampleEnvMixerChains++;
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
}

#endif
