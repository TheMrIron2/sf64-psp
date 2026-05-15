#include "PR/ultratypes.h"
#include "PR/mbi.h"
#include "PR/os.h"
#include "PR/os_cont.h"
#include "PR/os_eeprom.h"
#include "PR/ucode.h"
#include "sf64dma.h"
#include "src/psp/platform.h"

#define PSP_CTRL_SELECT 0x000001
#define PSP_CTRL_START 0x000008
#define PSP_CTRL_UP 0x000010
#define PSP_CTRL_RIGHT 0x000020
#define PSP_CTRL_DOWN 0x000040
#define PSP_CTRL_LEFT 0x000080
#define PSP_CTRL_LTRIGGER 0x000100
#define PSP_CTRL_RTRIGGER 0x000200
#define PSP_CTRL_TRIANGLE 0x001000
#define PSP_CTRL_CIRCLE 0x002000
#define PSP_CTRL_CROSS 0x004000
#define PSP_CTRL_SQUARE 0x008000
#define PSP_CTRL_MODE_ANALOG 1

typedef int SceUID;
typedef unsigned int SceSize;

typedef struct SceCtrlData {
    u32 TimeStamp;
    u32 Buttons;
    u8 Lx;
    u8 Ly;
    u8 Rsrv[6];
} SceCtrlData;

int sceCtrlSetSamplingCycle(int cycle);
int sceCtrlSetSamplingMode(int mode);
int sceCtrlReadBufferPositive(SceCtrlData* pad_data, int count);
int sceDisplayWaitVblankStart(void);
SceUID sceKernelCreateThread(const char* name, int (*entry)(SceSize, void*), int initPriority, int stackSize,
                             int attr, void* option);
int sceKernelStartThread(SceUID thid, SceSize arglen, void* argp);
void sceKernelExitGame(void);
void pspDebugScreenSetXY(int x, int y);
void pspDebugScreenPrintf(const char* fmt, ...);
float sqrtf(float x);

#define PSP_VI_PER_FRAME 2

typedef struct {
    OSMesgQueue* mq;
    OSMesg msg;
} PspEvent;

static PspEvent sEvents[OS_NUM_EVENTS];
static OSMesgQueue* sViMq;
static OSMesg sViMsg;
static u32 sViRetraceCount = 1;
static SceUID sViThreadId = -1;
static volatile int sExitRequested;
static u32 sGfxTaskCount;
static u32 sAudioTaskCount;
static u32 sViCount;

u32 osMemSize = 24 * 1024 * 1024;
s32 osTvType = OS_TV_NTSC;
f32 gDefaultSfxSource[3] = { 0.0f, 0.0f, 0.0f };
f32 gDefaultMod = 1.0f;
s8 gDefaultReverb = 0;

long long int rspbootTextStart[1], rspbootTextEnd[1];
long long int gspF3DEX_fifoTextStart[1], gspF3DEX_fifoTextEnd[1];
long long int gspF3DEX_fifoDataStart[1], gspF3DEX_fifoDataEnd[1];

#define PSP_EMPTY_SEGMENT(name) \
    u8 name##_ROM_START[1], name##_ROM_END[1]; \
    u8 name##_VRAM[1], name##_VRAM_END[1]; \
    u8 name##_TEXT_START[1], name##_TEXT_END[1]; \
    u8 name##_DATA_START[1], name##_DATA_END[1], name##_DATA_SIZE[1]; \
    u8 name##_RODATA_START[1], name##_RODATA_END[1]; \
    u8 name##_BSS_START[1], name##_BSS_END[1]

PSP_EMPTY_SEGMENT(makerom);
PSP_EMPTY_SEGMENT(main);
PSP_EMPTY_SEGMENT(dma_table);
PSP_EMPTY_SEGMENT(audio_seq);
PSP_EMPTY_SEGMENT(audio_bank);
PSP_EMPTY_SEGMENT(audio_table);
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

static int psp_vi_thread(SceSize args, void* argp) {
    (void) args;
    (void) argp;

    while (!sExitRequested) {
        sceDisplayWaitVblankStart();
        sceDisplayWaitVblankStart();
        PspPlatform_PostViEvent();
    }

    return 0;
}

static void psp_map_buttons(SceCtrlData* in, OSContPad* out) {
    out->button = 0;
    out->stick_x = (s8) (((s32) in->Lx - 128) * 80 / 128);
    out->stick_y = (s8) ((128 - (s32) in->Ly) * 80 / 128);
    out->errno = 0;

    if (in->Buttons & PSP_CTRL_CROSS) {
        out->button |= A_BUTTON;
    }
    if (in->Buttons & PSP_CTRL_CIRCLE) {
        out->button |= B_BUTTON;
    }
    if (in->Buttons & PSP_CTRL_SQUARE) {
        out->button |= Z_TRIG;
    }
    if (in->Buttons & PSP_CTRL_LTRIGGER) {
        out->button |= L_TRIG;
    }
    if (in->Buttons & PSP_CTRL_RTRIGGER) {
        out->button |= R_TRIG;
    }
    if (in->Buttons & PSP_CTRL_START) {
        out->button |= START_BUTTON;
    }
    if (in->Buttons & PSP_CTRL_UP) {
        out->button |= U_JPAD;
    }
    if (in->Buttons & PSP_CTRL_DOWN) {
        out->button |= D_JPAD;
    }
    if (in->Buttons & PSP_CTRL_LEFT) {
        out->button |= L_JPAD;
    }
    if (in->Buttons & PSP_CTRL_RIGHT) {
        out->button |= R_JPAD;
    }
    if (in->Buttons & PSP_CTRL_TRIANGLE) {
        out->button |= U_CBUTTONS;
    }
}

void PspPlatform_Init(void) {
    sExitRequested = 0;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

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
    sViMq = mq;
    sViMsg = msg;
    sViRetraceCount = retraceCount == 0 ? 1 : retraceCount;
}

void PspPlatform_PostViEvent(void) {
    sViCount++;
    if ((sViMq != NULL) && ((sViCount % sViRetraceCount) == 0)) {
        osSendMesg(sViMq, sViMsg, OS_MESG_NOBLOCK);
    }
    PspPlatform_DebugFrame();
}

void PspPlatform_PollInput(OSContPad* pads) {
    SceCtrlData pad;
    s32 i;

    sceCtrlReadBufferPositive(&pad, 1);
    for (i = 0; i < MAXCONTROLLERS; i++) {
        pads[i].button = 0;
        pads[i].stick_x = 0;
        pads[i].stick_y = 0;
        pads[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }

    psp_map_buttons(&pad, &pads[0]);

    if ((pad.Buttons & PSP_CTRL_SELECT) && (pad.Buttons & PSP_CTRL_START)) {
        PspPlatform_RequestExit();
        sceKernelExitGame();
    }
}

void PspPlatform_RunGfxTask(SPTask* task) {
    sGfxTaskCount++;
    (void) task;
    if (sEvents[OS_EVENT_SP].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_SP].mq, sEvents[OS_EVENT_SP].msg, OS_MESG_NOBLOCK);
    }
    if (sEvents[OS_EVENT_DP].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_DP].mq, sEvents[OS_EVENT_DP].msg, OS_MESG_NOBLOCK);
    }
}

void PspPlatform_RunAudioTask(SPTask* task) {
    sAudioTaskCount++;
    (void) task;
    if (sEvents[OS_EVENT_SP].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_SP].mq, sEvents[OS_EVENT_SP].msg, OS_MESG_NOBLOCK);
    }
}

void PspPlatform_DebugFrame(void) {
    static u32 sLastFrame;

    if (gSysFrameCount != sLastFrame) {
        sLastFrame = gSysFrameCount;
        pspDebugScreenSetXY(0, 4);
        pspDebugScreenPrintf("frames %lu  vi %lu  gfx %lu  aud %lu   ",
                             (unsigned long) gSysFrameCount,
                             (unsigned long) sViCount,
                             (unsigned long) sGfxTaskCount,
                             (unsigned long) sAudioTaskCount);
    }
}

void AudioLoad_Init(void) {
}

void Audio_dummy_80016A50(void) {
}

void Audio_InitSounds(void) {
}

void Audio_Update(void) {
}

SPTask* AudioThread_CreateTask(void) {
    return NULL;
}

void AudioThread_PreNMIReset(void) {
}

void Audio_PlayVoice(s32 msgId) {
    (void) msgId;
}

void Audio_PlayVoiceWithoutBGM(u32 msgId) {
    (void) msgId;
}

void Audio_ClearVoice(void) {
}

s32 Audio_GetCurrentVoice(void) {
    return -1;
}

s32 Audio_GetCurrentVoiceStatus(void) {
    return 0;
}

void Audio_SetUnkVoiceParam(u8 unkVoiceParam) {
    (void) unkVoiceParam;
}

u8* Audio_UpdateFrequencyAnalysis(void) {
    static u8 sSilentAnalysis[0x40];
    return sSilentAnalysis;
}

void Audio_SetVolume(u8 audioType, u8 volume) {
    (void) audioType;
    (void) volume;
}

void Audio_FadeOutAll(u8 fadeoutTime) {
    (void) fadeoutTime;
}

void Audio_SetAudioSpec(u8 unused, u16 specParam) {
    (void) unused;
    (void) specParam;
}

void Audio_SetBgmParam(s8 bgmParam) {
    (void) bgmParam;
}

void Audio_PlaySequence(u8 seqPlayId, u16 seqId, u8 fadeinTime, u8 bgmParam) {
    (void) seqPlayId;
    (void) seqId;
    (void) fadeinTime;
    (void) bgmParam;
}

void Audio_PlayFanfare(u16 seqId, u8 bgmVolume, u8 bgmFadeoutTime, u8 bgmFadeinTime) {
    (void) seqId;
    (void) bgmVolume;
    (void) bgmFadeoutTime;
    (void) bgmFadeinTime;
}

void Audio_PlayDeathSequence(void) {
}

void Audio_PlaySoundTest(u8 enable) {
    (void) enable;
}

void Audio_PlaySequenceDistorted(u8 seqPlayId, u16 seqId, u16 distortion, u8 fadeinTime, u8 unused) {
    (void) seqPlayId;
    (void) seqId;
    (void) distortion;
    (void) fadeinTime;
    (void) unused;
}

void Audio_PlaySoundTestTrack(u8 trackNumber) {
    (void) trackNumber;
}

void Audio_PlayBgm(u16 seqId) {
    (void) seqId;
}

void Audio_QueueSeqCmd(s32 cmd) {
    (void) cmd;
}

void Audio_PlaySfx(u32 sfxId, f32* sfxSource, u8 token, f32* freqMod, f32* volMod, s8* reverbAdd) {
    (void) sfxId;
    (void) sfxSource;
    (void) token;
    (void) freqMod;
    (void) volMod;
    (void) reverbAdd;
}

void Audio_KillSfxByBank(u8 bankId) {
    (void) bankId;
}

void Audio_StopSfxByBankAndSource(u8 bankId, f32* sfxSource) {
    (void) bankId;
    (void) sfxSource;
}

void Audio_KillSfxByBankAndSource(u8 bankId, f32* sfxSource) {
    (void) bankId;
    (void) sfxSource;
}

void Audio_KillSfxBySource(f32* sfxSource) {
    (void) sfxSource;
}

void Audio_KillSfxBySourceAndId(f32* sfxSource, u32 sfxId) {
    (void) sfxSource;
    (void) sfxId;
}

void Audio_KillSfxByTokenAndId(u8 token, u32 sfxId) {
    (void) token;
    (void) sfxId;
}

void Audio_KillSfxById(u32 sfxId) {
    (void) sfxId;
}

void Audio_StartPlayerNoise(u8 playerId) {
    (void) playerId;
}

void Audio_StopPlayerNoise(u8 playerId) {
    (void) playerId;
}

void Audio_InitBombSfx(u8 playerId, u8 type) {
    (void) playerId;
    (void) type;
}

void Audio_PlayBombFlightSfx(u8 playerId, f32* sfxSource) {
    (void) playerId;
    (void) sfxSource;
}

void Audio_PlayBombExplodeSfx(u8 playerId, f32* sfxSource) {
    (void) playerId;
    (void) sfxSource;
}

void Audio_StopEngineNoise(f32* sfxSource) {
    (void) sfxSource;
}

void Audio_SetSfxSpeedModulation(f32 vel) {
    (void) vel;
}

void Audio_SetTransposeAndPlaySfx(f32* sfxSource, u32 sfxId, u8 semitones) {
    (void) sfxSource;
    (void) sfxId;
    (void) semitones;
}

void Audio_SetModulationAndPlaySfx(f32* sfxSource, u32 sfxId, f32 freqMod) {
    (void) sfxSource;
    (void) sfxId;
    (void) freqMod;
}

void Audio_PlaySfxModulated(f32* sfxSource, u32 sfxId) {
    (void) sfxSource;
    (void) sfxId;
}

void Audio_SetSfxMapModulation(u8 fMod) {
    (void) fMod;
}

void Audio_SetHeatAlarmParams(u8 shields, u8 heightParam) {
    (void) shields;
    (void) heightParam;
}

void Audio_PlayEventSfx(f32* sfxSource, u16 eventSfxId) {
    (void) sfxSource;
    (void) eventSfxId;
}

void Audio_StopEventSfx(f32* sfxSource, u16 eventSfxId) {
    (void) sfxSource;
    (void) eventSfxId;
}

void Audio_SetEnvSfxReverb(s8 reverb) {
    (void) reverb;
}

void Audio_PlayPauseSfx(u8 active) {
    (void) active;
}

void Audio_PlayMapMenuSfx(u8 active) {
    (void) active;
}

void Audio_KillAllSfx(void) {
}

void Audio_RestartSeqPlayers(void) {
}

void Audio_StartReset(void) {
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
