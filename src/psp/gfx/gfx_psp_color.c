#include "src/psp/gfx/gfx_psp_color.h"

#include <math.h>

u8 gPspGfxColorTransferLut[256];

void PspGfxColor_Init(void) {
    static int initialized;
    u32 i;

    if (initialized) {
        return;
    }
    for (i = 0; i < 256; i++) {
        gPspGfxColorTransferLut[i] = (u8) (255.0f * sqrtf((float) i / 255.0f));
    }
    initialized = 1;
}
