#ifndef PSP_GFX_COLOR_H
#define PSP_GFX_COLOR_H

#include "PR/ultratypes.h"

// Shared square root transfer for fixed function RGB inputs
#ifndef SF64_PSP_COLOR_TRANSFER
#define SF64_PSP_COLOR_TRANSFER 1
#endif

extern u8 gPspGfxColorTransferLut[256];

void PspGfxColor_Init(void);

static inline u8 psp_gfx_color_transfer_u8(u8 value) {
#if SF64_PSP_COLOR_TRANSFER
    return gPspGfxColorTransferLut[value];
#else
    return value;
#endif
}

static inline u32 psp_gfx_rgba5551_to_abgr8888(u16 color) {
    u32 r5 = (color >> 11) & 0x1F;
    u32 g5 = (color >> 6) & 0x1F;
    u32 b5 = (color >> 1) & 0x1F;
    u32 r = psp_gfx_color_transfer_u8((u8) ((r5 << 3) | (r5 >> 2)));
    u32 g = psp_gfx_color_transfer_u8((u8) ((g5 << 3) | (g5 >> 2)));
    u32 b = psp_gfx_color_transfer_u8((u8) ((b5 << 3) | (b5 >> 2)));
    u32 a = (color & 1U) ? 255U : 0U;

    return r | (g << 8) | (b << 16) | (a << 24);
}

#endif
