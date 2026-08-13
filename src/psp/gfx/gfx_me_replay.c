#include "src/psp/gfx/gfx_me_replay.h"

#include "macros.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer.h"

#if PSP_GFX_ME_REPLAY
#include <me-core-mapper/me-core-mapper.h>
#include <me-core-mapper/vme-lib.h>
#endif
#include <stdint.h>

#define PSP_GFX_ME_MAX_DEPTH 8
#define PSP_GFX_ME_MAX_COMMANDS 8192
#define PSP_GFX_ME_MAX_NESTED_COMMANDS 2048
#define PSP_GFX_ME_MTX_STACK_DEPTH 4
#define PSP_GFX_ME_UNCACHED 0x40000000U
#if PSP_GFX_ME_REPLAY
#define PSP_GFX_ME_VME_TOP_BUFFER_0 (VME_TOP_BUFFERS + 0x2000U * 0)
#define PSP_GFX_ME_VME_TOP_BUFFER_1 (VME_TOP_BUFFERS + 0x2000U * 1)
#define PSP_GFX_ME_VME_TOP_BUFFER_2 (VME_TOP_BUFFERS + 0x2000U * 2)
#define PSP_GFX_ME_VME_TOP_BUFFER_3 (VME_TOP_BUFFERS + 0x2000U * 3)
#define PSP_GFX_ME_VME_BASE_BUFFER_0 (VME_BASE_BUFFERS + 0x2000U * 0)
#define PSP_GFX_ME_VME_INPUT_OFFSET 0x100
#define PSP_GFX_ME_VME_MAX_FRAC_BITS 12
#define PSP_GFX_ME_VME_LIMIT 0x7FFFFF
#endif

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
    PspGfxMeTransformTrace* trace;
    u32 traceCapacity;
    volatile u32* tracePublished;
#if PSP_GFX_ME_REPLAY
    volatile u32* vmeStage;
    int vmeStagePending;
    int vmeFracBits;
    float vmeInvScale;
    int vmeModelviewDirty;
    int vmeModelviewValid;
#endif
} PspGfxMeReplayContext;

#if PSP_GFX_ME_REPLAY
static int sPspGfxMeVmeConfigured;

static void psp_gfx_me_vme_configure(void) {
    int count = 4 - 1;
    u32 mux;

    if (sPspGfxMeVmeConfigured) {
        return;
    }
    vmeLibStart();
    vme_icn(AGU_TOP, 0);
    vme_icn(AGU_BASE, VME_DEF_MAPPER);
    vme_icn(AGU_WRITE, 0);

    mux = vme_mux(TOP_0, BASE_0);
    vme_pe0(vme_fu(PRIMARY), mux, VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS);
    vme_pe0(agu_top(MODE), VME_DEF_MODE);
    vme_pe0(agu_top(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_base(MODE), VME_DEF_MODE, PSP_GFX_ME_VME_INPUT_OFFSET);
    vme_pe0(agu_base(COUNT), VME_DEF_STEP, count);
    vme_pe0(agu_write(MODE), VME_DEF_MODE, VME_CYCLE_6);
    vme_pe0(agu_write(COUNT), VME_DEF_STEP, count);

    mux = vme_mux(TOP_1, BASE_0);
    vme_pe1(vme_fu(PRIMARY), mux, VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS);
    mux = vme_mux(TOP_2, BASE_0);
    vme_pe2(vme_fu(PRIMARY), mux, VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS);
    mux = vme_mux(TOP_3, BASE_0);
    vme_pe3(vme_fu(PRIMARY), mux, VME_FU_OPCODE_MAC_INNER_PRODUCT_BIAS);
    vmeLibFinish();
    sPspGfxMeVmeConfigured = 1;
}

void PspGfxMeReplay_VmeInit(volatile u32* vmeStage) {
    *vmeStage = 1;
    vmeLibEnable();
    *vmeStage = 2;
    vmeLibWipe();
    psp_gfx_me_vme_configure();
    *vmeStage = 3;
}

static int psp_gfx_me_vme_fixed(float value, int fracBits, s32* fixed) {
    float scaled = value * (1 << fracBits);

    if ((scaled > PSP_GFX_ME_VME_LIMIT) ||
        (scaled < -PSP_GFX_ME_VME_LIMIT)) {
        return 0;
    }
    *fixed = (s32) (scaled + (scaled >= 0.0f ? 0.5f : -0.5f));
    return 1;
}

static int psp_gfx_me_vme_choose_frac_bits(const float matrix[4][4],
                                            const Vtx* src, u32 count) {
    s32 maxCoord[4] = { 0, 0, 0, 1 };
    int fracBits;
    u32 vertex;
    u32 column;
    u32 row;

    for (vertex = 0; vertex < count; vertex++) {
        for (column = 0; column < 3; column++) {
            s32 value = src[vertex].v.ob[column];
            s32 magnitude = value < 0 ? -value : value;

            if (magnitude > maxCoord[column]) {
                maxCoord[column] = magnitude;
            }
        }
    }
    for (fracBits = PSP_GFX_ME_VME_MAX_FRAC_BITS; fracBits >= 0; fracBits--) {
        int fits = 1;

        for (row = 0; (row < 4) && fits; row++) {
            int64_t bound = 0;

            for (column = 0; column < 4; column++) {
                s32 fixed;
                int64_t magnitude;

                if (!psp_gfx_me_vme_fixed(matrix[column][row], fracBits, &fixed)) {
                    fits = 0;
                    break;
                }
                magnitude = fixed < 0 ? -(int64_t) fixed : fixed;
                bound += magnitude * maxCoord[column];
            }
            if (bound > PSP_GFX_ME_VME_LIMIT) {
                fits = 0;
            }
        }
        if (fits) {
            return fracBits;
        }
    }
    return -1;
}

static int psp_gfx_me_vme_set_matrix(const float matrix[4][4], int fracBits) {
    volatile s32* rows[4];
    u32 row;
    u32 column;

    rows[0] = (volatile s32*) PSP_GFX_ME_VME_TOP_BUFFER_0;
    rows[1] = (volatile s32*) PSP_GFX_ME_VME_TOP_BUFFER_1;
    rows[2] = (volatile s32*) PSP_GFX_ME_VME_TOP_BUFFER_2;
    rows[3] = (volatile s32*) PSP_GFX_ME_VME_TOP_BUFFER_3;
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            s32 fixed;

            if (!psp_gfx_me_vme_fixed(matrix[column][row], fracBits, &fixed)) {
                return 0;
            }
            rows[row][column] = fixed;
        }
    }
    return 1;
}

static void psp_gfx_me_vme_stage_vertices(const Vtx* src, u32 count) {
    s32 input[64][4] __attribute__((aligned(64)));
    u32 i;

    for (i = 0; i < count; i++) {
        input[i][0] = src[i].v.ob[0];
        input[i][1] = src[i].v.ob[1];
        input[i][2] = src[i].v.ob[2];
        input[i][3] = 1;
    }
    meCoreDcacheWritebackRange(input, (count * sizeof(input[0]) + 63) & ~63);
    vmeLibMemoryToRingBuffer(input, PSP_GFX_ME_VME_INPUT_OFFSET, count * 4);
}

static void psp_gfx_me_vme_transform(float out[4], u32 index,
                                     float invScale) {
    volatile s32* output = (volatile s32*) PSP_GFX_ME_VME_BASE_BUFFER_0;

    vmeLibStart();
    vme_pe0(agu_base(MODE), VME_DEF_MODE,
            PSP_GFX_ME_VME_INPUT_OFFSET + index * 4);
    vmeLibFinish();
    out[0] = output[3] * invScale;
    out[1] = output[2051] * invScale;
    out[2] = output[4099] * invScale;
    out[3] = output[6147] * invScale;
}
#endif

static void psp_gfx_me_publish_trace(PspGfxMeReplayContext* ctx) {
    u32 count;

    if (ctx->tracePublished == NULL) {
        return;
    }
    count = ctx->stats->transformTraceCount;
    if (count > ctx->traceCapacity) {
        count = ctx->traceCapacity;
    }
    __asm__ volatile("sync" ::: "memory");
    *ctx->tracePublished = count;
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
#if PSP_GFX_ME_REPLAY
    if ((flags & G_MTX_PROJECTION) == 0) {
        ctx->vmeModelviewDirty = 1;
    }
#endif
}

static void psp_gfx_me_handle_pop_matrix(PspGfxMeReplayContext* ctx) {
    if (ctx->modelviewStackDepth == 0) {
        return;
    }
    ctx->modelviewStackDepth--;
    psp_gfx_me_matrix_copy(
        ctx->modelview, ctx->modelviewStack[ctx->modelviewStackDepth]);
    ctx->hasModelview = 1;
#if PSP_GFX_ME_REPLAY
    ctx->vmeModelviewDirty = 1;
#endif
}

static void psp_gfx_me_transform_vertex(float out[4], const float matrix[4][4],
                                        float x, float y, float z, float w) {
    out[0] = matrix[0][0] * x + matrix[1][0] * y + matrix[2][0] * z + matrix[3][0] * w;
    out[1] = matrix[0][1] * x + matrix[1][1] * y + matrix[2][1] * z + matrix[3][1] * w;
    out[2] = matrix[0][2] * x + matrix[1][2] * y + matrix[2][2] * z + matrix[3][2] * w;
    out[3] = matrix[0][3] * x + matrix[1][3] * y + matrix[2][3] * z + matrix[3][3] * w;
}

static void psp_gfx_me_trace_vertex(PspGfxMeReplayContext* ctx, u32 slot,
                                    const float view[4], const float clip[4],
                                    int valid, int usedVme) {
    PspGfxMeTransformTrace* entry;
    u32 index = ctx->stats->transformTraceCount++;
    u32 i;

    if ((ctx->trace == NULL) || (index >= ctx->traceCapacity)) {
        ctx->stats->transformTraceOverflow++;
        return;
    }
    entry = &ctx->trace[index];
    for (i = 0; i < 4; i++) {
        entry->view[i] = view[i];
        entry->clip[i] = valid ? clip[i] : 0.0f;
    }
    entry->slot = slot;
    entry->flags = (valid ? PSP_GFX_ME_TRANSFORM_VALID : 0) |
                   (ctx->hasProjection ? PSP_GFX_ME_TRANSFORM_PROJECTED : 0) |
                   (usedVme ? PSP_GFX_ME_TRANSFORM_VME : 0);
}

static void psp_gfx_me_handle_vertices(PspGfxMeReplayContext* ctx, const Gfx* command) {
    const Vtx* src = (const Vtx*) psp_gfx_me_resolve_ptr(ctx, command->words.w1);
    u32 count = (command->words.w0 >> 10) & 0x3F;
    u32 first = (command->words.w0 >> 17) & 0x7F;
    u32 i;

    if ((src == NULL) || (count == 0) || ((first + count) > 64)) {
        return;
    }
#if PSP_GFX_ME_REPLAY
    {
        int fracBits = psp_gfx_me_vme_choose_frac_bits(ctx->modelview, src, count);

        if (ctx->vmeModelviewDirty || (ctx->vmeFracBits != fracBits)) {
            if (ctx->vmeStagePending) {
                *ctx->vmeStage = 6;
            }
            ctx->vmeModelviewValid = (fracBits >= 0) &&
                psp_gfx_me_vme_set_matrix(ctx->modelview, fracBits);
            if (ctx->vmeStagePending) {
                *ctx->vmeStage = 7;
            }
            ctx->vmeFracBits = fracBits;
            ctx->vmeInvScale = fracBits >= 0 ? 1.0f / (1 << fracBits) : 1.0f;
            ctx->vmeModelviewDirty = 0;
        }
        if (ctx->vmeModelviewValid) {
            psp_gfx_me_vme_stage_vertices(src, count);
        }
    }
#endif
    for (i = 0; i < count; i++) {
        float view[4];
        float clip[4];
        int valid = 1;

#if PSP_GFX_ME_REPLAY
        if (ctx->vmeModelviewValid) {
            if (ctx->vmeStagePending) {
                *ctx->vmeStage = 8;
            }
            psp_gfx_me_vme_transform(view, i, ctx->vmeInvScale);
            if (ctx->vmeStagePending) {
                *ctx->vmeStage = 9;
                ctx->vmeStagePending = 0;
            }
        } else {
            psp_gfx_me_transform_vertex(
                view, ctx->modelview,
                (float) src[i].v.ob[0], (float) src[i].v.ob[1],
                (float) src[i].v.ob[2], 1.0f);
        }
#else
        psp_gfx_me_transform_vertex(
            view, ctx->modelview,
            (float) src[i].v.ob[0], (float) src[i].v.ob[1],
            (float) src[i].v.ob[2], 1.0f);
#endif
        if (ctx->hasProjection) {
            psp_gfx_me_transform_vertex(
                clip, ctx->projection,
                view[0], view[1], view[2], view[3]);
            valid = !((clip[3] > -0.001f) && (clip[3] < 0.001f));
        } else {
            clip[0] = view[0] / 320.0f;
            clip[1] = -view[1] / 240.0f;
            clip[2] = view[2] / 4096.0f;
            clip[3] = 1.0f;
        }
        psp_gfx_me_trace_vertex(ctx, first + i, view, clip, valid,
#if PSP_GFX_ME_REPLAY
                                ctx->vmeModelviewValid
#else
                                0
#endif
        );
    }
    ctx->stats->loadedVertexCount += count;
    ctx->stats->transformedVertexCount += count;
    psp_gfx_me_publish_trace(ctx);
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
                        u32 snapshotSize, PspGfxMeTransformTrace* trace,
                        u32 traceCapacity, volatile u32* tracePublished,
                        volatile u32* vmeStage) {
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
    ctx.trace = trace;
    ctx.traceCapacity = traceCapacity;
    ctx.tracePublished = tracePublished;
#if PSP_GFX_ME_REPLAY
    ctx.vmeStage = vmeStage;
    ctx.vmeStagePending = *vmeStage != 9;
#else
    (void) vmeStage;
#endif
    ctx.sourceBase = (uintptr_t) sourceBase;
    ctx.snapshotBase = (uintptr_t) snapshotBase;
    ctx.snapshotSize = snapshotSize;
    psp_gfx_me_matrix_identity(ctx.modelview);
    psp_gfx_me_matrix_identity(ctx.projection);
#if PSP_GFX_ME_REPLAY
    ctx.vmeModelviewDirty = 1;
    ctx.vmeFracBits = -1;
    if (ctx.vmeStagePending) {
        *ctx.vmeStage = 5;
    }
#endif
    psp_gfx_me_walk_internal(&ctx, dl, 0);
    return (stats->commandLimitHit == 0) && (stats->depthLimitHit == 0) ? 0 : -2;
}
