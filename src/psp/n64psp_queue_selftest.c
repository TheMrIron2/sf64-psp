#include "src/psp/n64psp_integration.h"

#include "PR/os.h"

#include <pspdebug.h>
#include <pspkernel.h>
#include <stddef.h>

#define PSP_N64PSP_STATIC_ASSERT_JOIN(a, b) a##b
#define PSP_N64PSP_STATIC_ASSERT_NAME(a, b) PSP_N64PSP_STATIC_ASSERT_JOIN(a, b)
#define PSP_N64PSP_STATIC_ASSERT(cond) \
    typedef char PSP_N64PSP_STATIC_ASSERT_NAME(psp_n64psp_static_assert_, __LINE__)[(cond) ? 1 : -1]

PSP_N64PSP_STATIC_ASSERT(sizeof(OSMesg) == sizeof(void*));
PSP_N64PSP_STATIC_ASSERT(sizeof(OSMesgQueue) == (sizeof(void*) * 3 + sizeof(s32) * 3));
PSP_N64PSP_STATIC_ASSERT(offsetof(OSMesgQueue, mtqueue) == 0);
PSP_N64PSP_STATIC_ASSERT(offsetof(OSMesgQueue, fullqueue) == sizeof(void*));
PSP_N64PSP_STATIC_ASSERT(offsetof(OSMesgQueue, validCount) == sizeof(void*) * 2);
PSP_N64PSP_STATIC_ASSERT(offsetof(OSMesgQueue, first) == sizeof(void*) * 2 + sizeof(s32));
PSP_N64PSP_STATIC_ASSERT(offsetof(OSMesgQueue, msgCount) == sizeof(void*) * 2 + sizeof(s32) * 2);
PSP_N64PSP_STATIC_ASSERT(offsetof(OSMesgQueue, msg) == sizeof(void*) * 2 + sizeof(s32) * 3);

typedef struct PspN64pspSelfTestWorker {
    OSMesgQueue* queue;
    OSMesg expected;
    SceUID readySem;
    int entered;
    int aboutToRecv;
    int recvResult;
    OSMesg received;
    int readySignalResult;
} PspN64pspSelfTestWorker;

static int psp_n64psp_selftest_worker(SceSize args, void* argp) {
    PspN64pspSelfTestWorker* worker;
    OSMesg msg;

    if (args != sizeof(worker) || argp == NULL) {
        return -1;
    }

    worker = *(PspN64pspSelfTestWorker**) argp;
    if (worker == NULL) {
        return -1;
    }

    msg = NULL;
    worker->entered = 1;
    pspDebugScreenPrintf("[psp] queue worker entered ctx=%p queue=%p\n", worker, worker->queue);
    worker->aboutToRecv = 1;
    worker->readySignalResult = sceKernelSignalSema(worker->readySem, 1);
    pspDebugScreenPrintf("[psp] queue worker about to recv readySignal=%08x\n", worker->readySignalResult);
    worker->recvResult = osRecvMesg(worker->queue, &msg, OS_MESG_BLOCK);
    worker->received = msg;
    pspDebugScreenPrintf("[psp] queue worker recv result=%d value=%p\n", worker->recvResult, worker->received);
    return (worker->recvResult == 0 && msg == worker->expected) ? 0 : -1;
}

static int psp_n64psp_selftest_fail(const char* stage) {
    pspDebugScreenPrintf("[psp] n64psp queue self-test FAIL: %s\n", stage);
    return 0;
}

int PspN64psp_RunQueueSelfTest(void) {
    OSMesgQueue queue;
    OSMesg messages[2];
    OSMesg msg;
    PspN64pspSelfTestWorker worker;
    PspN64pspSelfTestWorker* workerArg;
    SceUID threadId;
    SceUID readySem;
    int startResult;
    int readyWait;
    int sendResult;
    int wait;
    int referResult;
    int workerExit;
    int deleteResult;
    SceKernelThreadInfo threadInfo;

    pspDebugScreenPrintf("[psp] n64psp queue self-test start\n");

    osCreateMesgQueue(&queue, messages, 2);
    if (queue.msg != messages || queue.msgCount != 2 || queue.validCount != 0 || queue.first != 0) {
        return psp_n64psp_selftest_fail("create");
    }

    msg = NULL;
    if (osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK) == 0) {
        return psp_n64psp_selftest_fail("empty non-block recv");
    }

    if (osSendMesg(&queue, (OSMesg) 0x11111111, OS_MESG_NOBLOCK) != 0) {
        return psp_n64psp_selftest_fail("fifo send");
    }
    msg = NULL;
    if (osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK) != 0 || msg != (OSMesg) 0x11111111) {
        return psp_n64psp_selftest_fail("fifo recv");
    }

    if (osSendMesg(&queue, (OSMesg) 0x22222222, OS_MESG_NOBLOCK) != 0 ||
        osJamMesg(&queue, (OSMesg) 0x33333333, OS_MESG_NOBLOCK) != 0) {
        return psp_n64psp_selftest_fail("jam setup");
    }
    msg = NULL;
    if (osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK) != 0 || msg != (OSMesg) 0x33333333) {
        return psp_n64psp_selftest_fail("jam front");
    }
    msg = NULL;
    if (osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK) != 0 || msg != (OSMesg) 0x22222222) {
        return psp_n64psp_selftest_fail("jam tail");
    }

    if (osSendMesg(&queue, (OSMesg) 0x44444444, OS_MESG_NOBLOCK) != 0 ||
        osSendMesg(&queue, (OSMesg) 0x55555555, OS_MESG_NOBLOCK) != 0 ||
        osSendMesg(&queue, (OSMesg) 0x66666666, OS_MESG_NOBLOCK) == 0) {
        return psp_n64psp_selftest_fail("full non-block send");
    }
    msg = NULL;
    osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK);
    osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK);

    readySem = sceKernelCreateSema("n64psp-qready", 0, 0, 1, NULL);
    pspDebugScreenPrintf("[psp] queue ready semaphore create=%08x\n", readySem);
    if (readySem < 0) {
        return psp_n64psp_selftest_fail("ready sem create");
    }

    worker.queue = &queue;
    worker.expected = (OSMesg) 0x77777777;
    worker.readySem = readySem;
    worker.entered = 0;
    worker.aboutToRecv = 0;
    worker.recvResult = -99;
    worker.received = NULL;
    worker.readySignalResult = -99;
    workerArg = &worker;
    threadId = sceKernelCreateThread("n64psp-qtest", psp_n64psp_selftest_worker, 0x18, 0x2000, 0, NULL);
    pspDebugScreenPrintf("[psp] queue worker created thread=%08x ctx=%p queue=%p\n", threadId, &worker, &queue);
    if (threadId < 0) {
        sceKernelDeleteSema(readySem);
        return psp_n64psp_selftest_fail("worker create");
    }
    startResult = sceKernelStartThread(threadId, sizeof(workerArg), &workerArg);
    pspDebugScreenPrintf("[psp] queue worker start result=%08x\n", startResult);
    if (startResult < 0) {
        sceKernelDeleteThread(threadId);
        sceKernelDeleteSema(readySem);
        return psp_n64psp_selftest_fail("worker start");
    }

    readyWait = sceKernelWaitSema(readySem, 1, NULL);
    pspDebugScreenPrintf("[psp] main observed worker readiness wait=%08x entered=%d aboutToRecv=%d readySignal=%08x\n",
                         readyWait, worker.entered, worker.aboutToRecv, worker.readySignalResult);
    if (readyWait < 0 || !worker.entered || !worker.aboutToRecv || worker.readySignalResult < 0) {
        sceKernelTerminateDeleteThread(threadId);
        sceKernelDeleteSema(readySem);
        return psp_n64psp_selftest_fail("worker readiness");
    }

    sendResult = osSendMesg(&queue, worker.expected, OS_MESG_NOBLOCK);
    pspDebugScreenPrintf("[psp] main osSendMesg result=%d value=%p\n", sendResult, worker.expected);
    if (sendResult != 0) {
        sceKernelTerminateDeleteThread(threadId);
        sceKernelDeleteSema(readySem);
        return psp_n64psp_selftest_fail("worker wake send");
    }

    wait = sceKernelWaitThreadEnd(threadId, NULL);
    threadInfo.size = sizeof(threadInfo);
    referResult = sceKernelReferThreadStatus(threadId, &threadInfo);
    workerExit = (referResult < 0) ? -1 : threadInfo.exitStatus;
    pspDebugScreenPrintf("[psp] queue worker join result=%08x refer=%08x exit=%d recvResult=%d received=%p\n", wait,
                         referResult, workerExit, worker.recvResult, worker.received);
    deleteResult = sceKernelDeleteThread(threadId);
    pspDebugScreenPrintf("[psp] queue worker cleanup result=%08x\n", deleteResult);
    sceKernelDeleteSema(readySem);
    if (wait < 0 || workerExit != 0 || worker.recvResult != 0 || worker.received != worker.expected ||
        deleteResult < 0) {
        return psp_n64psp_selftest_fail("worker wake recv");
    }

    pspDebugScreenPrintf("[psp] n64psp queue self-test PASS\n");
    return 1;
}
