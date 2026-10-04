static void psp_gfx_dl_native_tri1(PspGfxDlContext* ctx, u8 a, u8 b, u8 c, u32* projectionSerial) {
    *projectionSerial = 0;
#if PROFILE_HW_COUNTERS
    if (ctx->waterTile != 0) {
        ctx->waterInputTriangles++;
    }
#endif
    PspProfiler_CountTriangleCommand(1, 1, 0);
    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_TRIANGLE);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
    psp_gfx_dl_emit_tri(ctx, a, b, c);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TRIANGLE);
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_TRIANGLE);
}

static void psp_gfx_dl_native_tri2(PspGfxDlContext* ctx, u8 a0, u8 b0, u8 c0,
                                  u8 a1, u8 b1, u8 c1, u32* projectionSerial) {
#if PROFILE_HW_COUNTERS
    if (ctx->waterTile != 0) {
        ctx->waterInputTriangles += 2;
    }
#endif
    PspProfiler_CountTriangleCommand(2, 0, 1);
    PspHwCounterProfile_InnerScopeBegin(PSP_HW_SCOPE_TRIANGLE);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TRIANGLE);
#if PROFILE_TRIVIAL_REJECTS
    PspProfiler_CountTri2OutcomeMatrix(psp_gfx_dl_classify_triangle_outcome(ctx, a0, b0, c0),
                                       psp_gfx_dl_classify_triangle_outcome(ctx, a1, b1, c1));
#endif
    if (!psp_gfx_dl_try_emit_tri2_direct_pair(ctx, a0, b0, c0, a1, b1, c1, projectionSerial)) {
        *projectionSerial = 0;
        psp_gfx_dl_emit_tri(ctx, a0, b0, c0);
        psp_gfx_dl_emit_tri(ctx, a1, b1, c1);
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TRIANGLE);
    PspHwCounterProfile_InnerScopeEnd(PSP_HW_SCOPE_TRIANGLE);
}

#include "native_fighter.inc.c"

static int psp_gfx_dl_native_fighter_eligible(const PspGfxDlContext* ctx, u32 depth) {
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {
        return 0;
    }
#endif
    return (depth < PSP_GFX_DL_MAX_DEPTH) &&
           (ctx->stats.commandCount <= PSP_GFX_DL_MAX_COMMANDS - PSP_NATIVE_FIGHTER_COMMANDS);
}

static void psp_gfx_dl_native_fighter_run(PspGfxDlContext* ctx, u32 depth) {
    if (depth > ctx->stats.maxDepthReached) {
        ctx->stats.maxDepthReached = depth;
    }
    ctx->stats.commandCount += PSP_NATIVE_FIGHTER_COMMANDS;
#if PROFILE_HW_COUNTERS
    ctx->commandSources[ctx->commandSource].commands += PSP_NATIVE_FIGHTER_COMMANDS;
    ctx->commandSources[ctx->commandSource].vertexCommands += 6;
    ctx->commandSources[ctx->commandSource].triangleCommands += 13;
#endif
    psp_gfx_dl_native_fighter(ctx);
}
