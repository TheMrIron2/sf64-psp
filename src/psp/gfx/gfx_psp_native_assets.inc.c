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

static int psp_gfx_dl_native_asset_eligible(const PspGfxDlContext* ctx, u32 depth, u32 commands) {
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {
        return 0;
    }
#endif
    return (commands <= PSP_GFX_DL_MAX_COMMANDS) && (depth < PSP_GFX_DL_MAX_DEPTH) &&
           (ctx->stats.commandCount <= PSP_GFX_DL_MAX_COMMANDS - commands);
}

static __attribute__((noinline)) void psp_gfx_dl_native_asset_run(PspGfxDlContext* ctx, u32 depth,
                                                               u32 commands, u32 vertexCommands,
                                                               u32 triangleCommands,
                                                               void (*draw)(PspGfxDlContext*)) {
    if (depth > ctx->stats.maxDepthReached) {
        ctx->stats.maxDepthReached = depth;
    }
    ctx->stats.commandCount += commands;
#if PROFILE_HW_COUNTERS
    ctx->commandSources[ctx->commandSource].commands += commands;
    ctx->commandSources[ctx->commandSource].vertexCommands += vertexCommands;
    ctx->commandSources[ctx->commandSource].triangleCommands += triangleCommands;
#else
    (void) vertexCommands;
    (void) triangleCommands;
#endif
    draw(ctx);
}

#if PSP_NATIVE_COVERAGE_AB
static u32 sNativeCoverageRequested;
static PspGfxDlNativeCoverageStats sNativeCoverageStats;

static int psp_gfx_dl_native_coverage_run(PspGfxDlContext* ctx, u32 depth,
                                         u32 commands, u32 vertexCommands, u32 triangleCommands,
                                         void (*draw)(PspGfxDlContext*)) {
    sNativeCoverageStats.calls++;
    sNativeCoverageStats.commands += commands;
    if (sNativeCoverageStats.mode == 1) {
        return 0;
    }
    psp_gfx_dl_native_asset_run(ctx, depth, commands, vertexCommands, triangleCommands, draw);
    return 1;
}
#endif

#include "native_assets.inc.c"

#if PSP_NATIVE_COVERAGE_AB

void PspGfxDl_ToggleNativeCoverage(void) {
    sNativeCoverageRequested++;
    if (sNativeCoverageRequested == 3) {
        sNativeCoverageRequested = 0;
    }
}

void PspGfxDl_GetNativeCoverageStats(PspGfxDlNativeCoverageStats* stats) {
    *stats = sNativeCoverageStats;
}

static void psp_gfx_dl_native_coverage_begin(void) {
    sNativeCoverageStats.mode = sNativeCoverageRequested;
    sNativeCoverageStats.calls = 0;
    sNativeCoverageStats.commands = 0;
}

static int psp_gfx_dl_native_leaf_dispatch(PspGfxDlContext* ctx, const Gfx* child, u32 depth) {
    if (!sNativeCoverageStats.mode) {
        return psp_gfx_dl_native_asset_dispatch(ctx, child, depth);
    }
    return psp_gfx_dl_native_coverage_dispatch(ctx, child, depth);
}
#else
#define psp_gfx_dl_native_leaf_dispatch psp_gfx_dl_native_asset_dispatch
#endif

static int psp_gfx_dl_native_fighter_eligible(const PspGfxDlContext* ctx, u32 depth) {
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {
        return 0;
    }
#endif
    return (PSP_NATIVE_FIGHTER_COMMANDS <= PSP_GFX_DL_MAX_COMMANDS) && (depth < PSP_GFX_DL_MAX_DEPTH) &&
           (ctx->stats.commandCount <= PSP_GFX_DL_MAX_COMMANDS - PSP_NATIVE_FIGHTER_COMMANDS);
}

static void psp_gfx_dl_native_fighter_run(PspGfxDlContext* ctx, u32 depth) {
    if (depth > ctx->stats.maxDepthReached) {
        ctx->stats.maxDepthReached = depth;
    }
    ctx->stats.commandCount += PSP_NATIVE_FIGHTER_COMMANDS;
#if PROFILE_HW_COUNTERS
    ctx->commandSources[ctx->commandSource].commands += PSP_NATIVE_FIGHTER_COMMANDS;
    ctx->commandSources[ctx->commandSource].vertexCommands += PSP_NATIVE_FIGHTER_VERTEX_COMMANDS;
    ctx->commandSources[ctx->commandSource].triangleCommands += PSP_NATIVE_FIGHTER_TRIANGLE_COMMANDS;
#endif
    psp_gfx_dl_native_fighter(ctx);
}
