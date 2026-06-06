#include "src/psp/gfx/gfx_psp_dl.h"

#include "macros.h"
#include "sf64thread.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/platform.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define PSP_GFX_DL_MAX_DEPTH 8
#define PSP_GFX_DL_MAX_COMMANDS 8192
#define PSP_GFX_DL_MAX_VERTICES 64
#define PSP_GFX_DL_BATCH_VERTICES 3072

#define PSP_GFX_OP_GBI2_VTX 0x01
#define PSP_GFX_OP_GBI2_TRI1 0x05
#define PSP_GFX_OP_GBI2_TRI2 0x06
#define PSP_GFX_OP_GBI2_TEXTURE 0xd7
#define PSP_GFX_OP_GBI2_POPMTX 0xd8
#define PSP_GFX_OP_GBI2_GEOMETRYMODE 0xd9
#define PSP_GFX_OP_GBI2_MTX 0xda
#define PSP_GFX_OP_GBI2_MOVEWORD 0xdb
#define PSP_GFX_OP_GBI2_MOVEMEM 0xdc
#define PSP_GFX_OP_GBI2_DL 0xde
#define PSP_GFX_OP_GBI2_ENDDL 0xdf
#define PSP_GFX_OP_GBI2_SPNOOP 0xe0

#define PSP_GFX_OP_F3D_SPNOOP 0x00
#define PSP_GFX_OP_F3D_MTX 0x01
#define PSP_GFX_OP_F3D_MOVEMEM 0x03
#define PSP_GFX_OP_F3D_VTX 0x04
#define PSP_GFX_OP_F3D_DL 0x06
#define PSP_GFX_OP_F3D_TRI1 0xbf
#define PSP_GFX_OP_F3D_CULLDL 0xbe
#define PSP_GFX_OP_F3D_POPMTX 0xbd
#define PSP_GFX_OP_F3D_MOVEWORD 0xbc
#define PSP_GFX_OP_F3D_TEXTURE 0xbb
#define PSP_GFX_OP_F3D_SETOTHERMODE_H 0xba
#define PSP_GFX_OP_F3D_SETOTHERMODE_L 0xb9
#define PSP_GFX_OP_F3D_ENDDL 0xb8
#define PSP_GFX_OP_F3D_SETGEOMETRYMODE 0xb7
#define PSP_GFX_OP_F3D_CLEARGEOMETRYMODE 0xb6
#define PSP_GFX_OP_F3D_RDPHALF_1 0xb4
#define PSP_GFX_OP_F3D_RDPHALF_2 0xb3
#define PSP_GFX_OP_F3D_MODIFYVTX 0xb2
#define PSP_GFX_OP_F3D_TRI2 0xb1

typedef struct {
    s16 x;
    s16 y;
    s16 z;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    int valid;
} PspGfxDlVertex;

typedef struct {
    PspGfxDlStats stats;
    PspGfxDlVertex vertices[PSP_GFX_DL_MAX_VERTICES];
    PspGfxPspglColorVertex batch[PSP_GFX_DL_BATCH_VERTICES];
    u32 batchCount;
} PspGfxDlContext;

static PspGfxDlContext sPspGfxDlContext;

static u8 psp_gfx_dl_opcode(const Gfx* gfx) {
    return (u8) (gfx->words.w0 >> 24);
}

static int psp_gfx_dl_is_native_ptr(uintptr_t ptr) {
    return (ptr >= 0x08000000U) && (ptr < 0x0A000000U);
}

static const void* psp_gfx_dl_resolve_ptr(u32 raw) {
    uintptr_t ptr = (uintptr_t) raw;
    u32 segment;
    u32 base;

    if (ptr == 0) {
        return NULL;
    }
    if (psp_gfx_dl_is_native_ptr(ptr)) {
        return (const void*) ptr;
    }

    segment = (raw >> 24) & 0xF;
    base = gSegments[segment];
    if (base == 0) {
        return NULL;
    }
    return SEGMENTED_TO_VIRTUAL(raw);
}

static int psp_gfx_dl_is_end(u8 opcode) {
    return (opcode == PSP_GFX_OP_F3D_ENDDL) || (opcode == PSP_GFX_OP_GBI2_ENDDL);
}

static int psp_gfx_dl_is_noop_state(u8 opcode) {
    if (opcode >= G_TEXRECT) {
        return 1;
    }

    switch (opcode) {
        case PSP_GFX_OP_F3D_SPNOOP:
        case PSP_GFX_OP_F3D_MOVEMEM:
        case PSP_GFX_OP_F3D_CULLDL:
        case PSP_GFX_OP_F3D_POPMTX:
        case PSP_GFX_OP_F3D_MOVEWORD:
        case PSP_GFX_OP_F3D_TEXTURE:
        case PSP_GFX_OP_F3D_SETOTHERMODE_H:
        case PSP_GFX_OP_F3D_SETOTHERMODE_L:
        case PSP_GFX_OP_F3D_SETGEOMETRYMODE:
        case PSP_GFX_OP_F3D_CLEARGEOMETRYMODE:
        case PSP_GFX_OP_F3D_RDPHALF_1:
        case PSP_GFX_OP_F3D_RDPHALF_2:
        case PSP_GFX_OP_F3D_MODIFYVTX:
        case PSP_GFX_OP_GBI2_SPNOOP:
        case PSP_GFX_OP_GBI2_MOVEMEM:
        case PSP_GFX_OP_GBI2_MOVEWORD:
        case PSP_GFX_OP_GBI2_TEXTURE:
        case PSP_GFX_OP_GBI2_POPMTX:
        case PSP_GFX_OP_GBI2_GEOMETRYMODE:
            return 1;
        default:
            return 0;
    }
}

static int psp_gfx_dl_is_gbi2_vtx_candidate(const Gfx* gfx) {
    u32 count = (gfx->words.w0 >> 12) & 0xFF;
    u32 end = gfx->words.w0 & 0xFFF;

    return (count > 0) && (count <= PSP_GFX_DL_MAX_VERTICES) && (end <= (PSP_GFX_DL_MAX_VERTICES * sizeof(Vtx)));
}

static u8 psp_gfx_dl_decode_tri_index(u32 packed, int gbi2) {
    return gbi2 ? (u8) (packed / 2) : (u8) (packed / 10);
}

static void psp_gfx_dl_flush(PspGfxDlContext* ctx) {
    if (ctx->batchCount == 0) {
        return;
    }
    PspGfxPspgl_DrawColoredTriangles(ctx->batch, ctx->batchCount);
    ctx->stats.drawVertexCount += ctx->batchCount;
    ctx->batchCount = 0;
}

static int psp_gfx_dl_vertex_is_valid(PspGfxDlContext* ctx, u8 index) {
    return (index < PSP_GFX_DL_MAX_VERTICES) && ctx->vertices[index].valid;
}

static void psp_gfx_dl_emit_vertex(PspGfxDlContext* ctx, u8 index) {
    const PspGfxDlVertex* src;
    PspGfxPspglColorVertex* dst;

    if (ctx->batchCount >= PSP_GFX_DL_BATCH_VERTICES) {
        psp_gfx_dl_flush(ctx);
    }

    src = &ctx->vertices[index];
    dst = &ctx->batch[ctx->batchCount++];

    dst->x = (float) src->x / 320.0f;
    dst->y = (float) -src->y / 240.0f;
    dst->z = (float) src->z / 4096.0f;
    dst->r = (float) src->r / 255.0f;
    dst->g = (float) src->g / 255.0f;
    dst->b = (float) src->b / 255.0f;
    dst->a = (float) src->a / 255.0f;
}

static void psp_gfx_dl_emit_tri(PspGfxDlContext* ctx, u8 a, u8 b, u8 c) {
    if (!psp_gfx_dl_vertex_is_valid(ctx, a) || !psp_gfx_dl_vertex_is_valid(ctx, b) ||
        !psp_gfx_dl_vertex_is_valid(ctx, c)) {
        ctx->stats.unsupportedCount++;
        return;
    }

    psp_gfx_dl_emit_vertex(ctx, a);
    psp_gfx_dl_emit_vertex(ctx, b);
    psp_gfx_dl_emit_vertex(ctx, c);
    ctx->stats.triangleCount++;
}

static void psp_gfx_dl_handle_vtx(PspGfxDlContext* ctx, const Gfx* gfx, int gbi2) {
    const Vtx* src = (const Vtx*) psp_gfx_dl_resolve_ptr(gfx->words.w1);
    u32 w0 = gfx->words.w0;
    u32 count;
    s32 v0;
    u32 i;

    if (src == NULL) {
        ctx->stats.unsupportedCount++;
        return;
    }

    if (gbi2) {
        count = (w0 >> 12) & 0xFF;
        v0 = (s32) (((w0 >> 1) & 0x7F) - count);
    } else if (psp_gfx_dl_opcode(gfx) == PSP_GFX_OP_F3D_VTX) {
        count = (w0 >> 10) & 0x3F;
        v0 = (s32) ((w0 >> 17) & 0x7F);
        if (count == 0) {
            count = ((w0 >> 20) & 0xF) + 1;
            v0 = (s32) ((w0 >> 16) & 0xF);
        }
    } else {
        count = (w0 >> 10) & 0x3F;
        v0 = (s32) ((w0 >> 17) & 0x7F);
    }

    if ((count == 0) || (v0 < 0) || (((u32) v0 + count) > PSP_GFX_DL_MAX_VERTICES)) {
        ctx->stats.unsupportedCount++;
        return;
    }

    for (i = 0; i < count; i++) {
        const Vtx* in = &src[i];
        PspGfxDlVertex* out = &ctx->vertices[v0 + i];

        out->x = in->v.ob[0];
        out->y = in->v.ob[1];
        out->z = in->v.ob[2];
        out->r = in->v.cn[0];
        out->g = in->v.cn[1];
        out->b = in->v.cn[2];
        out->a = in->v.cn[3];
        out->valid = 1;
    }

    ctx->stats.vertexCount += count;
}

static int psp_gfx_dl_run_internal(PspGfxDlContext* ctx, const Gfx* dl, u32 depth) {
    const Gfx* pc = dl;

    if (dl == NULL) {
        return 0;
    }
    if (depth >= PSP_GFX_DL_MAX_DEPTH) {
        ctx->stats.depthLimitHit++;
        return 0;
    }

    while (ctx->stats.commandCount < PSP_GFX_DL_MAX_COMMANDS) {
        const Gfx* cmd = pc++;
        u8 opcode = psp_gfx_dl_opcode(cmd);

        ctx->stats.commandCount++;

        if (psp_gfx_dl_is_end(opcode)) {
            return 1;
        }

        if ((opcode == PSP_GFX_OP_F3D_DL) || (opcode == PSP_GFX_OP_GBI2_DL)) {
            int f3dDl = (opcode == PSP_GFX_OP_F3D_DL) && ((cmd->words.w0 & 0xFFFF) == 0) &&
                        ((((cmd->words.w0 >> 16) & 0xFF) == G_DL_PUSH) ||
                         (((cmd->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH));
            const Gfx* child = (const Gfx*) psp_gfx_dl_resolve_ptr(cmd->words.w1);
            int noPush = ((cmd->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH;

            if (!f3dDl && (opcode == PSP_GFX_OP_F3D_DL)) {
                u32 w0 = cmd->words.w0;
                u32 w1 = cmd->words.w1;

                psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w0 >> 16) & 0xFF, 1),
                                    psp_gfx_dl_decode_tri_index((w0 >> 8) & 0xFF, 1),
                                    psp_gfx_dl_decode_tri_index(w0 & 0xFF, 1));
                psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF, 1),
                                    psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF, 1),
                                    psp_gfx_dl_decode_tri_index(w1 & 0xFF, 1));
                continue;
            }

            if (child == NULL) {
                ctx->stats.nestedDlRejected++;
                continue;
            }

            ctx->stats.nestedDlFollowed++;
            psp_gfx_dl_run_internal(ctx, child, depth + 1);
            if (noPush) {
                return 1;
            }
            continue;
        }

        if ((opcode == PSP_GFX_OP_GBI2_VTX) && psp_gfx_dl_is_gbi2_vtx_candidate(cmd)) {
            psp_gfx_dl_handle_vtx(ctx, cmd, 1);
            continue;
        }

        if ((opcode == PSP_GFX_OP_F3D_MTX) || (opcode == PSP_GFX_OP_GBI2_MTX)) {
            ctx->stats.mtxCount++;
            continue;
        }

        if ((opcode == PSP_GFX_OP_F3D_VTX) || (opcode == PSP_GFX_OP_GBI2_VTX)) {
            psp_gfx_dl_handle_vtx(ctx, cmd, opcode == PSP_GFX_OP_GBI2_VTX);
            continue;
        }

        if ((opcode == PSP_GFX_OP_F3D_TRI1) || (opcode == PSP_GFX_OP_GBI2_TRI1)) {
            u32 w1 = cmd->words.w1;
            int gbi2 = opcode == PSP_GFX_OP_GBI2_TRI1;
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF, gbi2),
                                psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF, gbi2),
                                psp_gfx_dl_decode_tri_index(w1 & 0xFF, gbi2));
            continue;
        }

        if ((opcode == PSP_GFX_OP_F3D_TRI2) || (opcode == PSP_GFX_OP_GBI2_TRI2)) {
            u32 w0 = cmd->words.w0;
            u32 w1 = cmd->words.w1;
            int gbi2 = opcode == PSP_GFX_OP_GBI2_TRI2;

            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w0 >> 16) & 0xFF, gbi2),
                                psp_gfx_dl_decode_tri_index((w0 >> 8) & 0xFF, gbi2),
                                psp_gfx_dl_decode_tri_index(w0 & 0xFF, gbi2));
            psp_gfx_dl_emit_tri(ctx, psp_gfx_dl_decode_tri_index((w1 >> 16) & 0xFF, gbi2),
                                psp_gfx_dl_decode_tri_index((w1 >> 8) & 0xFF, gbi2),
                                psp_gfx_dl_decode_tri_index(w1 & 0xFF, gbi2));
            continue;
        }

        if (!psp_gfx_dl_is_noop_state(opcode)) {
            ctx->stats.unsupportedCount++;
        }
    }

    ctx->stats.commandLimitHit++;
    return 0;
}

static void psp_gfx_dl_reset_context(PspGfxDlContext* ctx) {
    u8* bytes = (u8*) ctx;
    u32 i;

    for (i = 0; i < sizeof(*ctx); i++) {
        bytes[i] = 0;
    }
}

int PspGfxDl_Run(const Gfx* dl, u32 taskIndex, PspGfxDlStats* outStats) {
    PspGfxDlContext* ctx = &sPspGfxDlContext;
    char line[192];

    psp_gfx_dl_reset_context(ctx);
    psp_gfx_dl_run_internal(ctx, dl, 0);
    psp_gfx_dl_flush(ctx);

    if (outStats != NULL) {
        *outStats = ctx->stats;
    }

    if ((taskIndex < 4) || ((taskIndex % 30) == 0) || (ctx->stats.commandLimitHit != 0) ||
        (ctx->stats.depthLimitHit != 0)) {
        snprintf(line, sizeof(line),
                 "[pspgl-dl] task=%lu cmds=%lu vtx=%lu tri=%lu drawv=%lu dl=%lu reject=%lu mtx=%lu unsup=%lu "
                 "cmdLimit=%lu depthLimit=%lu",
                 (unsigned long) taskIndex, (unsigned long) ctx->stats.commandCount,
                 (unsigned long) ctx->stats.vertexCount, (unsigned long) ctx->stats.triangleCount,
                 (unsigned long) ctx->stats.drawVertexCount, (unsigned long) ctx->stats.nestedDlFollowed,
                 (unsigned long) ctx->stats.nestedDlRejected, (unsigned long) ctx->stats.mtxCount,
                 (unsigned long) ctx->stats.unsupportedCount, (unsigned long) ctx->stats.commandLimitHit,
                 (unsigned long) ctx->stats.depthLimitHit);
        PspPlatform_LogLine(line);
    }

    return ctx->stats.commandCount > 0;
}
