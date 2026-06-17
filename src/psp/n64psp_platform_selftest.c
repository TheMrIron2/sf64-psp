#include "src/psp/n64psp_integration.h"

#include "n64psp/platform.h"

#include <pspdebug.h>

#define PSP_N64PSP_THREAD_VALUE 0x13572468
#define PSP_N64PSP_THREAD_EXIT 0x2468
#define PSP_N64PSP_SEM_EXIT 0x3579

typedef struct PspN64pspThreadDiagContext {
    int entered;
    int value;
    void* userdata_seen;
} PspN64pspThreadDiagContext;

typedef struct PspN64pspSemDiagContext {
    n64psp_platform_callbacks platform;
    n64psp_platform_sem* ready;
    n64psp_platform_sem* block;
    int entered;
    int woke;
    n64psp_result ready_post_result;
    n64psp_result wait_result;
} PspN64pspSemDiagContext;

static void psp_n64psp_print_diag(const char* prefix) {
    n64psp_platform_psp_diag diag;

    n64psp_platform_psp_get_diag(&diag);
    pspDebugScreenPrintf("[psp] %s sem create raw=%08x wait raw=%08x signal raw=%08x delete raw=%08x\n", prefix,
                         diag.sem_create_raw, diag.sem_wait_raw, diag.sem_signal_raw, diag.sem_delete_raw);
    pspDebugScreenPrintf("[psp] %s thread create raw=%08x start raw=%08x wait raw=%08x delete raw=%08x\n", prefix,
                         diag.thread_create_raw, diag.thread_start_raw, diag.thread_wait_raw, diag.thread_delete_raw);
    pspDebugScreenPrintf("[psp] %s parent thread=%p parent ctx=%p child thread=%p child userdata=%p\n", prefix,
                         diag.parent_thread_object, diag.parent_userdata, diag.child_thread_object, diag.child_userdata);
}

static int psp_n64psp_thread_diag_worker(void* userdata) {
    PspN64pspThreadDiagContext* ctx = (PspN64pspThreadDiagContext*) userdata;

    if (ctx == NULL) {
        return -1;
    }

    ctx->entered = 1;
    ctx->userdata_seen = userdata;
    ctx->value = PSP_N64PSP_THREAD_VALUE;
    return PSP_N64PSP_THREAD_EXIT;
}

static int psp_n64psp_sem_diag_worker(void* userdata) {
    PspN64pspSemDiagContext* ctx = (PspN64pspSemDiagContext*) userdata;

    if (ctx == NULL) {
        return -1;
    }

    ctx->entered = 1;
    ctx->ready_post_result = ctx->platform.sem_post(ctx->platform.userdata, ctx->ready);
    ctx->wait_result = ctx->platform.sem_wait(ctx->platform.userdata, ctx->block);
    ctx->woke = (ctx->wait_result == N64PSP_OK);
    return ctx->woke ? PSP_N64PSP_SEM_EXIT : -1;
}

static int psp_n64psp_platform_fail(const char* stage) {
    pspDebugScreenPrintf("[psp] n64psp platform self-test FAIL: %s\n", stage);
    psp_n64psp_print_diag(stage);
    return 0;
}

static int psp_n64psp_thread_selftest(const n64psp_platform_callbacks* platform) {
    PspN64pspThreadDiagContext ctx;
    n64psp_platform_thread* thread;
    n64psp_result result;
    n64psp_result join_result;
    int exit_code;

    ctx.entered = 0;
    ctx.value = 0;
    ctx.userdata_seen = NULL;
    thread = NULL;
    exit_code = -1;

    pspDebugScreenPrintf("[psp] n64psp platform thread self-test start\n");
    result = platform->thread_create(platform->userdata, "n64psp-tdiag", psp_n64psp_thread_diag_worker, &ctx, 0x2000,
                                     0x18, &thread);
    pspDebugScreenPrintf("[psp] thread_create result=%d parent thread=%p parent ctx=%p\n", (int) result,
                         n64psp_platform_psp_thread_object(thread), &ctx);
    psp_n64psp_print_diag("thread_create");
    if (result != N64PSP_OK) {
        return psp_n64psp_platform_fail("thread_create");
    }

    join_result = platform->thread_join(platform->userdata, thread, &exit_code);
    pspDebugScreenPrintf("[psp] thread_join result=%d worker exit=%d entered=%d value=%08x userdata_seen=%p\n",
                         (int) join_result, exit_code, ctx.entered, ctx.value, ctx.userdata_seen);
    psp_n64psp_print_diag("thread_join");

    platform->thread_destroy(platform->userdata, thread);
    psp_n64psp_print_diag("thread_destroy");

    if (join_result != N64PSP_OK || exit_code != PSP_N64PSP_THREAD_EXIT || !ctx.entered ||
        ctx.value != PSP_N64PSP_THREAD_VALUE || ctx.userdata_seen != &ctx) {
        return psp_n64psp_platform_fail("thread verification");
    }

    return 1;
}

static int psp_n64psp_sem_selftest(const n64psp_platform_callbacks* platform) {
    PspN64pspSemDiagContext ctx;
    n64psp_platform_sem* ready;
    n64psp_platform_sem* block;
    n64psp_platform_thread* thread;
    n64psp_result ready_create;
    n64psp_result block_create;
    n64psp_result thread_create;
    n64psp_result ready_wait;
    n64psp_result signal_result;
    n64psp_result join_result;
    int exit_code;

    pspDebugScreenPrintf("[psp] n64psp platform semaphore self-test start\n");

    ready = NULL;
    block = NULL;
    thread = NULL;
    exit_code = -1;
    ready_create = platform->sem_create(platform->userdata, 0, 1, &ready);
    pspDebugScreenPrintf("[psp] sem ready create result=%d sem=%p\n", (int) ready_create,
                         n64psp_platform_psp_sem_object(ready));
    psp_n64psp_print_diag("sem_ready_create");
    if (ready_create != N64PSP_OK) {
        return psp_n64psp_platform_fail("ready sem_create");
    }

    block_create = platform->sem_create(platform->userdata, 0, 1, &block);
    pspDebugScreenPrintf("[psp] sem block create result=%d sem=%p\n", (int) block_create,
                         n64psp_platform_psp_sem_object(block));
    psp_n64psp_print_diag("sem_block_create");
    if (block_create != N64PSP_OK) {
        platform->sem_destroy(platform->userdata, ready);
        return psp_n64psp_platform_fail("block sem_create");
    }

    ctx.platform = *platform;
    ctx.ready = ready;
    ctx.block = block;
    ctx.entered = 0;
    ctx.woke = 0;
    ctx.ready_post_result = N64PSP_ERROR_INVALID_STATE;
    ctx.wait_result = N64PSP_ERROR_INVALID_STATE;

    thread_create = platform->thread_create(platform->userdata, "n64psp-sdiag", psp_n64psp_sem_diag_worker, &ctx,
                                            0x2000, 0x18, &thread);
    pspDebugScreenPrintf("[psp] sem worker create result=%d thread=%p ctx=%p\n", (int) thread_create,
                         n64psp_platform_psp_thread_object(thread), &ctx);
    psp_n64psp_print_diag("sem_worker_create");
    if (thread_create != N64PSP_OK) {
        platform->sem_destroy(platform->userdata, block);
        platform->sem_destroy(platform->userdata, ready);
        return psp_n64psp_platform_fail("sem worker create");
    }

    ready_wait = platform->sem_wait(platform->userdata, ready);
    pspDebugScreenPrintf("[psp] main ready wait result=%d worker entered=%d ready_post=%d\n", (int) ready_wait,
                         ctx.entered, (int) ctx.ready_post_result);
    psp_n64psp_print_diag("sem_ready_wait");
    if (ready_wait != N64PSP_OK || !ctx.entered || ctx.ready_post_result != N64PSP_OK) {
        platform->thread_destroy(platform->userdata, thread);
        platform->sem_destroy(platform->userdata, block);
        platform->sem_destroy(platform->userdata, ready);
        return psp_n64psp_platform_fail("worker readiness");
    }

    signal_result = platform->sem_post(platform->userdata, block);
    pspDebugScreenPrintf("[psp] main signal result=%d\n", (int) signal_result);
    psp_n64psp_print_diag("sem_signal");
    if (signal_result != N64PSP_OK) {
        platform->thread_destroy(platform->userdata, thread);
        platform->sem_destroy(platform->userdata, block);
        platform->sem_destroy(platform->userdata, ready);
        return psp_n64psp_platform_fail("sem signal");
    }

    join_result = platform->thread_join(platform->userdata, thread, &exit_code);
    pspDebugScreenPrintf("[psp] sem worker join result=%d exit=%d woke=%d wait_result=%d\n", (int) join_result,
                         exit_code, ctx.woke, (int) ctx.wait_result);
    psp_n64psp_print_diag("sem_worker_join");

    platform->thread_destroy(platform->userdata, thread);
    platform->sem_destroy(platform->userdata, block);
    platform->sem_destroy(platform->userdata, ready);
    psp_n64psp_print_diag("sem_cleanup");

    if (join_result != N64PSP_OK || exit_code != PSP_N64PSP_SEM_EXIT || !ctx.woke ||
        ctx.wait_result != N64PSP_OK) {
        return psp_n64psp_platform_fail("sem wake verification");
    }

    return 1;
}

int PspN64psp_RunPlatformSelfTest(void) {
    n64psp_platform_callbacks platform;
    n64psp_result result;

    result = n64psp_platform_psp_get_callbacks(&platform);
    pspDebugScreenPrintf("[psp] platform callback fetch result=%d\n", (int) result);
    if (result != N64PSP_OK) {
        return psp_n64psp_platform_fail("callback fetch");
    }

    if (!psp_n64psp_thread_selftest(&platform)) {
        return 0;
    }
    if (!psp_n64psp_sem_selftest(&platform)) {
        return 0;
    }

    pspDebugScreenPrintf("[psp] n64psp platform self-test PASS\n");
    return 1;
}
