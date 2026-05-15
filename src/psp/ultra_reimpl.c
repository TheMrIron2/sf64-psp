// adapted from sf64-dc by jnmartin84
// https://github.com/jnmartin84/sf64-dc/blob/main/src/ultra_reimpl.c

#include "PR/os.h"
#include "PR/os_internal.h"
#include "PR/os_message.h"
#include "PR/os_time.h"
#include "PR/os_thread.h"
#include "PR/os_cache.h"
#include "PR/os_ai.h"
#include "PR/os_eeprom.h"
#include "PR/os_cont.h"
#include "PR/os_pi.h"
#include "PR/os_rdp.h"
#include "PR/os_rsp.h"
#include "PR/os_vi.h"
#include "PR/sptask.h"
#include "macros.h"
#include "src/psp/platform.h"

#include <pspkernel.h>
#include <psprtc.h>

#define PSP_N64_TICKS_PER_SECOND 46875000ULL
#define PSP_TIMER_POOL_SIZE 32

u64 osClockRate = PSP_N64_TICKS_PER_SECOND;

typedef struct PspTimer {
    OSTimer* owner;
    SceUID threadId;
    OSTime countdown;
    OSTime interval;
    OSMesgQueue* mq;
    OSMesg msg;
    volatile int active;
} PspTimer;

typedef struct PspThread {
    OSThread* owner;
    SceUID threadId;
    void (*entry)(void*);
    void* arg;
    volatile int active;
} PspThread;

static PspTimer sTimers[PSP_TIMER_POOL_SIZE];
static PspThread sThreads[16];
static OSTask* sLoadedTask;

extern float floorf(float);
extern float ceilf(float);
extern float nearbyintf(float);
extern float truncf(float);
extern float roundf(float);

f32 __floorf(f32 x) {
    return floorf(x);
}

f32 __ceilf(f32 x) {
    return ceilf(x);
}

f32 __nearbyintf(f32 x) {
    return nearbyintf(x);
}

f32 __truncf(f32 x) {
    return truncf(x);
}

f32 __roundf(f32 x) {
    return roundf(x);
}

static void psp_memzero(void* ptr, u32 size) {
    u8* bytes = (u8*) ptr;

    while (size-- != 0) {
        *bytes++ = 0;
    }
}

static void psp_memcpy(void* dst, const void* src, u32 size) {
    u8* d = (u8*) dst;
    const u8* s = (const u8*) src;

    while (size-- != 0) {
        *d++ = *s++;
    }
}

static u32 ticks_to_usecs(OSTime ticks) {
    return (u32) ((ticks * 1000000ULL) / PSP_N64_TICKS_PER_SECOND);
}

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count) {
    mq->mtqueue = NULL;
    mq->fullqueue = NULL;
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msgBuf;
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    s32 index;

    if (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) {
            return -1;
        }
        while (mq->validCount >= mq->msgCount) {
            sceKernelDelayThread(1000);
        }
    }

    index = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[index] = msg;
    mq->validCount++;

    return 0;
}

s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    s32 i;

    if (mq->validCount >= mq->msgCount) {
        if (flag != OS_MESG_BLOCK) {
            return -1;
        }
        while (mq->validCount >= mq->msgCount) {
            sceKernelDelayThread(1000);
        }
    }

    mq->first--;
    if (mq->first < 0) {
        mq->first = mq->msgCount - 1;
    }
    for (i = mq->validCount; i > 0; i--) {
        mq->msg[(mq->first + i) % mq->msgCount] = mq->msg[(mq->first + i - 1) % mq->msgCount];
    }
    mq->msg[mq->first] = msg;
    mq->validCount++;

    return 0;
}

s32 osSendMesgNoBlock(OSMesgQueue* mq, OSMesg msg) {
    if (mq == NULL) {
        return -1;
    }
    return osSendMesg(mq, msg, OS_MESG_NOBLOCK);
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    while (mq->validCount == 0) {
        if (flag != OS_MESG_BLOCK) {
            return -1;
        }
        sceKernelDelayThread(1000);
    }

    if (msg != NULL) {
        *msg = mq->msg[mq->first];
    }

    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    return 0;
}

static int psp_timer_thread(SceSize args, void* argp) {
    PspTimer* timer = (PspTimer*) argp;

    (void) args;

    while (timer->active) {
        sceKernelDelayThread(ticks_to_usecs(timer->countdown));
        if (!timer->active) {
            break;
        }
        if (timer->mq != NULL) {
            osSendMesg(timer->mq, timer->msg, OS_MESG_NOBLOCK);
        }
        if (timer->interval == 0) {
            timer->active = 0;
        } else {
            timer->countdown = timer->interval;
        }
    }

    return 0;
}

int osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval, OSMesgQueue* mq, OSMesg msg) {
    s32 i;

    for (i = 0; i < PSP_TIMER_POOL_SIZE; i++) {
        if (!sTimers[i].active) {
            psp_memzero(&sTimers[i], sizeof(sTimers[i]));
            sTimers[i].owner = timer;
            sTimers[i].countdown = countdown;
            sTimers[i].interval = interval;
            sTimers[i].mq = mq;
            sTimers[i].msg = msg;
            sTimers[i].active = 1;
            sTimers[i].threadId = sceKernelCreateThread("n64_timer", psp_timer_thread, 0x18, 0x1000, 0, NULL);
            if (sTimers[i].threadId < 0) {
                sTimers[i].active = 0;
                return -1;
            }
            {
                PspTimer* timerArg = &sTimers[i];
                sceKernelStartThread(sTimers[i].threadId, sizeof(timerArg), &timerArg);
            }
            return 0;
        }
    }

    return -1;
}

int osStopTimer(OSTimer* timer) {
    s32 i;

    for (i = 0; i < PSP_TIMER_POOL_SIZE; i++) {
        if (sTimers[i].owner == timer) {
            sTimers[i].active = 0;
            if (sTimers[i].threadId >= 0) {
                sceKernelTerminateDeleteThread(sTimers[i].threadId);
            }
            psp_memzero(&sTimers[i], sizeof(sTimers[i]));
            return 0;
        }
    }

    return -1;
}

OSTime osGetTime(void) {
    u64 ticks;

    sceRtcGetCurrentTick(&ticks);
    return (OSTime) ((ticks * PSP_N64_TICKS_PER_SECOND) / sceRtcGetTickResolution());
}

void osSetTime(OSTime time) {
    (void) time;
}

u32 osGetCount(void) {
    return (u32) osGetTime();
}

static int psp_thread_entry(SceSize args, void* argp) {
    PspThread* thread = (PspThread*) argp;

    (void) args;

    thread->entry(thread->arg);
    thread->active = 0;

    return 0;
}

void osCreateThread(OSThread* thread, OSId id, void (*entry)(void*), void* arg, void* sp, OSPri pri) {
    s32 i;

    (void) sp;

    thread->id = id;
    thread->priority = pri;
    thread->state = OS_STATE_STOPPED;

    for (i = 0; i < ARRAY_COUNT(sThreads); i++) {
        if (!sThreads[i].active && sThreads[i].owner == NULL) {
            sThreads[i].owner = thread;
            sThreads[i].entry = entry;
            sThreads[i].arg = arg;
            sThreads[i].threadId = -1;
            return;
        }
    }
}

void osStartThread(OSThread* thread) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sThreads); i++) {
        if (sThreads[i].owner == thread) {
            sThreads[i].active = 1;
            sThreads[i].threadId = sceKernelCreateThread("n64_thread", psp_thread_entry, 0x18, 0x4000, 0, NULL);
            if (sThreads[i].threadId >= 0) {
                PspThread* threadArg = &sThreads[i];
                thread->state = OS_STATE_RUNNING;
                sceKernelStartThread(sThreads[i].threadId, sizeof(threadArg), &threadArg);
            }
            return;
        }
    }
}

void osStopThread(OSThread* thread) {
    thread->state = OS_STATE_STOPPED;
}

void osDestroyThread(OSThread* thread) {
    s32 i;

    for (i = 0; i < ARRAY_COUNT(sThreads); i++) {
        if (sThreads[i].owner == thread) {
            if (sThreads[i].threadId >= 0) {
                sceKernelTerminateDeleteThread(sThreads[i].threadId);
            }
            psp_memzero(&sThreads[i], sizeof(sThreads[i]));
            return;
        }
    }
}

void osYieldThread(void) {
    sceKernelDelayThread(0);
}

OSId osGetThreadId(OSThread* thread) {
    return thread != NULL ? thread->id : 0;
}

void osSetThreadPri(OSThread* thread, OSPri pri) {
    if (thread == NULL) {
        return;
    }
    thread->priority = pri;
}

OSPri osGetThreadPri(OSThread* thread) {
    return thread != NULL ? thread->priority : 0;
}

void osWritebackDCacheAll(void) {
    sceKernelDcacheWritebackAll();
}

void osWritebackDCache(void* addr, s32 size) {
    sceKernelDcacheWritebackRange(addr, size);
}

void osInvalDCache(void* addr, s32 size) {
    sceKernelDcacheInvalidateRange(addr, size);
}

void osInvalICache(void* addr, s32 size) {
    (void) addr;
    (void) size;
    sceKernelIcacheInvalidateAll();
}

s32 osAiSetFrequency(u32 freq) {
    return (s32) freq;
}

void osSetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {
    PspPlatform_SetEventMesg(event, mq, msg);
}

void osViSetEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    PspPlatform_SetViEvent(mq, msg, retraceCount);
}

void osInitialize(void) {
    osTvType = OS_TV_NTSC;
}

s32 osEepromProbe(OSMesgQueue* mq) {
    (void) mq;
    return 1;
}

s32 osEepromRead(OSMesgQueue* mq, u8 address, u8* buffer) {
    return osEepromLongRead(mq, address, buffer, EEPROM_BLOCK_SIZE);
}

s32 osEepromWrite(OSMesgQueue* mq, u8 address, u8* buffer) {
    return osEepromLongWrite(mq, address, buffer, EEPROM_BLOCK_SIZE);
}

s32 osEepromLongRead(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    (void) mq;
    (void) address;
    psp_memzero(buffer, nbytes);
    return 0;
}

s32 osEepromLongWrite(OSMesgQueue* mq, u8 address, u8* buffer, int nbytes) {
    (void) mq;
    (void) address;
    (void) buffer;
    (void) nbytes;
    return 0;
}

s32 osContStartReadData(OSMesgQueue* mq) {
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetReadData(OSContPad* pad) {
    PspPlatform_PollInput(pad);
}

s32 osContInit(OSMesgQueue* mq, u8* bitpattern, OSContStatus* data) {
    s32 i;

    (void) mq;

    if (bitpattern != NULL) {
        *bitpattern = 1;
    }
    for (i = 0; i < MAXCONTROLLERS; i++) {
        data[i].type = CONT_TYPE_NORMAL;
        data[i].status = 0;
        data[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }
    return 0;
}

s32 osContReset(OSMesgQueue* mq, OSContStatus* data) {
    u8 bitpattern;
    return osContInit(mq, &bitpattern, data);
}

s32 osContStartQuery(OSMesgQueue* mq) {
    osSendMesg(mq, NULL, OS_MESG_NOBLOCK);
    return 0;
}

void osContGetQuery(OSContStatus* data) {
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++) {
        data[i].type = CONT_TYPE_NORMAL;
        data[i].status = 0;
        data[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }
}

s32 osContSetCh(u8 ch) {
    (void) ch;
    return 0;
}

void osCreateViManager(OSPri pri) {
    (void) pri;
}

void osViSetMode(OSViMode* modep) {
    (void) modep;
}

void osViSetSpecialFeatures(u32 func) {
    (void) func;
}

void osViSwapBuffer(void* frameBufPtr) {
    (void) frameBufPtr;
}

void osViBlack(u8 active) {
    (void) active;
}

void osViRepeatLine(u8 active) {
    (void) active;
}

u32 osViGetCurrentMode(void) {
    return 0;
}

u32 osViGetCurrentLine(void) {
    return 0;
}

void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQ, OSMesg* cmdBuf, s32 cmdMsgCnt) {
    (void) pri;
    osCreateMesgQueue(cmdQ, cmdBuf, cmdMsgCnt);
}

OSPiHandle* osCartRomInit(void) {
    static OSPiHandle sHandle;
    return &sHandle;
}

OSPiHandle* osDriveRomInit(void) {
    return osCartRomInit();
}

s32 osPiStartDma(OSIoMesg* mb, s32 pri, s32 direction, u32 devAddr, void* dramAddr, u32 size, OSMesgQueue* mq) {
    (void) pri;

    mb->hdr.retQueue = mq;
    mb->dramAddr = dramAddr;
    mb->devAddr = devAddr;
    mb->size = size;

    if ((direction == OS_READ) && (devAddr != 0) && (dramAddr != NULL) && (size != 0)) {
        psp_memcpy(dramAddr, (void*) devAddr, size);
    }
    if (mq != NULL) {
        osSendMesg(mq, mb, OS_MESG_NOBLOCK);
    }
    return 0;
}

s32 osEPiStartDma(OSPiHandle* pihandle, OSIoMesg* mb, s32 direction) {
    (void) pihandle;
    return osPiStartDma(mb, mb->hdr.pri, direction, mb->devAddr, mb->dramAddr, mb->size, mb->hdr.retQueue);
}

u32 osPiGetStatus(void) {
    return 0;
}

s32 osPiGetDeviceType(void) {
    return 0;
}

s32 osPiWriteIo(u32 devAddr, u32 data) {
    (void) devAddr;
    (void) data;
    return 0;
}

s32 osPiReadIo(u32 devAddr, u32* data) {
    (void) devAddr;
    if (data != NULL) {
        *data = 0;
    }
    return 0;
}

u32 osVirtualToPhysical(void* addr) {
    return (u32) addr;
}

void osDpSetStatus(u32 data) {
    (void) data;
}

u32 osDpGetStatus(void) {
    return 0;
}

void osSpTaskLoad(OSTask* intp) {
    sLoadedTask = intp;
}

void osSpTaskStartGo(OSTask* tp) {
    OSTask* task = tp != NULL ? tp : sLoadedTask;
    SPTask* spTask;

    if (task == NULL) {
        return;
    }

    spTask = (SPTask*) task;
    if (task->t.type == M_AUDTASK) {
        PspPlatform_RunAudioTask(spTask);
    } else {
        PspPlatform_RunGfxTask(spTask);
    }
}

void osSpTaskYield(void) {
}

OSYieldResult osSpTaskYielded(OSTask* tp) {
    (void) tp;
    return 0;
}
