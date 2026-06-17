#include "src/psp/n64psp_integration.h"

#include "n64psp/runtime.h"

#include <pspdebug.h>

static int psp_n64psp_report(const char* stage, n64psp_result result) {
    if (result == N64PSP_OK) {
        return 1;
    }

    pspDebugScreenPrintf("[psp] n64psp %s failed: %s (%d)\n", stage, n64psp_result_name(result), (int) result);
    return 0;
}

int PspN64psp_Init(void) {
    n64psp_platform_callbacks platform;
    n64psp_renderer_callbacks renderer;
    n64psp_result result;

    result = n64psp_platform_psp_get_callbacks(&platform);
    if (!psp_n64psp_report("platform callbacks", result)) {
        return 0;
    }

    result = n64psp_trace_backend_get_callbacks(&renderer);
    if (!psp_n64psp_report("trace renderer callbacks", result)) {
        return 0;
    }

    result = n64psp_runtime_register_platform(&platform);
    if (!psp_n64psp_report("register platform", result)) {
        return 0;
    }

    result = n64psp_runtime_register_renderer(&renderer);
    if (!psp_n64psp_report("register renderer", result)) {
        return 0;
    }

    result = n64psp_runtime_init();
    if (!psp_n64psp_report("runtime init", result)) {
        return 0;
    }

    return 1;
}

void PspN64psp_Shutdown(void) {
    n64psp_result result;

    if (!n64psp_runtime_is_initialized()) {
        return;
    }

    result = n64psp_runtime_shutdown();
    if (result != N64PSP_OK) {
        pspDebugScreenPrintf("[psp] n64psp shutdown failed: %s (%d)\n", n64psp_result_name(result), (int) result);
    }
}
