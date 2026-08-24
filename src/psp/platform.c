#include "PR/ultratypes.h"
#include "PR/mbi.h"
#include "PR/os.h"
#include "PR/os_cont.h"
#include "PR/os_eeprom.h"
#include "PR/ucode.h"
#include "sf64dma.h"
#if PSP_AUDIO
#include "src/psp/audio_me.h"
#endif
#include "src/psp/audio_output.h"
#include "src/psp/input.h"
#include "src/psp/platform.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

typedef int SceUID;
typedef unsigned int SceSize;
typedef unsigned int SceMode;


int sceDisplayWaitVblankStart(void);
SceUID sceKernelCreateThread(const char* name, int (*entry)(SceSize, void*), int initPriority, int stackSize,
                             int attr, void* option);
int sceKernelStartThread(SceUID thid, SceSize arglen, void* argp);

SceUID sceKernelCreateSema(const char* name, int attr, int initVal, int maxVal, void* option);
int sceKernelWaitSema(SceUID semaid, int signal, unsigned int* timeout);
int sceKernelSignalSema(SceUID semaid, int signal);
void sceKernelExitGame(void);
int sceKernelCpuSuspendIntr(void);
void sceKernelCpuResumeIntr(int flags);
SceUID sceIoOpen(const char* file, int flags, SceMode mode);
int sceIoWrite(SceUID fd, const void* data, SceSize size);
int sceIoClose(SceUID fd);
int sceIoRemove(const char* file);
void pspDebugScreenSetXY(int x, int y);
void pspDebugScreenPrintf(const char* fmt, ...);
float sqrtf(float x);

#define PSP_LOG_PATH_MS0 "ms0:/sf64_psp.log"
#define PSP_LOG_PATH_EF0 "ef0:/sf64_psp.log"
#define PSP_LOG_PATH_HOST0 "host0:/sf64_psp.log"
#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif
#ifndef PSP_AUDIO_PROFILE
#define PSP_AUDIO_PROFILE 0
#endif
#ifndef PSP_AUDIO_VME
#define PSP_AUDIO_VME 0
#endif
#ifndef PSP_AUDIO_VME_VALIDATE
#define PSP_AUDIO_VME_VALIDATE 0
#endif
#ifndef PSP_AUDIO_VME_BENCH
#define PSP_AUDIO_VME_BENCH 0
#endif
#define PSP_FILE_LOG_ENABLED (PSP_LOG_ENABLED || PSP_AUDIO_PROFILE || PSP_AUDIO_VME)
#ifndef PSP_DEBUG_OVERLAY_ENABLED
#define PSP_DEBUG_OVERLAY_ENABLED 0
#endif
#define PSP_O_WRONLY 0x0002
#define PSP_O_APPEND 0x0100
#define PSP_O_CREAT 0x0200
#define PSP_O_TRUNC 0x0400

typedef struct {
    OSMesgQueue* mq;
    OSMesg msg;
} PspEvent;

static PspEvent sEvents[OS_NUM_EVENTS];
static OSMesgQueue* sViMq;
static OSMesg sViMsg;
static u32 sViRetraceCount = 1;
static SceUID sViThreadId = -1;
#if PSP_FILE_LOG_ENABLED
static SceUID sLogSemaId = -1;
#endif
static volatile int sViEventPending;
static volatile int sExitRequested;
static u32 sGfxTaskCount;
static u32 sAudioTaskCount;
static u32 sViCount;


u32 osMemSize = 24 * 1024 * 1024;
s32 osTvType = OS_TV_NTSC;
long long int rspbootTextStart[1], rspbootTextEnd[1];
long long int aspMainTextStart[1];
long long int aspMainDataStart[1], aspMainDataEnd[1];
long long int gspF3DEX_fifoTextStart[1], gspF3DEX_fifoTextEnd[1];
long long int gspF3DEX_fifoDataStart[1], gspF3DEX_fifoDataEnd[1];

#define PSP_EMPTY_RANGE_IMPL(start, end) \
    __asm__(".section .rodata\n" \
            ".balign 4\n" \
            ".globl " #start "\n" \
            #start ":\n" \
            ".globl " #end "\n" \
            #end ":\n" \
            ".previous\n")
#define PSP_EMPTY_RANGE(start, end) PSP_EMPTY_RANGE_IMPL(start, end)
#define PSP_EMPTY_SYMBOL_IMPL(symbol) \
    __asm__(".section .rodata\n" \
            ".balign 4\n" \
            ".globl " #symbol "\n" \
            #symbol ":\n" \
            ".previous\n")
#define PSP_EMPTY_SYMBOL(symbol) PSP_EMPTY_SYMBOL_IMPL(symbol)
#define PSP_EMPTY_SEGMENT(name) \
    PSP_EMPTY_RANGE(name##_ROM_START, name##_ROM_END); \
    PSP_EMPTY_RANGE(name##_VRAM, name##_VRAM_END); \
    PSP_EMPTY_RANGE(name##_TEXT_START, name##_TEXT_END); \
    PSP_EMPTY_RANGE(name##_DATA_START, name##_DATA_END); \
    PSP_EMPTY_SYMBOL(name##_DATA_SIZE); \
    PSP_EMPTY_RANGE(name##_RODATA_START, name##_RODATA_END); \
    PSP_EMPTY_RANGE(name##_BSS_START, name##_BSS_END)

PSP_EMPTY_SEGMENT(makerom);
PSP_EMPTY_SEGMENT(main);
PSP_EMPTY_SEGMENT(dma_table);
PSP_EMPTY_SEGMENT(ast_common);
PSP_EMPTY_SEGMENT(ast_bg_space);
PSP_EMPTY_SEGMENT(ast_bg_planet);
PSP_EMPTY_SEGMENT(ast_arwing);
PSP_EMPTY_SEGMENT(ast_landmaster);
PSP_EMPTY_SEGMENT(ast_blue_marine);
PSP_EMPTY_SEGMENT(ast_versus);
PSP_EMPTY_SEGMENT(ast_enmy_planet);
PSP_EMPTY_SEGMENT(ast_enmy_space);
PSP_EMPTY_SEGMENT(ast_great_fox);
PSP_EMPTY_SEGMENT(ast_star_wolf);
PSP_EMPTY_SEGMENT(ast_allies);
PSP_EMPTY_SEGMENT(ast_corneria);
PSP_EMPTY_SEGMENT(ast_meteo);
PSP_EMPTY_SEGMENT(ast_titania);
PSP_EMPTY_SEGMENT(ast_7_ti_2);
PSP_EMPTY_SEGMENT(ast_8_ti);
PSP_EMPTY_SEGMENT(ast_9_ti);
PSP_EMPTY_SEGMENT(ast_A_ti);
PSP_EMPTY_SEGMENT(ast_7_ti_1);
PSP_EMPTY_SEGMENT(ast_sector_x);
PSP_EMPTY_SEGMENT(ast_sector_z);
PSP_EMPTY_SEGMENT(ast_aquas);
PSP_EMPTY_SEGMENT(ast_area_6);
PSP_EMPTY_SEGMENT(ast_venom_1);
PSP_EMPTY_SEGMENT(ast_venom_2);
PSP_EMPTY_SEGMENT(ast_ve1_boss);
PSP_EMPTY_SEGMENT(ast_bolse);
PSP_EMPTY_SEGMENT(ast_fortuna);
PSP_EMPTY_SEGMENT(ast_sector_y);
PSP_EMPTY_SEGMENT(ast_solar);
PSP_EMPTY_SEGMENT(ast_zoness);
PSP_EMPTY_SEGMENT(ast_katina);
PSP_EMPTY_SEGMENT(ast_macbeth);
PSP_EMPTY_SEGMENT(ast_warp_zone);
PSP_EMPTY_SEGMENT(ast_title);
PSP_EMPTY_SEGMENT(ast_map);
PSP_EMPTY_SEGMENT(ast_map_en);
PSP_EMPTY_SEGMENT(ast_map_fr);
PSP_EMPTY_SEGMENT(ast_map_de);
PSP_EMPTY_SEGMENT(ast_option);
PSP_EMPTY_SEGMENT(ast_option_en);
PSP_EMPTY_SEGMENT(ast_option_fr);
PSP_EMPTY_SEGMENT(ast_option_de);
PSP_EMPTY_SEGMENT(ast_vs_menu);
PSP_EMPTY_SEGMENT(ast_vs_menu_en);
PSP_EMPTY_SEGMENT(ast_vs_menu_fr);
PSP_EMPTY_SEGMENT(ast_vs_menu_de);
PSP_EMPTY_SEGMENT(ast_text);
PSP_EMPTY_SEGMENT(ast_font_3d);
PSP_EMPTY_SEGMENT(ast_andross);
PSP_EMPTY_SEGMENT(ast_logo);
PSP_EMPTY_SEGMENT(ast_ending);
PSP_EMPTY_SEGMENT(ast_ending_award_front);
PSP_EMPTY_SEGMENT(ast_ending_award_back);
PSP_EMPTY_SEGMENT(ast_ending_expert);
PSP_EMPTY_SEGMENT(ast_training);
PSP_EMPTY_SEGMENT(ast_radio);
PSP_EMPTY_SEGMENT(ast_radio_en);
PSP_EMPTY_SEGMENT(ast_radio_fr);
PSP_EMPTY_SEGMENT(ast_radio_de);
PSP_EMPTY_SEGMENT(ovl_i1);
PSP_EMPTY_SEGMENT(ovl_i2);
PSP_EMPTY_SEGMENT(ovl_i3);
PSP_EMPTY_SEGMENT(ovl_i4);
PSP_EMPTY_SEGMENT(ovl_i5);
PSP_EMPTY_SEGMENT(ovl_i6);
PSP_EMPTY_SEGMENT(ovl_menu);
PSP_EMPTY_SEGMENT(ovl_ending);
PSP_EMPTY_SEGMENT(ovl_unused);

#if PSP_FILE_LOG_ENABLED
static u32 psp_strlen(const char* text) {
    u32 len = 0;

    while ((text != NULL) && (text[len] != '\0')) {
        len++;
    }
    return len;
}
#endif

static char* psp_append_text(char* out, const char* text) {
    while ((text != NULL) && (*text != '\0')) {
        *out++ = *text++;
    }
    return out;
}

static char* psp_append_u32(char* out, u32 value) {
    char digits[10];
    s32 count = 0;

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

#if PSP_AUDIO_VME
static char* psp_append_s32(char* out, s32 value) {
    u32 magnitude;

    if (value < 0) {
        *out++ = '-';
        magnitude = 0U - (u32) value;
    } else {
        magnitude = (u32) value;
    }
    return psp_append_u32(out, magnitude);
}

#if PSP_AUDIO_VME_VALIDATE
static char* psp_append_u64(char* out, u64 value) {
    char digits[20];
    s32 count = 0;

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

static char* psp_append_s64(char* out, s64 value) {
    u64 magnitude;

    if (value < 0) {
        *out++ = '-';
        magnitude = 0ULL - (u64) value;
    } else {
        magnitude = (u64) value;
    }
    return psp_append_u64(out, magnitude);
}
#endif
#endif

#if PSP_FILE_LOG_ENABLED
/* ms0 is absent on a PSP Go without an M2 card, ef0 is its internal flash
 * host0 is the PSPLINK host directory and catches both refusing writes */
static const char* psp_log_path(void) {
    static const char* sLogPath;
    static const char* const sCandidates[3] = { PSP_LOG_PATH_MS0, PSP_LOG_PATH_EF0, PSP_LOG_PATH_HOST0 };
    SceUID fd;
    int i;

    if (sLogPath != NULL) {
        return sLogPath;
    }

    for (i = 0; i < 3; i++) {
        fd = sceIoOpen(sCandidates[i], PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
        if (fd >= 0) {
            sceIoClose(fd);
            sLogPath = sCandidates[i];
            return sLogPath;
        }
    }

    sLogPath = PSP_LOG_PATH_MS0;
    return sLogPath;
}
#endif

#if PSP_FILE_LOG_ENABLED
static void psp_log_line(const char* line) {
    SceUID fd;

    if (line == NULL) {
        return;
    }

    if (sLogSemaId >= 0) {
        sceKernelWaitSema(sLogSemaId, 1, NULL);
    }

    fd = sceIoOpen(psp_log_path(), PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, psp_strlen(line));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    if (sLogSemaId >= 0) {
        sceKernelSignalSema(sLogSemaId, 1);
    }
}
#endif

void PspPlatform_LogLine(const char* line) {
#if PSP_LOG_ENABLED
    psp_log_line(line);
#else
    (void) line;
#endif
}

void PspPlatform_LogAudioProfileLine(const char* line) {
#if PSP_AUDIO_PROFILE
    psp_log_line(line);
#else
    (void) line;
#endif
}

void PspPlatform_LogAudioVmeLine(const char* line) {
#if PSP_AUDIO_VME
    psp_log_line(line);
#else
    (void) line;
#endif
}

#if PSP_AUDIO_VME
static void psp_log_audio_vme_result(void) {
    PspAudioVmeSmokeResult result;
    char line[192];
    char* out = line;

    PspAudioMe_GetVmeSmokeResult(&result);
    out = psp_append_text(out, "[audio-vme] smoke state=");
    if (result.state == PSP_AUDIO_VME_READY) {
        out = psp_append_text(out, "ready");
    } else if (result.state == PSP_AUDIO_VME_FAULT) {
        out = psp_append_text(out, "fault");
    } else {
        out = psp_append_text(out, "disabled");
    }
    out = psp_append_text(out, " checkpoint=");
    out = psp_append_u32(out, result.checkpoint);
    out = psp_append_text(out, " runs=");
    out = psp_append_u32(out, result.runs);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    if (result.mismatches != 0) {
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " input=");
        out = psp_append_s32(out, result.input);
        out = psp_append_text(out, " factor=");
        out = psp_append_s32(out, result.factor);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
}
#endif

void PspPlatform_ReportAudioVmeMix(void) {
#if PSP_AUDIO_VME_VALIDATE
    static u32 sLastCalls;
    PspAudioVmeMixResult result;
    const u32 startupCalls = 4;
    char line[224];
    char* out;

    PspAudioMe_GetVmeMixResult(&result);
    if ((result.calls == sLastCalls) ||
        ((result.calls != startupCalls) &&
         ((result.calls - sLastCalls) < 256) &&
         (result.mismatches == 0))) {
        return;
    }
    sLastCalls = result.calls;
    out = line;
    out = psp_append_text(out, "[audio-vme] mixer calls=");
    out = psp_append_u32(out, result.calls);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    if (result.mismatches != 0) {
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " input=");
        out = psp_append_s32(out, result.input);
        out = psp_append_text(out, " old_out=");
        out = psp_append_s32(out, result.oldOutput);
        out = psp_append_text(out, " gain=");
        out = psp_append_s32(out, result.gain);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeFilter(void) {
#if PSP_AUDIO_VME_VALIDATE
    PspAudioVmeFilterResult result;
    char line[192];
    char* out = line;

    PspAudioMe_GetVmeFilterResult(&result);
    out = psp_append_text(out, "[audio-vme] filter runs=");
    out = psp_append_u32(out, result.runs);
    out = psp_append_text(out, " outputs=");
    out = psp_append_u32(out, result.outputs);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    if (result.mismatches != 0) {
        out = psp_append_text(out, " run=");
        out = psp_append_s32(out, result.firstCase);
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeAdpcmAudit(void) {
#if PSP_AUDIO_VME_VALIDATE
    PspAudioVmeAdpcmAuditResult result;
    char line[768];
    char* out = line;

    PspAudioMe_GetVmeAdpcmAuditResult(&result);
    out = psp_append_text(out, "[audio-vme-adpcm-width] runs=");
    out = psp_append_u32(out, result.runs);
    out = psp_append_text(out, " vectors=");
    out = psp_append_u32(out, result.vectors);
    out = psp_append_text(out, " min_acc=");
    out = psp_append_s64(out, result.minAccumulator);
    out = psp_append_text(out, " max_acc=");
    out = psp_append_s64(out, result.maxAccumulator);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    out = psp_append_text(out, " term_runs=");
    out = psp_append_u32(out, result.termRuns);
    out = psp_append_text(out, " term_mismatches=");
    out = psp_append_u32(out, result.termMismatches);
    out = psp_append_text(out, " explicit_runs=");
    out = psp_append_u32(out, result.explicitRuns);
    out = psp_append_text(out, " explicit_mismatches=");
    out = psp_append_u32(out, result.explicitMismatches);
    if (result.termMismatches != 0) {
        out = psp_append_text(out, " first_terms=");
        out = psp_append_s32(out, result.firstTermCount);
        out = psp_append_text(out, " term_expected=");
        out = psp_append_s32(out, result.firstTermExpected);
        out = psp_append_text(out, " term_actual=");
        out = psp_append_s32(out, result.firstTermActual);
    }
    if (result.mismatches != 0) {
        out = psp_append_text(out, " case=");
        out = psp_append_s32(out, result.firstCase);
        out = psp_append_text(out, " terms=");
        out = psp_append_u32(out, result.firstTerms);
        out = psp_append_text(out, " wide_acc=");
        out = psp_append_s64(out, result.firstWideAccumulator);
        out = psp_append_text(out, " scalar_acc=");
        out = psp_append_s32(out, result.firstScalarAccumulator);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.firstExpected);
        out = psp_append_text(out, " raw_actual=");
        out = psp_append_s32(out, result.firstActualRaw);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.firstActual);
    }
    if (result.explicitMismatches != 0) {
        out = psp_append_text(out, " explicit_case=");
        out = psp_append_s32(out, result.firstExplicitCase);
        out = psp_append_text(out, " explicit_expected=");
        out = psp_append_s32(out, result.firstExplicitExpected);
        out = psp_append_text(out, " explicit_raw=");
        out = psp_append_s32(out, result.firstExplicitRaw);
        out = psp_append_text(out, " explicit_actual=");
        out = psp_append_s32(out, result.firstExplicitActual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeAdpcmHandoff(void) {
#if PSP_AUDIO_VME_BENCH
    static u32 sNextReport = 32;
    static u32 sLastCommands;
    static u32 sLastPcmMismatches;
    static u32 sLastStateMismatches;
    PspAudioVmeAdpcmHandoffResult result;
    u64 mainTotal;
    u64 localTotal;
    u64 mainEntry;
    u64 localEntry;
    char line[896];
    char* out = line;

    PspAudioMe_GetVmeAdpcmHandoffResult(&result);
    if ((result.commands == sLastCommands) &&
        (result.pcmMismatches == sLastPcmMismatches) &&
        (result.stateMismatches == sLastStateMismatches)) {
        return;
    }
    if ((result.commands < sNextReport) &&
        (result.pcmMismatches == sLastPcmMismatches) &&
        (result.stateMismatches == sLastStateMismatches)) {
        return;
    }
    while ((sNextReport <= result.commands) && (sNextReport < 256)) {
        sNextReport <<= 1;
    }
    sLastCommands = result.commands;
    sLastPcmMismatches = result.pcmMismatches;
    sLastStateMismatches = result.stateMismatches;
    mainTotal = result.mainCopyTicks + result.mainPrepareTicks +
                result.mainTransferTicks;
    localTotal = result.localCopyTicks + result.localPrepareTicks +
                 result.localTransferTicks;
    mainEntry = result.decodeTicks + mainTotal;
    localEntry = result.decodeTicks + localTotal;

    out = psp_append_text(
        out, "[audio-vme-adpcm-handoff] mode=compare commands=");
    out = psp_append_u32(out, result.commands);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " skipped=");
    out = psp_append_u32(out, result.skipped);
    out = psp_append_text(out, " pcm_mismatches=");
    out = psp_append_u32(out, result.pcmMismatches);
    out = psp_append_text(out, " state_mismatches=");
    out = psp_append_u32(out, result.stateMismatches);
    if (result.commands != 0) {
        out = psp_append_text(out, " decode_materialize=");
        out = psp_append_u64(out, result.decodeTicks / result.commands);
        out = psp_append_text(out, " main_copy=");
        out = psp_append_u64(out, result.mainCopyTicks / result.commands);
        out = psp_append_text(out, " main_prepare=");
        out = psp_append_u64(out, result.mainPrepareTicks / result.commands);
        out = psp_append_text(out, " main_transfer=");
        out = psp_append_u64(out, result.mainTransferTicks / result.commands);
        out = psp_append_text(out, " main_total=");
        out = psp_append_u64(out, mainTotal / result.commands);
        out = psp_append_text(out, " main_entry=");
        out = psp_append_u64(out, mainEntry / result.commands);
        out = psp_append_text(out, " local_copy=");
        out = psp_append_u64(out, result.localCopyTicks / result.commands);
        out = psp_append_text(out, " local_prepare=");
        out = psp_append_u64(out, result.localPrepareTicks / result.commands);
        out = psp_append_text(out, " local_transfer=");
        out = psp_append_u64(out, result.localTransferTicks / result.commands);
        out = psp_append_text(out, " local_total=");
        out = psp_append_u64(out, localTotal / result.commands);
        out = psp_append_text(out, " local_entry=");
        out = psp_append_u64(out, localEntry / result.commands);
        out = psp_append_text(out, " validate=");
        out = psp_append_u64(out, result.validateTicks / result.commands);
    }
    if ((result.pcmMismatches != 0) ||
        (result.stateMismatches != 0)) {
        out = psp_append_text(out, " first_mode=");
        out = psp_append_u32(out, result.firstMode);
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeResample(void) {
#if PSP_AUDIO_VME_VALIDATE
    static u32 sLastCommands = (u32) -1;
    static u32 sLastProductMismatches = (u32) -1;
    static u32 sLastPairMismatches = (u32) -1;
    static u32 sLastOutputMismatches = (u32) -1;
    static u32 sLastStateMismatches = (u32) -1;
    PspAudioVmeResampleResult result;
    char line[768];
    char* out;

    PspAudioMe_GetVmeResampleResult(&result);
    if ((result.commands == sLastCommands) &&
        (result.mismatches == sLastProductMismatches) &&
        (result.pairMismatches == sLastPairMismatches) &&
        (result.outputMismatches == sLastOutputMismatches) &&
        (result.stateMismatches == sLastStateMismatches)) {
        return;
    }
    if ((sLastCommands != (u32) -1) && (result.commands > 1) &&
        ((result.commands - sLastCommands) < 256) &&
        (result.mismatches == 0) && (result.pairMismatches == 0) &&
        (result.outputMismatches == 0) &&
        (result.stateMismatches == 0)) {
        return;
    }
    sLastCommands = result.commands;
    sLastProductMismatches = result.mismatches;
    sLastPairMismatches = result.pairMismatches;
    sLastOutputMismatches = result.outputMismatches;
    sLastStateMismatches = result.stateMismatches;
    out = line;
    out = psp_append_text(out, "[audio-vme] resample runs=");
    out = psp_append_u32(out, result.runs);
    out = psp_append_text(out, " products=");
    out = psp_append_u32(out, result.products);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    out = psp_append_text(out, " commands=");
    out = psp_append_u32(out, result.commands);
    out = psp_append_text(out, " outputs=");
    out = psp_append_u32(out, result.outputs);
    out = psp_append_text(out, " pair_mismatches=");
    out = psp_append_u32(out, result.pairMismatches);
    out = psp_append_text(out, " pcm_mismatches=");
    out = psp_append_u32(out, result.outputMismatches);
    out = psp_append_text(out, " state_mismatches=");
    out = psp_append_u32(out, result.stateMismatches);
    out = psp_append_text(out, " skipped=");
    out = psp_append_u32(out, result.skipped);
    if (result.mismatches != 0) {
        out = psp_append_text(out, " lane=");
        out = psp_append_s32(out, result.firstLane);
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " input=");
        out = psp_append_s32(out, result.input);
        out = psp_append_text(out, " coefficient=");
        out = psp_append_s32(out, result.coefficient);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    } else if (result.pairMismatches != 0) {
        out = psp_append_text(out, " first_pair=");
        out = psp_append_s32(out, result.firstPairIndex);
        out = psp_append_text(out, " pair01_expected=");
        out = psp_append_s32(out, result.pair01Expected);
        out = psp_append_text(out, " pair01_actual=");
        out = psp_append_s32(out, result.pair01Actual);
        out = psp_append_text(out, " pair23_expected=");
        out = psp_append_s32(out, result.pair23Expected);
        out = psp_append_text(out, " pair23_actual=");
        out = psp_append_s32(out, result.pair23Actual);
    } else if (result.outputMismatches != 0) {
        out = psp_append_text(out, " first_output=");
        out = psp_append_s32(out, result.firstOutputIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.outputExpected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.outputActual);
    } else if (result.stateMismatches != 0) {
        out = psp_append_text(out, " first_state=");
        out = psp_append_s32(out, result.firstStateIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.stateExpected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.stateActual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeBench(void) {
#if PSP_AUDIO_VME_BENCH
    static u32 sNextReport = 256;
    PspAudioVmeBenchRow rows[PSP_AUDIO_VME_BENCH_ROWS];
    u32 totalCalls = 0;
    u32 i;

    for (i = 0; i < PSP_AUDIO_VME_BENCH_ROWS; i++) {
        PspAudioMe_GetVmeBenchRow(i, &rows[i]);
        totalCalls += rows[i].calls;
    }
    if (totalCalls < sNextReport) {
        return;
    }
    while (sNextReport <= totalCalls) {
        sNextReport <<= 1;
    }
    for (i = 0; i < PSP_AUDIO_VME_BENCH_ROWS; i++) {
        PspAudioVmeBenchRow* row = &rows[i];
        u64 vmeTicks;
        u32 scalarAvg;
        u32 vmeAvg;
        char line[256];
        char* out;

        if ((row->calls == 0) || (row->scalarCalls == 0)) {
            continue;
        }
        vmeTicks = row->stageTicks + row->updateTicks + row->runTicks +
                   row->readbackTicks + row->postTicks;
        scalarAvg = row->scalarTicks / row->scalarCalls;
        vmeAvg = vmeTicks / row->calls;
        out = line;
        out = psp_append_text(out, "[audio-vme-bench] samples=");
        out = psp_append_u32(out, row->samples);
        out = psp_append_text(out, " stage=stores");
        out = psp_append_text(out, " calls=");
        out = psp_append_u32(out, row->calls);
        out = psp_append_text(out, " scalar=");
        out = psp_append_u32(out, scalarAvg);
        out = psp_append_text(out, " staging=");
        out = psp_append_u32(out, row->stageTicks / row->calls);
        out = psp_append_text(out, " update=");
        out = psp_append_u32(out, row->updateTicks / row->calls);
        out = psp_append_text(out, " run=");
        out = psp_append_u32(out, row->runTicks / row->calls);
        out = psp_append_text(out, " readback=");
        out = psp_append_u32(out, row->readbackTicks / row->calls);
        out = psp_append_text(out, " post=");
        out = psp_append_u32(out, row->postTicks / row->calls);
        out = psp_append_text(out, " vme=");
        out = psp_append_u32(out, vmeAvg);
        out = psp_append_text(out, " speed_x1000=");
        out = psp_append_u32(out, vmeAvg ?
            ((u64) scalarAvg * 1000) / vmeAvg : 0);
        *out = '\0';
        PspPlatform_LogAudioVmeLine(line);
    }
#endif
}

#if PSP_AUDIO_VME_BENCH
static void psp_report_audio_vme_resample_transport(
    const char* mode, const PspAudioVmeResampleBenchResult* result) {
    u64 directTicks = result->stageTicks + result->updateTicks +
                      result->runTicks + result->readbackTicks +
                      result->wipeTicks;
    u32 directAvg = directTicks / result->calls;
    char line[352];
    char* out = line;

    out = psp_append_text(out, "[audio-vme-resample-transport] mode=");
    out = psp_append_text(out, mode);
    out = psp_append_text(out, " calls=");
    out = psp_append_u32(out, result->calls);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result->samples / result->calls);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result->mismatches);
    out = psp_append_text(out, " scalar=");
    out = psp_append_u32(out, result->scalarTicks / result->calls);
    out = psp_append_text(out, " staging=");
    out = psp_append_u32(out, result->stageTicks / result->calls);
    out = psp_append_text(out, " update=");
    out = psp_append_u32(out, result->updateTicks / result->calls);
    out = psp_append_text(out, " run=");
    out = psp_append_u32(out, result->runTicks / result->calls);
    out = psp_append_text(out, " readback=");
    out = psp_append_u32(out, result->readbackTicks / result->calls);
    out = psp_append_text(out, " wipe=");
    out = psp_append_u32(out, result->wipeTicks / result->calls);
    out = psp_append_text(out, " direct=");
    out = psp_append_u32(out, directAvg);
    out = psp_append_text(out, " validate=");
    out = psp_append_u32(out, result->validateTicks / result->calls);
    out = psp_append_text(out, " speed_x1000=");
    out = psp_append_u32(out, directTicks ?
        (result->scalarTicks * 1000) / directTicks : 0);
    if (result->mismatches != 0) {
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result->firstMismatch);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result->expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result->actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
}
#endif

void PspPlatform_ReportAudioVmeResampleBatchBench(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeResampleBenchResult storeResult;
    PspAudioVmeResampleBenchResult dmacResult;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeResampleBatchBenchResult(&storeResult);
    PspAudioMe_GetVmeResampleDmacBenchResult(&dmacResult);
    if ((storeResult.calls == 0) || (dmacResult.calls == 0)) {
        return;
    }
    sReported = 1;
    psp_report_audio_vme_resample_transport("stores", &storeResult);
    psp_report_audio_vme_resample_transport("dmac", &dmacResult);
#endif
}

void PspPlatform_ReportAudioVmeTransportBench(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeTransportBenchResult firstResult;
    u32 index;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeTransportBenchResult(0, &firstResult);
    if (firstResult.calls == 0) {
        return;
    }
    sReported = 1;
    for (index = 0; index < PSP_AUDIO_VME_TRANSPORT_BENCH_CASES; index++) {
        PspAudioVmeTransportBenchResult result;
        char line[384];
        char* out = line;

        PspAudioMe_GetVmeTransportBenchResult(index, &result);
        if (result.calls == 0) {
            break;
        }
        out = psp_append_text(out, "[audio-vme-transport] samples=");
        out = psp_append_u32(out, result.samples);
        out = psp_append_text(out, " calls=");
        out = psp_append_u32(out, result.calls);
        out = psp_append_text(out, " mismatches=");
        out = psp_append_u32(out, result.mismatches);
        out = psp_append_text(out, " main_addr=");
        out = psp_append_u32(out, result.mainAddress);
        out = psp_append_text(out, " local_addr=");
        out = psp_append_u32(out, result.localAddress);
        out = psp_append_text(out, " main_to=");
        out = psp_append_u32(out, result.mainToTicks / result.calls);
        out = psp_append_text(out, " local_to=");
        out = psp_append_u32(out, result.localToTicks / result.calls);
        out = psp_append_text(out, " main_from=");
        out = psp_append_u32(out, result.mainFromTicks / result.calls);
        out = psp_append_text(out, " local_from=");
        out = psp_append_u32(out, result.localFromTicks / result.calls);
        if (result.mismatches != 0) {
            out = psp_append_text(out, " domain=");
            out = psp_append_s32(out, result.firstDomain);
            out = psp_append_text(out, " first=");
            out = psp_append_s32(out, result.firstIndex);
            out = psp_append_text(out, " expected=");
            out = psp_append_s32(out, result.expected);
            out = psp_append_text(out, " actual=");
            out = psp_append_s32(out, result.actual);
        }
        *out = '\0';
        PspPlatform_LogAudioVmeLine(line);
    }
#endif
}

void PspPlatform_ReportAudioVmeEnvBoundaryBench(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeEnvBoundaryBenchResult result;
    u64 inclusive;
    char line[512];
    char* out = line;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeEnvBoundaryBenchResult(&result);
    if (result.calls == 0) {
        return;
    }
    sReported = 1;
    inclusive = result.prepareTicks + result.transferInTicks +
        result.setupTicks + result.runTicks + result.transferOutTicks +
        result.materializeTicks;
    out = psp_append_text(out, "[audio-vme-env-boundary] samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " calls=");
    out = psp_append_u32(out, result.calls);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    out = psp_append_text(out, " prepare=");
    out = psp_append_u32(out, result.prepareTicks / result.calls);
    out = psp_append_text(out, " transfer_in=");
    out = psp_append_u32(out, result.transferInTicks / result.calls);
    out = psp_append_text(out, " setup=");
    out = psp_append_u32(out, result.setupTicks / result.calls);
    out = psp_append_text(out, " run=");
    out = psp_append_u32(out, result.runTicks / result.calls);
    out = psp_append_text(out, " transfer_out=");
    out = psp_append_u32(out, result.transferOutTicks / result.calls);
    out = psp_append_text(out, " materialize=");
    out = psp_append_u32(out, result.materializeTicks / result.calls);
    out = psp_append_text(out, " inclusive=");
    out = psp_append_u32(out, inclusive / result.calls);
    out = psp_append_text(out, " target_20pct=10514");
    out = psp_append_text(out, " ideal_routing=excluded");
    if (result.mismatches != 0) {
        out = psp_append_text(out, " output=");
        out = psp_append_s32(out, result.firstOutput);
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeEnvPipeline(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeEnvPipelineResult result;
    char line[512];
    char* out = line;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeEnvPipelineResult(&result);
    if ((result.calls == 0) && (result.seedMismatches == 0) &&
        (result.residentMismatches == 0)) {
        return;
    }
    sReported = 1;
    out = psp_append_text(out, "[audio-vme-env-pipeline] calls=");
    out = psp_append_u32(out, result.calls);
    out = psp_append_text(out, " voices=");
    out = psp_append_u32(out, result.voices);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " seed_mismatches=");
    out = psp_append_u32(out, result.seedMismatches);
    out = psp_append_text(out, " resident_mismatches=");
    out = psp_append_u32(out, result.residentMismatches);
    out = psp_append_text(out, " best_offset=");
    out = psp_append_s32(out, result.bestOffset);
    out = psp_append_text(out, " offset_mismatches=");
    out = psp_append_u32(out, result.offsetMismatches);
    out = psp_append_text(out, " clamp_mismatches=");
    out = psp_append_u32(out, result.clampMismatches);
    out = psp_append_text(out, " clamp_best_offset=");
    out = psp_append_s32(out, result.clampBestOffset);
    out = psp_append_text(out, " clamp_offset_mismatches=");
    out = psp_append_u32(out, result.clampOffsetMismatches);
    out = psp_append_text(out, " clamp_masks=");
    out = psp_append_s32(out, result.clampMaskBestOffset[0]);
    *out++ = '/';
    out = psp_append_u32(out, result.clampMaskOffsetMismatches[0]);
    *out++ = ',';
    out = psp_append_s32(out, result.clampMaskBestOffset[1]);
    *out++ = '/';
    out = psp_append_u32(out, result.clampMaskOffsetMismatches[1]);
    *out++ = ',';
    out = psp_append_s32(out, result.clampMaskBestOffset[2]);
    *out++ = '/';
    out = psp_append_u32(out, result.clampMaskOffsetMismatches[2]);
    *out++ = ',';
    out = psp_append_s32(out, result.clampMaskBestOffset[3]);
    *out++ = '/';
    out = psp_append_u32(out, result.clampMaskOffsetMismatches[3]);
    out = psp_append_text(out, " clamp_pass=");
    out = psp_append_u32(out, result.clampPassingMask);
    out = psp_append_text(out, " dry_calls=");
    out = psp_append_u32(out, result.dryCalls);
    out = psp_append_text(out, " dry_voices=");
    out = psp_append_u32(out, result.dryVoices);
    out = psp_append_text(out, " dry_samples=");
    out = psp_append_u32(out, result.drySamples);
    out = psp_append_text(out, " dry_seed=");
    out = psp_append_u32(out, result.drySeedMismatches[0]);
    *out++ = '/';
    out = psp_append_u32(out, result.drySeedMismatches[1]);
    out = psp_append_text(out, " dry_resident=");
    out = psp_append_u32(out, result.dryResidentMismatches[0]);
    *out++ = '/';
    out = psp_append_u32(out, result.dryResidentMismatches[1]);
    out = psp_append_text(out, " wet_calls=");
    out = psp_append_u32(out, result.wetCalls);
    out = psp_append_text(out, " wet_voices=");
    out = psp_append_u32(out, result.wetVoices);
    out = psp_append_text(out, " wet_samples=");
    out = psp_append_u32(out, result.wetSamples);
    out = psp_append_text(out, " wet_product=");
    out = psp_append_u32(out, result.wetProductMismatches[0]);
    *out++ = '/';
    out = psp_append_u32(out, result.wetProductMismatches[1]);
    out = psp_append_text(out, " wet_seed=");
    out = psp_append_u32(out, result.wetSeedMismatches[0]);
    *out++ = '/';
    out = psp_append_u32(out, result.wetSeedMismatches[1]);
    out = psp_append_text(out, " wet_resident=");
    out = psp_append_u32(out, result.wetResidentMismatches[0]);
    *out++ = '/';
    out = psp_append_u32(out, result.wetResidentMismatches[1]);
    out = psp_append_text(out, " wet_ticks=");
    out = psp_append_u32(out, result.wetCalls ?
        result.wetTicks / result.wetCalls : 0);
    if ((result.seedMismatches + result.residentMismatches +
         result.clampMismatches + result.drySeedMismatches[0] +
         result.drySeedMismatches[1] + result.dryResidentMismatches[0] +
         result.dryResidentMismatches[1] +
         result.wetProductMismatches[0] +
         result.wetProductMismatches[1] + result.wetSeedMismatches[0] +
         result.wetSeedMismatches[1] +
         result.wetResidentMismatches[0] +
         result.wetResidentMismatches[1]) != 0) {
        out = psp_append_text(out, " stage=");
        out = psp_append_s32(out, result.firstStage);
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
        if ((result.drySeedMismatches[0] +
             result.drySeedMismatches[1] +
             result.dryResidentMismatches[0] +
             result.dryResidentMismatches[1]) != 0) {
            out = psp_append_text(out, " dry_first=");
            out = psp_append_s32(out, result.dryFirstLane);
            *out++ = '/';
            out = psp_append_s32(out, result.dryFirstStage);
            *out++ = '/';
            out = psp_append_s32(out, result.dryFirstIndex);
            out = psp_append_text(out, " dry_expected=");
            out = psp_append_s32(out, result.dryExpected);
            out = psp_append_text(out, " dry_actual=");
            out = psp_append_s32(out, result.dryActual);
        }
        if ((result.wetProductMismatches[0] +
             result.wetProductMismatches[1] +
             result.wetSeedMismatches[0] +
             result.wetSeedMismatches[1] +
             result.wetResidentMismatches[0] +
             result.wetResidentMismatches[1]) != 0) {
            out = psp_append_text(out, " wet_first=");
            out = psp_append_s32(out, result.wetFirstLane);
            *out++ = '/';
            out = psp_append_s32(out, result.wetFirstStage);
            *out++ = '/';
            out = psp_append_s32(out, result.wetFirstIndex);
            out = psp_append_text(out, " wet_expected=");
            out = psp_append_s32(out, result.wetExpected);
            out = psp_append_text(out, " wet_actual=");
            out = psp_append_s32(out, result.wetActual);
        }
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeEnvRamp(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeEnvRampResult result;
    u64 inclusive;
    char line[512];
    char* out = line;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeEnvRampResult(&result);
    if ((result.calls == 0) && (result.mismatches == 0)) {
        return;
    }
    sReported = 1;
    inclusive = result.prepareTicks + result.accumulatorInTicks +
        result.inputTicks + result.rampTicks + result.runTicks +
        result.outputTicks + result.materializeTicks;
    out = psp_append_text(out, "[audio-vme-env-ramp] calls=");
    out = psp_append_u32(out, result.calls);
    out = psp_append_text(out, " voices=");
    out = psp_append_u32(out, result.voices);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " mismatches=");
    out = psp_append_u32(out, result.mismatches);
    if (result.calls != 0) {
        out = psp_append_text(out, " prepare=");
        out = psp_append_u32(out, result.prepareTicks / result.calls);
        out = psp_append_text(out, " accum_in=");
        out = psp_append_u32(out, result.accumulatorInTicks / result.calls);
        out = psp_append_text(out, " input=");
        out = psp_append_u32(out, result.inputTicks / result.calls);
        out = psp_append_text(out, " ramps=");
        out = psp_append_u32(out, result.rampTicks / result.calls);
        out = psp_append_text(out, " run=");
        out = psp_append_u32(out, result.runTicks / result.calls);
        out = psp_append_text(out, " output=");
        out = psp_append_u32(out, result.outputTicks / result.calls);
        out = psp_append_text(out, " materialize=");
        out = psp_append_u32(out, result.materializeTicks / result.calls);
        out = psp_append_text(out, " inclusive=");
        out = psp_append_u32(out, inclusive / result.calls);
        out = psp_append_text(out, " validate=");
        out = psp_append_u32(out, result.validateTicks / result.calls);
    }
    if (result.mismatches != 0) {
        out = psp_append_text(out, " stage=");
        out = psp_append_s32(out, result.firstStage);
        out = psp_append_text(out, " lane=");
        out = psp_append_s32(out, result.firstLane);
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeResidentTail(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeResidentTailResult result;
    char line[768];
    char* out = line;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeResidentTailResult(&result);
    if (result.calls == 0) {
        return;
    }
    sReported = 1;
    out = psp_append_text(out, "[audio-vme-resident-tail] calls=");
    out = psp_append_u32(out, result.calls);
    out = psp_append_text(out, " voices=");
    out = psp_append_u32(out, result.voices);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.samples);
    out = psp_append_text(out, " resample_mismatch=");
    out = psp_append_u32(out, result.resampleMismatches);
    out = psp_append_text(out, " state_mismatch=");
    out = psp_append_u32(out, result.stateMismatches);
    out = psp_append_text(out, " accum_mismatch=");
    out = psp_append_u32(out, result.accumulatorMismatches);
    out = psp_append_text(out, " stages=");
    out = psp_append_u32(out, result.dryProductMismatches);
    *out++ = '/';
    out = psp_append_u32(out, result.wetProductMismatches);
    *out++ = '/';
    out = psp_append_u32(out, result.dryAccumulatorMismatches);
    *out++ = '/';
    out = psp_append_u32(out, result.wetAccumulatorMismatches);
    out = psp_append_text(out, " source_prepare=");
    out = psp_append_u32(out, result.sourcePrepareTicks / result.calls);
    out = psp_append_text(out, " coefficient_prepare=");
    out = psp_append_u32(out,
                         result.coefficientPrepareTicks / result.calls);
    out = psp_append_text(out, " resample_stage=");
    out = psp_append_u32(out, result.resampleStageTicks / result.calls);
    out = psp_append_text(out, " resample_setup=");
    out = psp_append_u32(out, result.resampleSetupTicks / result.calls);
    out = psp_append_text(out, " resample_run=");
    out = psp_append_u32(out, result.resampleRunTicks / result.calls);
    out = psp_append_text(out, " accum_in=");
    out = psp_append_u32(out, result.accumulatorInTicks / result.calls);
    out = psp_append_text(out, " ramp=");
    out = psp_append_u32(out, result.rampTicks / result.calls);
    out = psp_append_text(out, " env_setup=");
    out = psp_append_u32(out, result.envSetupTicks / result.calls);
    out = psp_append_text(out, " env_run=");
    out = psp_append_u32(out, result.envRunTicks / result.calls);
    out = psp_append_text(out, " accum_out=");
    out = psp_append_u32(out, result.accumulatorOutTicks / result.calls);
    out = psp_append_text(out, " state=");
    out = psp_append_u32(out, result.stateTicks / result.calls);
    out = psp_append_text(out, " reset=");
    out = psp_append_u32(out, result.resetTicks / result.calls);
    out = psp_append_text(out, " total=");
    out = psp_append_u32(out, result.totalTicks / result.calls);
    out = psp_append_text(out, " validate=");
    out = psp_append_u32(out, result.validateTicks / result.calls);
    if ((result.resampleMismatches + result.stateMismatches +
         result.accumulatorMismatches + result.dryProductMismatches +
         result.wetProductMismatches) != 0) {
        out = psp_append_text(out, " first=");
        out = psp_append_s32(out, result.firstStage);
        *out++ = '/';
        out = psp_append_s32(out, result.firstLane);
        *out++ = '/';
        out = psp_append_s32(out, result.firstIndex);
        out = psp_append_text(out, " expected=");
        out = psp_append_s32(out, result.expected);
        out = psp_append_text(out, " actual=");
        out = psp_append_s32(out, result.actual);
    }
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeEnvRuns(void) {
#if PSP_AUDIO_VME_BENCH
    static u32 sLastJobs;
    static u32 sLastIncompleteCapture;
    static u32 sLastTransferLifetimeStarts;
    static u32 sLastTransferLifetimePhase;
    static u32 sLastTransferLifetimeTransaction;
    PspAudioVmeEnvRunResult result;
    s32 incomplete;
    s32 transferLifetimeIncomplete;
    char line[1024];
    char* out = line;

    PspAudioMe_GetVmeEnvRunResult(&result);
    transferLifetimeIncomplete =
        (result.transferLifetimePhase != 0) &&
        (result.transferLifetimePhase != 12) &&
        (result.transferLifetimePhase != 14);
    if ((result.transferLifetimeStarts != 0) &&
        ((result.transferLifetimeStarts != sLastTransferLifetimeStarts) ||
         (transferLifetimeIncomplete &&
          ((result.transferLifetimePhase != sLastTransferLifetimePhase) ||
           (result.transferLifetimeTransaction !=
            sLastTransferLifetimeTransaction)))) &&
        (transferLifetimeIncomplete ||
         (result.transferLifetimeTransactions ==
          result.transferLifetimeTargetTransactions) ||
         ((result.transferLifetimeTransactions & 7) == 0))) {
        char transferLine[512];
        char* transferOut = transferLine;

        sLastTransferLifetimeStarts = result.transferLifetimeStarts;
        sLastTransferLifetimePhase = result.transferLifetimePhase;
        sLastTransferLifetimeTransaction =
            result.transferLifetimeTransaction;
        transferOut = psp_append_text(
            transferOut, "[audio-vme-env-transfer-life] target=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeTargetTransactions);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeVoices);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeSamples);
        transferOut = psp_append_text(transferOut, " progress=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeTransactions);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeStarts);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeCompletions);
        transferOut = psp_append_text(transferOut, " accumulator=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeAccumulatorStarts);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeAccumulatorCompletions);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeAccumulatorTicks);
        transferOut = psp_append_text(transferOut, " pcm=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimePcmStarts);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimePcmCompletions);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeTicks);
        transferOut = psp_append_text(transferOut, " ramp_ticks=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeRampTicks);
        transferOut = psp_append_text(transferOut, " dry=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeDryRuns);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeContextTicks);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeDryTicks);
        transferOut = psp_append_text(transferOut, " wet=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeWetRuns);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeWetContextTicks);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeWetTicks);
        transferOut = psp_append_text(transferOut, " add=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeAddRuns);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeAddContextTicks);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeAddTicks);
        transferOut = psp_append_text(transferOut, " output=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeOutputStarts);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeOutputCompletions);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeOutputTicks);
        transferOut = psp_append_text(transferOut, " restore=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeRestoreStarts);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeRestores);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeRestoreTicks);
        transferOut = psp_append_text(transferOut, " full_restore=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestoreStarts);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestores);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestoreTicks);
        transferOut = psp_append_text(transferOut, " probe=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestoreProbeStopped);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestoreStep);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestoreProbeTicks);
        transferOut = psp_append_text(transferOut, " checkpoint=");
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimePhase);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeTransaction);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeVoice);
        *transferOut++ = '/';
        transferOut = psp_append_u32(
            transferOut, result.transferLifetimeFullRestoreStep);
        *transferOut = '\0';
        PspPlatform_LogAudioVmeLine(transferLine);
    }
    incomplete = (result.persistentPhase != 0) &&
                 (result.persistentPhase != 12) &&
                 (result.captureRuns != sLastIncompleteCapture);
    if (!incomplete &&
        ((result.jobs < 64) || ((result.jobs - sLastJobs) < 256))) {
        return;
    }
    if (incomplete) {
        sLastIncompleteCapture = result.captureRuns;
    } else {
        sLastJobs = result.jobs;
    }
    out = psp_append_text(out, "[audio-vme-env-runs] jobs=");
    out = psp_append_u32(out, result.jobs);
    out = psp_append_text(out, " commands=");
    out = psp_append_u32(out, result.envCommands);
    out = psp_append_text(out, " runs=");
    out = psp_append_u32(out, result.compatibleRuns);
    out = psp_append_text(out, " voices=");
    out = psp_append_u32(out, result.compatibleVoices);
    out = psp_append_text(out, " samples=");
    out = psp_append_u32(out, result.compatibleSamples);
    out = psp_append_text(out, " max_voices=");
    out = psp_append_u32(out, result.maxVoices);
    out = psp_append_text(out, " flagged=");
    out = psp_append_u32(out, result.flaggedCommands);
    out = psp_append_text(out, " topology_splits=");
    out = psp_append_u32(out, result.destinationBreaks);
    out = psp_append_text(out, " boundary_counts=");
    out = psp_append_u32(out, result.saveBoundaries);
    *out++ = '/';
    out = psp_append_u32(out, result.addBoundaries);
    *out++ = '/';
    out = psp_append_u32(out, result.mixBoundaries);
    *out++ = '/';
    out = psp_append_u32(out, result.clearBoundaries);
    *out++ = '/';
    out = psp_append_u32(out, result.moveBoundaries);
    *out++ = '/';
    out = psp_append_u32(out, result.resampleBoundaries);
    *out++ = '/';
    out = psp_append_u32(out, result.otherBoundaries);
    out = psp_append_text(out, " isolation=");
    out = psp_append_u32(out, result.isolationRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.isolationMismatches);
    out = psp_append_text(out, " segments=");
    out = psp_append_u32(out, result.segments);
    *out++ = '/';
    out = psp_append_u32(out, result.segmentCommands);
    *out++ = '/';
    out = psp_append_u32(out, result.maxSegmentCommands);
    out = psp_append_text(out, " capture=");
    out = psp_append_u32(out, result.captureRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.captureVoices);
    *out++ = '/';
    out = psp_append_u32(out, result.captureBytes);
    *out++ = '/';
    out = psp_append_u32(out, result.captureMismatches);
    out = psp_append_text(out, " capture_range=");
    out = psp_append_u32(out, result.captureMinVoices);
    *out++ = '/';
    out = psp_append_u32(out, result.captureMaxVoices);
    out = psp_append_text(out, " shadow=");
    out = psp_append_u32(out, result.shadowRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowVoices);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowMismatches);
    if (result.shadowMismatches != 0) {
        *out++ = '/';
        out = psp_append_u32(out, result.shadowFirstLane);
        *out++ = '/';
        out = psp_append_u32(out, result.shadowFirstIndex);
        *out++ = '/';
        out = psp_append_s32(out, result.shadowExpected);
        *out++ = '/';
        out = psp_append_s32(out, result.shadowActual);
    }
    out = psp_append_text(out, " timing=");
    out = psp_append_u32(out, result.captureTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.scalarTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowResetTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowStageTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowRunTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowMaterializeTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowValidateTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowTotalTicks);
    out = psp_append_text(out, " restore=");
    out = psp_append_u32(out, result.restorationRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.restorationInterval);
    *out++ = '/';
    out = psp_append_u32(out, result.restorationTicks);
    *out++ = '/';
    out = psp_append_u32(
        out, (result.restorationRuns != 0) &&
                 (result.restorationInterval != 0) ?
            result.restorationTicks /
                (result.restorationRuns * result.restorationInterval) : 0);
    out = psp_append_text(out, " full_restore=");
    out = psp_append_u32(out, result.fullRestorationRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.fullRestorationInterval);
    *out++ = '/';
    out = psp_append_u32(out, result.fullRestorationTicks);
    *out++ = '/';
    out = psp_append_u32(
        out, (result.fullRestorationRuns != 0) &&
                 (result.fullRestorationInterval != 0) ?
            result.fullRestorationTicks /
                (result.fullRestorationRuns *
                 result.fullRestorationInterval) : 0);
    out = psp_append_text(out, " capture_costs=");
    out = psp_append_u32(out, result.captureScanTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.captureAccumulatorTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.capturePcmTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.captureParamTicks);
    out = psp_append_text(out, " vme_costs=");
    out = psp_append_u32(out, result.shadowContextTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowAccumulatorTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowPcmTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowRampTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowRunTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowOutputTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowMaterializeTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.shadowValidateTicks);
    out = psp_append_text(out, " commit=");
    out = psp_append_u32(out, result.commitRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.commitDeclined);
    out = psp_append_text(out, " authority=");
    out = psp_append_u32(out, result.authoritativeRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.authoritativeDeclined);
    *out++ = '/';
    out = psp_append_u32(out, result.fallbackRuns);
    out = psp_append_text(out, " persistent=");
    out = psp_append_u32(out, result.persistentRuns);
    *out++ = '/';
    out = psp_append_u32(out, result.ownershipDeclined);
    *out++ = '/';
    out = psp_append_u32(out, result.notReadyDeclined);
    *out++ = '/';
    out = psp_append_u32(out, result.validatorDeclined);
    *out++ = '/';
    out = psp_append_u32(out, result.persistentCaptureTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.scalarTicks);
    *out++ = '/';
    out = psp_append_u32(out, result.persistentVmeTicks);
    out = psp_append_text(out, " checkpoint=");
    out = psp_append_u32(out, result.persistentPhase);
    *out++ = '/';
    out = psp_append_u32(out, result.persistentVoice);
    out = psp_append_text(out, " transfers=");
    out = psp_append_u32(out, result.accumulatorTransferStarts);
    *out++ = '/';
    out = psp_append_u32(out, result.accumulatorTransferCompletions);
    *out++ = '/';
    out = psp_append_u32(out, result.pcmTransferStarts);
    *out++ = '/';
    out = psp_append_u32(out, result.pcmTransferCompletions);
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_ReportAudioVmeEnvBench(void) {
#if PSP_AUDIO_VME_BENCH
    static s32 sReported;
    PspAudioVmeEnvBenchResult firstResult;
    u32 index;

    if (sReported) {
        return;
    }
    PspAudioMe_GetVmeEnvBenchResult(0, &firstResult);
    if (firstResult.calls == 0) {
        return;
    }
    sReported = 1;
    for (index = 0; index < PSP_AUDIO_VME_ENV_BENCH_CASES; index++) {
        PspAudioVmeEnvBenchResult result;
        char line[768];
        char* out = line;

        PspAudioMe_GetVmeEnvBenchResult(index, &result);
        if (result.calls == 0) {
            break;
        }
        out = psp_append_text(out, "[audio-vme-env-resident] samples=");
        out = psp_append_u32(out, result.samples);
        out = psp_append_text(out, " calls=");
        out = psp_append_u32(out, result.calls);
        out = psp_append_text(out, " outputs=");
        out = psp_append_u32(out, result.samples * 4);
        out = psp_append_text(out, " mismatches=");
        out = psp_append_u32(out, result.mismatches);
        out = psp_append_text(out, " multiply_mismatches=");
        out = psp_append_u32(out, result.multiplyMismatches);
        out = psp_append_text(out, " scalar=");
        out = psp_append_u32(out, result.scalarTicks / result.calls);
        out = psp_append_text(out, " vme=");
        out = psp_append_u32(out, result.vmeTicks / result.calls);
        out = psp_append_text(out, " validate=");
        out = psp_append_u32(out, result.validateTicks / result.calls);
        out = psp_append_text(out, " speed_x1000=");
        out = psp_append_u32(out, result.vmeTicks ?
            (result.scalarTicks * 1000) / result.vmeTicks : 0);
        if (result.mismatches != 0) {
            out = psp_append_text(out, " output=");
            out = psp_append_s32(
                out, result.firstMismatch / PSP_AUDIO_VME_ENV_MAX_SAMPLES);
            out = psp_append_text(out, " first=");
            out = psp_append_s32(
                out, result.firstMismatch % PSP_AUDIO_VME_ENV_MAX_SAMPLES);
            out = psp_append_text(out, " expected=");
            out = psp_append_s32(out, result.expected);
            out = psp_append_text(out, " actual=");
            out = psp_append_s32(out, result.actual);
            out = psp_append_text(out, " best_offset=");
            out = psp_append_s32(out, result.bestOffset);
            out = psp_append_text(out, " offset_mismatches=");
            out = psp_append_u32(out, result.offsetMismatches);
        }
        if (result.multiplyMismatches != 0) {
            out = psp_append_text(out, " multiply_output=");
            out = psp_append_s32(out, result.multiplyFirstMismatch /
                PSP_AUDIO_VME_ENV_MAX_SAMPLES);
            out = psp_append_text(out, " multiply_first=");
            out = psp_append_s32(out, result.multiplyFirstMismatch %
                PSP_AUDIO_VME_ENV_MAX_SAMPLES);
            out = psp_append_text(out, " multiply_expected=");
            out = psp_append_s32(out, result.multiplyExpected);
            out = psp_append_text(out, " multiply_actual=");
            out = psp_append_s32(out, result.multiplyActual);
        }
        *out = '\0';
        PspPlatform_LogAudioVmeLine(line);
    }
#endif
}

void PspPlatform_ReportAudioVmeResampleBench(void) {
#if PSP_AUDIO_VME_BENCH
    static u32 sNextReport = 32;
    PspAudioVmeResampleBenchResult result;
    u64 directTicks;
    u64 shadowTicks;
    u32 scalarAvg;
    u32 directAvg;
    char line[384];
    char* out;

    PspAudioMe_GetVmeResampleBenchResult(&result);
    if (result.calls < sNextReport) {
        return;
    }
    while (sNextReport <= result.calls) {
        sNextReport <<= 1;
    }
    directTicks = result.prepareTicks + result.stageTicks +
                  result.updateTicks + result.runTicks +
                  result.readbackTicks + result.wipeTicks;
    shadowTicks = directTicks + result.pairTicks + result.validateTicks;
    scalarAvg = result.scalarTicks / result.calls;
    directAvg = directTicks / result.calls;
    out = line;
    out = psp_append_text(out, "[audio-vme-resample-bench] calls=");
    out = psp_append_u32(out, result.calls);
    out = psp_append_text(out, " avg_samples=");
    out = psp_append_u32(out, result.samples / result.calls);
    out = psp_append_text(out, " scalar=");
    out = psp_append_u32(out, scalarAvg);
    out = psp_append_text(out, " prepare=");
    out = psp_append_u32(out, result.prepareTicks / result.calls);
    out = psp_append_text(out, " staging=");
    out = psp_append_u32(out, result.stageTicks / result.calls);
    out = psp_append_text(out, " update=");
    out = psp_append_u32(out, result.updateTicks / result.calls);
    out = psp_append_text(out, " run=");
    out = psp_append_u32(out, result.runTicks / result.calls);
    out = psp_append_text(out, " readback=");
    out = psp_append_u32(out, result.readbackTicks / result.calls);
    out = psp_append_text(out, " wipe=");
    out = psp_append_u32(out, result.wipeTicks / result.calls);
    out = psp_append_text(out, " direct=");
    out = psp_append_u32(out, directAvg);
    out = psp_append_text(out, " pair_debug=");
    out = psp_append_u32(out, result.pairTicks / result.calls);
    out = psp_append_text(out, " validate=");
    out = psp_append_u32(out, result.validateTicks / result.calls);
    out = psp_append_text(out, " shadow=");
    out = psp_append_u32(out, shadowTicks / result.calls);
    out = psp_append_text(out, " speed_x1000=");
    out = psp_append_u32(out, directAvg ?
        ((u64) scalarAvg * 1000) / directAvg : 0);
    *out = '\0';
    PspPlatform_LogAudioVmeLine(line);
#endif
}

void PspPlatform_LogFrame(const char* phase, u32 frame) {
    char line[96];
    char* out = line;

    out = psp_append_text(out, "[psp] frame ");
    out = psp_append_u32(out, frame);
    out = psp_append_text(out, ": ");
    out = psp_append_text(out, phase);
    *out = '\0';
    PspPlatform_LogLine(line);
}

static int psp_vi_thread(SceSize args, void* argp) {
    (void) args;
    (void) argp;

    while (!sExitRequested) {
        // game already divides frame pacing with gVIsPerFrame
        sceDisplayWaitVblankStart();
        PspPlatform_PostViEvent();
    }

    return 0;
}

void PspPlatform_Init(void) {
#if PSP_AUDIO
    int audioMeResult;
#endif
    int audioResult;

    sExitRequested = 0;
    sViEventPending = 0;
#if PSP_FILE_LOG_ENABLED
    if (sLogSemaId < 0) {
        sLogSemaId = sceKernelCreateSema("sf64_log", 0, 1, 1, NULL);
    }
    sceIoRemove(psp_log_path());
#endif
#if PSP_LOG_ENABLED
    PspPlatform_LogLine("[psp] log start");
#endif
#if PSP_AUDIO_PROFILE
    PspPlatform_LogAudioProfileLine("[audio-prof] log start cache=targeted");
#endif
#if PSP_AUDIO_VME
    PspPlatform_LogAudioVmeLine("[audio-vme] log start");
#endif
    PspInput_Init();
    PspProfiler_Init();
#if PSP_AUDIO
    audioMeResult = PspAudioMe_Init();
#if PSP_AUDIO_VME
    psp_log_audio_vme_result();
#if PSP_AUDIO_VME_VALIDATE
    PspPlatform_ReportAudioVmeMix();
    PspPlatform_ReportAudioVmeFilter();
    PspPlatform_ReportAudioVmeResample();
    PspPlatform_ReportAudioVmeAdpcmAudit();
#endif
#endif
#endif
    audioResult = PspAudioOutput_Init();
#if PSP_LOG_ENABLED
    if (audioResult < 0) {
        PspPlatform_LogLine("[psp-audio] output initialization failed");
    } else {
#if PSP_AUDIO
        if (PspAudioMe_IsActive()) {
            PspPlatform_LogLine("[psp-audio] Media Engine scalar backend, 32000 Hz stereo");
        } else {
            PspPlatform_LogLine("[psp-audio] Allegrex scalar fallback, 32000 Hz stereo");
            PspPlatform_LogValue("audio Media Engine error", (u32) audioMeResult);
        }
#else
        PspPlatform_LogLine("[psp-audio] disabled");
#endif
    }
#else
#if PSP_AUDIO
    (void) audioMeResult;
#endif
    (void) audioResult;
#endif
    PspRenderer_Init();

    if (sViThreadId < 0) {
        sViThreadId = sceKernelCreateThread("sf64_vi", psp_vi_thread, 0x12, 0x1000, 0, NULL);
        if (sViThreadId >= 0) {
            sceKernelStartThread(sViThreadId, 0, NULL);
        }
    }
}

void PspPlatform_RequestExit(void) {
    sExitRequested = 1;
}

void PspPlatform_SetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {
    if (event < OS_NUM_EVENTS) {
        sEvents[event].mq = mq;
        sEvents[event].msg = msg;
    }
}

void PspPlatform_SetViEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    int intrState;

    intrState = sceKernelCpuSuspendIntr();
    sViMq = mq;
    sViMsg = msg;
    sViRetraceCount = retraceCount == 0 ? 1 : retraceCount;
    sViEventPending = 0;
    sceKernelCpuResumeIntr(intrState);
}

void PspPlatform_PostViEvent(void) {
    int intrState;
    int shouldPost;
    s32 result;

    shouldPost = 0;
    sViCount++;

    if ((sViMq != NULL) && ((sViCount % sViRetraceCount) == 0)) {
        intrState = sceKernelCpuSuspendIntr();

        if (!sViEventPending) {
            sViEventPending = 1;
            shouldPost = 1;
        }

        sceKernelCpuResumeIntr(intrState);

        if (shouldPost) {
            result = osSendMesg(sViMq, sViMsg, OS_MESG_NOBLOCK);

            if (result != 0) {
                intrState = sceKernelCpuSuspendIntr();
                sViEventPending = 0;
                sceKernelCpuResumeIntr(intrState);

#if PSP_LOG_ENABLED
                PspPlatform_LogLine("[psp-vi] VI enqueue failed");
#endif
            }
        }
    }

    PspPlatform_DebugFrame();
}

void PspPlatform_AcknowledgeViEvent(void) {
    int intrState;

    intrState = sceKernelCpuSuspendIntr();
    sViEventPending = 0;
    sceKernelCpuResumeIntr(intrState);
}

void PspPlatform_PollInput(OSContPad* pads) {
    if (PspProfiler_ExitRequested() || PspInput_Poll(pads)) {
        PspPlatform_RequestExit();
        PspProfiler_Shutdown();
        sceKernelExitGame();
    }
}

void PspPlatform_RunGfxTask(SPTask* task) {
#if PSP_LOG_ENABLED
    s32 result;
#endif

    sGfxTaskCount++;

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_GFX_TASK);
    PspRenderer_RenderGfxTask(task, sGfxTaskCount);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_GFX_TASK);
    PspProfiler_OnGfxTaskComplete();

#if PSP_LOG_ENABLED
    if ((sGfxTaskCount <= 4) || ((sGfxTaskCount % 30) == 0)) {
        PspPlatform_LogFrame("gfx task complete", sGfxTaskCount);
    }
#endif

    if (sEvents[OS_EVENT_SP].mq != NULL) {
#if PSP_LOG_ENABLED
        result = osSendMesg(sEvents[OS_EVENT_SP].mq,
                            sEvents[OS_EVENT_SP].msg,
                            OS_MESG_NOBLOCK);
        if (result != 0) {
            PspPlatform_LogLine("[psp-gfx] SP event enqueue failed");
        }
#else
        osSendMesg(sEvents[OS_EVENT_SP].mq, sEvents[OS_EVENT_SP].msg, OS_MESG_NOBLOCK);
#endif
    }

    if (sEvents[OS_EVENT_DP].mq != NULL) {
#if PSP_LOG_ENABLED
        result = osSendMesg(sEvents[OS_EVENT_DP].mq,
                            sEvents[OS_EVENT_DP].msg,
                            OS_MESG_NOBLOCK);
        if (result != 0) {
            PspPlatform_LogLine("[psp-gfx] DP event enqueue failed");
        }
#else
        osSendMesg(sEvents[OS_EVENT_DP].mq, sEvents[OS_EVENT_DP].msg, OS_MESG_NOBLOCK);
#endif
    }
}

void PspPlatform_RunAudioTask(SPTask* task) {
    sAudioTaskCount++;
    (void) task;
    // PSP audio ABI macros execute inline during synthesis
    // The scalar task only acknowledges completion
    if (sEvents[OS_EVENT_SP].mq != NULL) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_AUDIO_TASK_DISPATCH);
        osSendMesg(sEvents[OS_EVENT_SP].mq, sEvents[OS_EVENT_SP].msg, OS_MESG_NOBLOCK);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_AUDIO_TASK_DISPATCH);
    }
}

void PspPlatform_DebugFrame(void) {
#if PSP_DEBUG_OVERLAY_ENABLED
    static u32 sLastFrame;
    static u32 sLastLoggedFrame;

    if (gSysFrameCount != sLastFrame) {
        sLastFrame = gSysFrameCount;
        pspDebugScreenSetXY(0, 4);
        pspDebugScreenPrintf("frames %lu  vi %lu  gfx %lu  aud %lu   ",
                             (unsigned long) gSysFrameCount,
                             (unsigned long) sViCount,
                             (unsigned long) sGfxTaskCount,
                             (unsigned long) sAudioTaskCount);

#if PSP_LOG_ENABLED
        if ((gSysFrameCount <= 4) || ((gSysFrameCount - sLastLoggedFrame) >= 30)) {
            sLastLoggedFrame = gSysFrameCount;
            PspPlatform_LogFrame("heartbeat", gSysFrameCount);
        }
#else
        (void) sLastLoggedFrame;
#endif
    }
#endif
    PspProfiler_DrawStatus();
}

void PspPlatform_LogValue(const char* label, u32 value) {
    char line[96];
    char* out = line;

    out = psp_append_text(out, "[psp] ");
    out = psp_append_text(out, label);
    out = psp_append_text(out, ": ");
    out = psp_append_u32(out, value);
    *out = '\0';

    PspPlatform_LogLine(line);
}



void Mio0_Decompress(void* header, u8* dst) {
    (void) header;
    (void) dst;
}

f32 guSqrtf(f32 x) {
    return sqrtf(x);
}

void RdRam_CheckIPL3(void) {
}

OSThread* __osGetActiveQueue(void) {
    return NULL;
}

s32 osMotorInit(OSMesgQueue* mq, OSPfs* pfs, int channel) {
    (void) mq;
    (void) pfs;
    (void) channel;
    return -1;
}

s32 osMotorStart(OSPfs* pfs) {
    (void) pfs;
    return 0;
}

s32 osMotorStop(OSPfs* pfs) {
    (void) pfs;
    return 0;
}
