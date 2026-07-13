#include "PR/ultratypes.h"
#include "PR/mbi.h"
#include "PR/os.h"
#include "PR/os_cont.h"
#include "PR/os_eeprom.h"
#include "PR/ucode.h"
#include "sf64dma.h"
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

#define PSP_LOG_PATH "ms0:/sf64_psp.log"
#ifndef PSP_LOG_ENABLED
#define PSP_LOG_ENABLED 0
#endif
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
#if PSP_LOG_ENABLED
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

#if PSP_LOG_ENABLED
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

void PspPlatform_LogLine(const char* line) {
#if PSP_LOG_ENABLED
    SceUID fd;
    static int sLogReady;

    if (line == NULL) {
        return;
    }

    if (sLogSemaId >= 0) {
        sceKernelWaitSema(sLogSemaId, 1, NULL);
    }

    fd = sceIoOpen(PSP_LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        if (!sLogReady) {
            sLogReady = 1;
        }
        sceIoWrite(fd, line, psp_strlen(line));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    if (sLogSemaId >= 0) {
        sceKernelSignalSema(sLogSemaId, 1);
    }
#else
    (void) line;
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
    int audioResult;

    sExitRequested = 0;
    sViEventPending = 0;
#if PSP_LOG_ENABLED
    if (sLogSemaId < 0) {
        sLogSemaId = sceKernelCreateSema("sf64_log", 0, 1, 1, NULL);
    }
    sceIoRemove(PSP_LOG_PATH);
    PspPlatform_LogLine("[psp] log start");
#endif
    PspInput_Init();
    PspProfiler_Init();
    audioResult = PspAudioOutput_Init();
#if PSP_LOG_ENABLED
    if (audioResult < 0) {
        PspPlatform_LogLine("[psp-audio] output initialization failed");
    } else {
        PspPlatform_LogLine("[psp-audio] scalar software backend, 32000 Hz stereo");
    }
#else
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
    s32 result;

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
        result = osSendMesg(sEvents[OS_EVENT_SP].mq,
                            sEvents[OS_EVENT_SP].msg,
                            OS_MESG_NOBLOCK);
#if PSP_LOG_ENABLED
        if (result != 0) {
            PspPlatform_LogLine("[psp-gfx] SP event enqueue failed");
        }
#endif
    }

    if (sEvents[OS_EVENT_DP].mq != NULL) {
        result = osSendMesg(sEvents[OS_EVENT_DP].mq,
                            sEvents[OS_EVENT_DP].msg,
                            OS_MESG_NOBLOCK);
#if PSP_LOG_ENABLED
        if (result != 0) {
            PspPlatform_LogLine("[psp-gfx] DP event enqueue failed");
        }
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
