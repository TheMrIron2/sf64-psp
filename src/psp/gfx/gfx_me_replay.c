#include "src/psp/gfx/gfx_me_replay.h"

#include "macros.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#include <stdint.h>

#define PSP_GFX_ME_MAX_DEPTH 8
#define PSP_GFX_ME_MAX_COMMANDS 8192
#define PSP_GFX_ME_MAX_NESTED_COMMANDS 2048

#define PSP_GFX_ME_OP_MTX 0x01
#define PSP_GFX_ME_OP_VTX 0x04
#define PSP_GFX_ME_OP_DL 0x06
#define PSP_GFX_ME_OP_TRI2 0xb1
#define PSP_GFX_ME_OP_ENDDL 0xb8
#define PSP_GFX_ME_OP_MOVEWORD 0xbc
#define PSP_GFX_ME_OP_TRI1 0xbf

typedef struct {
    u32 segments[16];
    PspGfxMeReplayStats* stats;
} PspGfxMeReplayContext;

static u8 psp_gfx_me_opcode(const Gfx* command) {
    return (u8) (command->words.w0 >> 24);
}

static void psp_gfx_me_hash_word(PspGfxMeReplayStats* stats, u32 word) {
    stats->commandHash = (stats->commandHash ^ word) * 16777619U;
}

static void psp_gfx_me_hash_command(PspGfxMeReplayStats* stats, const Gfx* command) {
    psp_gfx_me_hash_word(stats, command->words.w0);
    psp_gfx_me_hash_word(stats, command->words.w1);
}

static const void* psp_gfx_me_resolve_ptr(const PspGfxMeReplayContext* ctx, u32 raw) {
    uintptr_t ptr = (uintptr_t) raw;
    u32 base;
    u32 segment;

    if (ptr == 0) {
        return NULL;
    }
    if (PSP_IS_NATIVE_PTR(ptr)) {
        return (const void*) ptr;
    }
    segment = (raw >> 24) & 0xF;
    base = ctx->segments[segment];
    if (base == 0) {
        return NULL;
    }
    return (const void*) (uintptr_t) (base + (raw & 0xFFFFFFU));
}

static int psp_gfx_me_has_bounded_end(const Gfx* dl) {
    u32 i;

    for (i = 0; i < PSP_GFX_ME_MAX_NESTED_COMMANDS; i++) {
        if (psp_gfx_me_opcode(&dl[i]) == PSP_GFX_ME_OP_ENDDL) {
            return 1;
        }
    }
    return 0;
}

static int psp_gfx_me_walk_internal(PspGfxMeReplayContext* ctx, const Gfx* dl, u32 depth) {
    const Gfx* pc = dl;

    if (dl == NULL) {
        return 0;
    }
    if (depth >= PSP_GFX_ME_MAX_DEPTH) {
        ctx->stats->depthLimitHit++;
        return 0;
    }

    while (ctx->stats->commandCount < PSP_GFX_ME_MAX_COMMANDS) {
        const Gfx* command = pc++;
        u8 opcode = psp_gfx_me_opcode(command);

        if ((opcode == G_NOOP) && PSP_RENDERER_DL_MARKER_MATCH(command->words.w1)) {
            continue;
        }
#if PROFILE_COMPONENTS
        if ((opcode == G_NOOP) && PSP_PROFILE_DL_COMPONENT_TAG_MATCH(command->words.w1)) {
            continue;
        }
#endif

        ctx->stats->commandCount++;
        psp_gfx_me_hash_command(ctx->stats, command);

        if (opcode == PSP_GFX_ME_OP_ENDDL) {
            return 1;
        }
        if (opcode == PSP_GFX_ME_OP_DL) {
            const Gfx* child = (const Gfx*) psp_gfx_me_resolve_ptr(ctx, command->words.w1);
            int noPush = ((command->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH;

            if ((child == NULL) || !psp_gfx_me_has_bounded_end(child)) {
                continue;
            }
            ctx->stats->nestedDlCount++;
            psp_gfx_me_walk_internal(ctx, child, depth + 1);
            if (noPush) {
                return 1;
            }
            continue;
        }
        if (opcode == PSP_GFX_ME_OP_MOVEWORD) {
            u32 offset = (command->words.w0 >> 8) & 0xFFFF;
            u32 index = command->words.w0 & 0xFF;

            if ((index == G_MW_SEGMENT) && ((offset & 3U) == 0) && ((offset >> 2) < 16)) {
                ctx->segments[offset >> 2] = command->words.w1;
            }
            continue;
        }
        if (opcode == PSP_GFX_ME_OP_VTX) {
            u32 count = (command->words.w0 >> 10) & 0x3F;
            u32 first = (command->words.w0 >> 17) & 0x7F;

            ctx->stats->gvtxCommandCount++;
            if ((psp_gfx_me_resolve_ptr(ctx, command->words.w1) != NULL) && (count != 0) &&
                ((first + count) <= 64)) {
                ctx->stats->loadedVertexCount += count;
            }
            continue;
        }
        if ((opcode == PSP_GFX_ME_OP_MTX) || (opcode == PSP_RENDERER_DL_OP_MTXF)) {
            ctx->stats->matrixCommandCount++;
            continue;
        }
        if (opcode == PSP_GFX_ME_OP_TRI1) {
            ctx->stats->tri1CommandCount++;
            ctx->stats->inputTriangleCount++;
            continue;
        }
        if (opcode == PSP_GFX_ME_OP_TRI2) {
            ctx->stats->tri2CommandCount++;
            ctx->stats->inputTriangleCount += 2;
            continue;
        }
        if ((opcode == G_TEXRECT) || (opcode == G_TEXRECTFLIP)) {
            const Gfx* half1;
            const Gfx* half2;

            if ((ctx->stats->commandCount + 2) > PSP_GFX_ME_MAX_COMMANDS) {
                ctx->stats->commandLimitHit++;
                return 0;
            }
            half1 = pc++;
            half2 = pc++;
            ctx->stats->commandCount += 2;
            ctx->stats->textureRectangleCount++;
            psp_gfx_me_hash_command(ctx->stats, half1);
            psp_gfx_me_hash_command(ctx->stats, half2);
        }
    }

    ctx->stats->commandLimitHit++;
    return 0;
}

int PspGfxMeReplay_Walk(const Gfx* dl, PspGfxMeReplayStats* stats) {
    PspGfxMeReplayContext ctx;
    u32 i;

    if ((dl == NULL) || (stats == NULL)) {
        return -1;
    }
    for (i = 0; i < sizeof(ctx); i++) {
        ((u8*) &ctx)[i] = 0;
    }
    for (i = 0; i < sizeof(*stats); i++) {
        ((u8*) stats)[i] = 0;
    }
    stats->commandHash = 2166136261U;
    ctx.stats = stats;
    psp_gfx_me_walk_internal(&ctx, dl, 0);
    return (stats->commandLimitHit == 0) && (stats->depthLimitHit == 0) ? 0 : -2;
}
