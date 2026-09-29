#ifndef PSP_TEXTURE_ATTRIBUTION_H
#define PSP_TEXTURE_ATTRIBUTION_H

#include "src/psp/hw_counter_profile.h"

#if PROFILE_HW_COUNTERS && PSP_GFX_BACKEND_GU
#include "src/psp/gfx/gfx_psp_backend.h"

typedef enum {
    PSP_HW_TEXTURE_INVALIDATE,
    PSP_HW_TEXTURE_SLOT_PRESSURE,
    PSP_HW_TEXTURE_EDRAM_BUDGET,
    PSP_HW_TEXTURE_EDRAM_ALLOC,
    PSP_HW_TEXTURE_RAM_BUDGET,
    PSP_HW_TEXTURE_RETIRE_COUNT
} PspHwTextureRetireReason;

typedef enum {
    PSP_HW_TEXTURE_PREPARE,
    PSP_HW_TEXTURE_RESERVE,
    PSP_HW_TEXTURE_DECODE,
    PSP_HW_TEXTURE_WRITEBACK,
    PSP_HW_TEXTURE_STAGE_COUNT
} PspHwTextureStage;

typedef enum {
    PSP_HW_TEXTURE_FRAME_CREATE_US,
    PSP_HW_TEXTURE_FRAME_RESERVE_US,
    PSP_HW_TEXTURE_FRAME_DECODE_US,
    PSP_HW_TEXTURE_FRAME_WRITEBACK_US,
    PSP_HW_TEXTURE_FRAME_BYTES,
    PSP_HW_TEXTURE_FRAME_UPLOADS,
    PSP_HW_TEXTURE_FRAME_FAILURES,
    PSP_HW_TEXTURE_FRAME_INVALIDATED,
    PSP_HW_TEXTURE_FRAME_EVICTED,
    PSP_HW_TEXTURE_FRAME_VARIANT,
    PSP_HW_TEXTURE_FRAME_COUNT
} PspHwTextureFrameMetric;

typedef struct {
    int active;
    u32 startUs;
    u32 lastUs;
    u32 stageUs[PSP_HW_TEXTURE_STAGE_COUNT];
    u32 cause;
    u32 changes;
    u32 history;
    u32 source;
} PspHwTextureCreateSample;

void PspHwCounterProfile_TextureCreateBegin(const PspGfxTextureRequest* request, PspHwTextureCreateSample* sample);
void PspHwCounterProfile_TextureCreateStage(PspHwTextureCreateSample* sample, PspHwTextureStage stage);
void PspHwCounterProfile_TextureCreateEnd(const PspGfxTextureRequest* request, PspHwTextureCreateSample* sample,
                                        u32 bytes, u32 texels, int edram, int refresh);
void PspHwCounterProfile_TextureRetire(const PspGfxTextureRequest* request, PspHwTextureRetireReason reason,
                                     u32 bytes, int retired, int edram);

#endif
#endif
