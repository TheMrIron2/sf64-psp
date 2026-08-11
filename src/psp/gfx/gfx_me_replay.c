#include "src/psp/gfx/gfx_me_replay.h"

#include "macros.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#include <stdint.h>

#define PSP_GFX_ME_MAX_DEPTH 8
#define PSP_GFX_ME_MAX_COMMANDS 8192
#define PSP_GFX_ME_MAX_NESTED_COMMANDS 2048
#define PSP_GFX_ME_MTX_STACK_DEPTH 4
#define PSP_GFX_ME_UNCACHED 0x40000000U

#define PSP_GFX_ME_OP_MTX 0x01
#define PSP_GFX_ME_OP_VTX 0x04
#define PSP_GFX_ME_OP_DL 0x06
#define PSP_GFX_ME_OP_TRI2 0xb1
#define PSP_GFX_ME_OP_ENDDL 0xb8
#define PSP_GFX_ME_OP_POPMTX 0xbd
#define PSP_GFX_ME_OP_MOVEWORD 0xbc
#define PSP_GFX_ME_OP_TRI1 0xbf

typedef struct {
    u32 segments[16];
    uintptr_t sourceBase;
    uintptr_t snapshotBase;
    u32 snapshotSize;
    float modelview[4][4];
    float projection[4][4];
    float modelviewStack[PSP_GFX_ME_MTX_STACK_DEPTH][4][4];
    u32 modelviewStackDepth;
    int hasModelview;
    int hasProjection;
    PspGfxMeReplayStats* stats;
} PspGfxMeReplayContext;

static void psp_gfx_me_hash_value(u32* hash, u32 value) {
    *hash = (*hash ^ value) * 16777619U;
}

static s32 psp_gfx_me_quantize(float value, float scale) {
    float scaled = value * scale;

    if (scaled >= 2147483520.0f) {
        return 0x7FFFFFFF;
    }
    if (scaled <= -2147483648.0f) {
        return (s32) 0x80000000U;
    }
    return scaled >= 0.0f ? (s32) (scaled + 0.5f) : (s32) (scaled - 0.5f);
}

void PspGfxMeReplay_HashTransform(u32* hash, u32 slot,
                                  float viewX, float viewY, float viewZ, float viewW,
                                  float clipX, float clipY, float clipZ, float clipW,
                                  int valid, int hasProjection) {
    const float view[4] = { viewX, viewY, viewZ, viewW };
    const float clip[4] = { clipX, clipY, clipZ, clipW };
    float clipScale = hasProjection ? 16.0f : 65536.0f;
    u32 i;

    psp_gfx_me_hash_value(hash, slot);
    psp_gfx_me_hash_value(hash, valid != 0);
    for (i = 0; i < 4; i++) {
        psp_gfx_me_hash_value(hash, (u32) psp_gfx_me_quantize(view[i], 16.0f));
    }
    if (!valid) {
        return;
    }
    for (i = 0; i < 4; i++) {
        psp_gfx_me_hash_value(hash, (u32) psp_gfx_me_quantize(clip[i], clipScale));
    }
}

static void psp_gfx_me_matrix_identity(float matrix[4][4]) {
    u32 column;
    u32 row;

    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            matrix[column][row] = column == row ? 1.0f : 0.0f;
        }
    }
}

static void psp_gfx_me_matrix_copy(float out[4][4], const float in[4][4]) {
    u32 column;
    u32 row;

    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            out[column][row] = in[column][row];
        }
    }
}

static void psp_gfx_me_matrix_mul(float out[4][4], const float a[4][4], const float b[4][4]) {
    float result[4][4];
    u32 column;
    u32 row;

    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            result[column][row] =
                a[0][row] * b[column][0] +
                a[1][row] * b[column][1] +
                a[2][row] * b[column][2] +
                a[3][row] * b[column][3];
        }
    }
    psp_gfx_me_matrix_copy(out, result);
}

static void psp_gfx_me_matrix_fixed_to_float(float out[4][4], const Mtx* src) {
    u32 column;
    u32 row;

    for (column = 0; column < 4; column++) {
        for (row = 0; row < 4; row++) {
            s32 fixed = ((s32) ((u32) src->u.i[column][row] << 16)) |
                        src->u.f[column][row];

            out[column][row] = fixed / 65536.0f;
        }
    }
}

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

static const void* psp_gfx_me_uncached_ptr(const void* ptr) {
    return (const void*) (uintptr_t) (PSP_GFX_ME_UNCACHED | (u32) (uintptr_t) ptr);
}

static const void* psp_gfx_me_map_ptr(const PspGfxMeReplayContext* ctx,
                                      const void* ptr) {
    uintptr_t address = (uintptr_t) ptr;

    if ((address >= ctx->sourceBase) &&
        ((address - ctx->sourceBase) < ctx->snapshotSize)) {
        return (const void*) (ctx->snapshotBase + (address - ctx->sourceBase));
    }
    return psp_gfx_me_uncached_ptr(ptr);
}

static const void* psp_gfx_me_resolve_ptr(const PspGfxMeReplayContext* ctx, u32 raw) {
    uintptr_t ptr = (uintptr_t) raw;
    u32 base;
    u32 segment;

    if (ptr == 0) {
        return NULL;
    }
    if (PSP_IS_NATIVE_PTR(ptr)) {
        return psp_gfx_me_map_ptr(ctx, (const void*) ptr);
    }
    segment = (raw >> 24) & 0xF;
    base = ctx->segments[segment];
    if (base == 0) {
        return NULL;
    }
    return psp_gfx_me_map_ptr(
        ctx, (const void*) (uintptr_t) (base + (raw & 0xFFFFFFU)));
}

static void psp_gfx_me_handle_matrix(PspGfxMeReplayContext* ctx,
                                     const Gfx* command, int floating) {
    const void* src = psp_gfx_me_resolve_ptr(ctx, command->words.w1);
    float decoded[4][4];
    const float (*loaded)[4];
    float (*target)[4];
    int* hasTarget;
    u32 flags = (command->words.w0 >> 16) & 0xFF;

    if ((src == NULL) || (floating && ((((uintptr_t) src) & 0xF) != 0))) {
        return;
    }
    if ((flags & G_MTX_PROJECTION) != 0) {
        target = ctx->projection;
        hasTarget = &ctx->hasProjection;
    } else {
        target = ctx->modelview;
        hasTarget = &ctx->hasModelview;
        if ((flags & G_MTX_PUSH) != 0) {
            if (ctx->modelviewStackDepth < PSP_GFX_ME_MTX_STACK_DEPTH) {
                psp_gfx_me_matrix_copy(
                    ctx->modelviewStack[ctx->modelviewStackDepth], ctx->modelview);
                ctx->modelviewStackDepth++;
            }
        }
    }

    if (floating) {
        loaded = (const float (*)[4]) src;
    } else {
        psp_gfx_me_matrix_fixed_to_float(decoded, (const Mtx*) src);
        loaded = decoded;
    }
    if (((flags & G_MTX_LOAD) != 0) || !*hasTarget) {
        psp_gfx_me_matrix_copy(target, loaded);
    } else {
        psp_gfx_me_matrix_mul(target, target, loaded);
    }
    *hasTarget = 1;
}

static void psp_gfx_me_handle_pop_matrix(PspGfxMeReplayContext* ctx) {
    if (ctx->modelviewStackDepth == 0) {
        return;
    }
    ctx->modelviewStackDepth--;
    psp_gfx_me_matrix_copy(
        ctx->modelview, ctx->modelviewStack[ctx->modelviewStackDepth]);
    ctx->hasModelview = 1;
}

static void psp_gfx_me_handle_vertices(PspGfxMeReplayContext* ctx, const Gfx* command) {
    u32 count = (command->words.w0 >> 10) & 0x3F;
    u32 first = (command->words.w0 >> 17) & 0x7F;

    if ((psp_gfx_me_resolve_ptr(ctx, command->words.w1) == NULL) ||
        (count == 0) || ((first + count) > 64)) {
        return;
    }
    ctx->stats->loadedVertexCount += count;
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
            ctx->stats->gvtxCommandCount++;
            psp_gfx_me_handle_vertices(ctx, command);
            continue;
        }
        if ((opcode == PSP_GFX_ME_OP_MTX) || (opcode == PSP_RENDERER_DL_OP_MTXF)) {
            ctx->stats->matrixCommandCount++;
            psp_gfx_me_handle_matrix(
                ctx, command, opcode == PSP_RENDERER_DL_OP_MTXF);
            continue;
        }
        if (opcode == PSP_GFX_ME_OP_POPMTX) {
            psp_gfx_me_handle_pop_matrix(ctx);
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

int PspGfxMeReplay_Walk(const Gfx* dl, PspGfxMeReplayStats* stats,
                        const void* sourceBase, const void* snapshotBase,
                        u32 snapshotSize) {
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
    stats->transformHash = 2166136261U;
    ctx.stats = stats;
    ctx.sourceBase = (uintptr_t) sourceBase;
    ctx.snapshotBase = (uintptr_t) snapshotBase;
    ctx.snapshotSize = snapshotSize;
    psp_gfx_me_matrix_identity(ctx.modelview);
    psp_gfx_me_matrix_identity(ctx.projection);
    psp_gfx_me_walk_internal(&ctx, dl, 0);
    return (stats->commandLimitHit == 0) && (stats->depthLimitHit == 0) ? 0 : -2;
}
