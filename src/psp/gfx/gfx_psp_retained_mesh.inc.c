#if PSP_GFX_BACKEND_GU
#include <n64psp/native_mesh.h>

#include "gfx_psp_retained_sources.h"
#include "gfx_psp_mesh_diagnostic.h"

static PspGfxDlRetainedCache* sRetainedCache;

static int psp_gfx_dl_retained_inside(PspGfxDlContext* ctx) {
    u32 corner;
    n64psp_mat4f n64Projection;
    const n64psp_mat4f* mv = &ctx->alignedMatrices.modelview;
    if (mv->m[0][3] != 0 || mv->m[1][3] != 0 || mv->m[2][3] != 0 || mv->m[3][3] != 1) return 0;
    psp_gfx_dl_mtx_copy(n64Projection.m, ctx->fogProjection);
    for (corner = 0; corner < 8; corner++) {
        n64psp_vec4f point, view, clip;
        u32 axis, pass;
        float* coords = &point.x;
        for (axis = 0; axis < 3; axis++) {
            u32 high = (corner >> axis) & 1;
            coords[axis] = sRetainedCache->bounds[high][axis] + (high ? 1.0f : -1.0f);
        }
        point.w = 1;
        n64psp_mat4f_transform_vec4(&view, mv, &point);
        for (pass = 0; pass < 2; pass++) {
            float margin;
            n64psp_mat4f_transform_vec4(&clip, pass ? &n64Projection : &ctx->alignedMatrices.projection, &view);
            if (!(clip.w > 0.001f) || !isfinite(clip.w)) return 0;
            margin = clip.w * 0.0005f;
            if (!(clip.x > -clip.w + margin && clip.x < clip.w - margin &&
                  clip.y > -clip.w + margin && clip.y < clip.w - margin &&
                  clip.z > -clip.w + margin && clip.z < clip.w - margin)) return 0;
        }
    }
    return 1;
}

static int psp_gfx_dl_retained_materials(const PspGfxDlContext* ctx) {
    PspGfxTextureRequest request = { 0 };
    u32 i, fmt = ctx->textureFormat, size = ctx->textureSize;
    request.width = ctx->textureWidth;
    request.height = ctx->textureHeight;
    request.pixels = ctx->textureImage;
    for (i = 0; i < sRetainedCache->mesh.action_count; i++) {
        const n64psp_mesh_action* action = &sRetainedCache->mesh.actions[i];
        u32 w0 = action->w0, w1 = action->w1;
        if (action->kind == N64PSP_MESH_MATERIAL) {
            switch (w0 >> 24) {
                case 0xfd:
                    fmt = (w0 >> 21) & 7;
                    size = (w0 >> 19) & 3;
                    if (!psp_gfx_dl_is_native_ptr(w1)) return 0;
                    request.pixels = (const void*) (uintptr_t) w1;
                    break;
                case 0xf5:
                    fmt = (w0 >> 21) & 7;
                    size = (w0 >> 19) & 3;
                    request.mirrorS = ((w1 >> 8) & G_TX_MIRROR) && ((w1 >> 4) & 15);
                    request.mirrorT = ((w1 >> 18) & G_TX_MIRROR) && ((w1 >> 14) & 15);
                    break;
                case 0xf2:
                    request.width = ((w1 >> 12) & 4095) / 4 + 1;
                    request.height = (w1 & 4095) / 4 + 1;
                    break;
            }
        } else if (action->kind == N64PSP_MESH_SPAN && ctx->textureEnabled) {
            request.palette = NULL;
            if (fmt == G_IM_FMT_RGBA && size == G_IM_SIZ_16b) request.format = PSP_GFX_TEXTURE_RGBA16;
            else if (fmt == G_IM_FMT_RGBA && size == G_IM_SIZ_32b) request.format = PSP_GFX_TEXTURE_RGBA32;
            else if (fmt == G_IM_FMT_IA && size == G_IM_SIZ_8b) request.format = PSP_GFX_TEXTURE_IA8;
            else if (fmt == G_IM_FMT_IA && size == G_IM_SIZ_16b) request.format = PSP_GFX_TEXTURE_IA16;
            else if (fmt == G_IM_FMT_CI && (size == G_IM_SIZ_4b || size == G_IM_SIZ_8b)) {
                if (!ctx->texturePalette) return 0;
                request.palette = ctx->texturePalette;
                request.format = size == G_IM_SIZ_4b ? PSP_GFX_TEXTURE_CI4 : PSP_GFX_TEXTURE_CI8;
            } else return 0;
            if (!PspGfxBackend_TextureSupported(&request)) return 0;
        }
    }
    return 1;
}

static void psp_gfx_dl_retained_final_slots(PspGfxDlContext* ctx) {
    Vtx vertices[64];
    u32 slot = 0, finalCount = 0;
    while (slot < 64) {
        u32 first = slot, count = 0;
        if (sRetainedCache->mesh.final_slots[slot] == N64PSP_MESH_EMPTY) { slot++; continue; }
        while (slot < 64 && sRetainedCache->mesh.final_slots[slot] != N64PSP_MESH_EMPTY) {
            vertices[count++] = *(const Vtx*) &sRetainedCache->mesh.vertices[sRetainedCache->mesh.final_slots[slot++]];
        }
        psp_gfx_dl_load_vertices(ctx, vertices, count, first);
        finalCount += count;
    }
    sRetainedStats.restored += finalCount;
#if PSP_GFX_DL_HOT_STATS || PROFILE_HW_COUNTERS
    ctx->stats.vertexCount += sRetainedCache->mesh.loaded_count - finalCount;
#endif
}

static __attribute__((noinline)) void psp_gfx_dl_retained_discard_slots(PspGfxDlContext* ctx) {
    u32 slot;
    for (slot = 0; slot < 64; slot++) {
        if (sRetainedCache->mesh.final_slots[slot] == N64PSP_MESH_EMPTY) continue;
        psp_gfx_dl_set_vertex_projection(ctx, &ctx->vertices[slot], PSP_GFX_DL_NO_PROJECTION_SNAPSHOT);
        ctx->vertices[slot].state.raw = 0;
    }
#if PSP_GFX_DL_HOT_STATS || PROFILE_HW_COUNTERS
    ctx->stats.vertexCount += sRetainedCache->mesh.loaded_count;
#endif
}

static __attribute__((noinline)) void psp_gfx_dl_retained_vertices(PspGfxDlContext* ctx,
    PspGfxVertex* vertices, const n64psp_mesh_vertex* source, u32 count) {
    u32 j;
    for (j = 0; j < count; j++) {
        sPspGfxDlLightingNormals[j] = *(const n64psp_snorm8x4*) source[j].color;
    }
    if (ctx->geometryMode & G_LIGHTING) {
        n64psp_directional_light_snorm8_batch(sPspGfxDlLightingOutput, &ctx->alignedMatrices.modelview,
            sPspGfxDlLightingNormals,
            ctx->groupedLightCount ? sPspGfxDlGroupedLightingLights : sPspGfxDlLightingLights,
            &sPspGfxDlLightingAmbient, ctx->groupedLightCount ? ctx->groupedLightCount : ctx->lightCount,
            count);
    }
    for (j = 0; j < count; j++) {
        const n64psp_mesh_vertex* vertex = &source[j];
        u32 r, g, b, a = vertex->color[3];
        if (ctx->geometryMode & G_LIGHTING) {
            r = psp_gfx_dl_remap_lighting(sPspGfxDlLightingOutput[j].x);
            g = psp_gfx_dl_remap_lighting(sPspGfxDlLightingOutput[j].y);
            b = psp_gfx_dl_remap_lighting(sPspGfxDlLightingOutput[j].z);
        } else {
            r = psp_gfx_color_transfer_u8(vertex->color[0]);
            g = psp_gfx_color_transfer_u8(vertex->color[1]);
            b = psp_gfx_color_transfer_u8(vertex->color[2]);
        }
        if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) a = 255;
        vertices[j].color = psp_gfx_dl_pack_rgba_u8(r, g, b, a, ctx->effectiveMaterial.classification.premultiplied);
        vertices[j].x = vertex->position[0];
        vertices[j].y = vertex->position[1];
        vertices[j].z = vertex->position[2];
        vertices[j].u = vertex->uv[0] * ctx->textureUvMulS + ctx->textureUvAddS;
        vertices[j].v = vertex->uv[1] * ctx->textureUvMulT + ctx->textureUvAddT;
    }
}

static void psp_gfx_dl_retained_draw(PspGfxDlContext* ctx, PspGfxVertex* stream, int discard) {
    u32 i, phaseStart, drawStart = PspMeshDiagnostic_Start();
    PspGfxDlFogProjection fogProjection;
    float fogColor[4], fogStart, fogEnd;
    int requestedFog = (ctx->otherModeL >> 30) == G_BL_CLR_FOG;
    psp_gfx_dl_get_fog_projection(ctx->fogProjection, &fogProjection);
    psp_gfx_dl_resolve_fog_values(ctx, requestedFog, &fogProjection, fogColor, &fogStart, &fogEnd);
    phaseStart = PspMeshDiagnostic_Start();
    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_OTHER);
    PspMeshDiagnostic_End(MESH_DIAG_DRAIN, phaseStart);
    phaseStart = PspMeshDiagnostic_Start();
    psp_gfx_dl_prepare_effective_lights(ctx);
    PspMeshDiagnostic_End(MESH_DIAG_LIGHTS, phaseStart);
    for (i = 0; i < sRetainedCache->mesh.action_count; i++) {
        const n64psp_mesh_action* action = &sRetainedCache->mesh.actions[i];
        if (action->kind == N64PSP_MESH_MATERIAL) {
            Gfx command;
            phaseStart = PspMeshDiagnostic_Start();
            command.words.w0 = action->w0;
            command.words.w1 = action->w1;
            switch (action->w0 >> 24) {
                case 0xfd: psp_gfx_dl_handle_set_texture_image(ctx, &command); break;
                case 0xf5: psp_gfx_dl_handle_set_tile(ctx, &command); break;
                case 0xf2: psp_gfx_dl_handle_set_tile_size(ctx, &command); break;
            }
            PspMeshDiagnostic_End(MESH_DIAG_DRAW_STATE, phaseStart);
        } else if (action->kind == N64PSP_MESH_SPAN) {
            PspGfxDrawState state = { 0 };
            PspGfxVertex* vertices = stream + action->reserved;
            u32 j;
            phaseStart = PspMeshDiagnostic_Start();
            psp_gfx_dl_resolve_effective_material_state(ctx);
            psp_gfx_dl_resolve_effective_depth_state(ctx);
            psp_gfx_dl_update_texture_uv_coefficients(ctx);
            PspMeshDiagnostic_End(MESH_DIAG_DRAW_STATE, phaseStart);
            phaseStart = PspMeshDiagnostic_Start();
            for (j = 0; j < action->w1; j += 64) {
                u32 count = action->w1 - j;
                if (count > 64) count = 64;
                psp_gfx_dl_retained_vertices(ctx, vertices + j,
                    sRetainedCache->mesh.vertices + action->w0 + j, count);
            }
            PspMeshDiagnostic_End(MESH_DIAG_VERTICES, phaseStart);
            phaseStart = PspMeshDiagnostic_Start();
            state.texture = ctx->effectiveMaterial.texture;
            state.textureEnv = ctx->effectiveMaterial.classification.textureEnv;
            state.textureEnvColor = ctx->effectiveMaterial.textureEnvColor;
            state.wrapS = ctx->effectiveMaterial.wrapS;
            state.wrapT = ctx->effectiveMaterial.wrapT;
            state.alphaTest = ctx->effectiveMaterial.classification.alphaTest;
            state.blend = ctx->effectiveMaterial.classification.blend;
            state.premultiplied = ctx->effectiveMaterial.classification.premultiplied;
            state.depthTest = ctx->effectiveDepth.depthTest;
            state.depthWrite = ctx->effectiveDepth.depthWrite;
            state.pointFilter = ctx->effectiveMaterial.classification.pointFilter;
            state.fog = requestedFog && ctx->fogMul != 0 && fogStart >= 0 && fogEnd > fogStart;
            state.fogColor = fogColor;
            state.fogStart = fogStart;
            state.fogEnd = fogEnd;
            state.projectionMatrix = &ctx->projection[0][0];
            state.projectionSerial = ctx->projectionSerial;
            state.viewport = psp_gfx_dl_resolve_viewport(ctx, NULL, 0, state.projectionMatrix);
            PspMeshDiagnostic_End(MESH_DIAG_DRAW_STATE, phaseStart);
            phaseStart = PspMeshDiagnostic_Start();
            PspGfxBackend_DrawMesh(vertices, sRetainedCache->mesh.indices + action->first, action->count, &state,
                &ctx->alignedMatrices.modelview.m[0][0], (ctx->geometryMode & G_CULL_FRONT) != 0,
                (ctx->geometryMode & G_CULL_BACK) != 0);
            PspMeshDiagnostic_End(MESH_DIAG_SUBMIT, phaseStart);
        }
    }
    phaseStart = PspMeshDiagnostic_Start();
    if (discard) psp_gfx_dl_retained_discard_slots(ctx);
    else psp_gfx_dl_retained_final_slots(ctx);
    PspMeshDiagnostic_End(discard ? MESH_DIAG_DISCARD : MESH_DIAG_RESTORE, phaseStart);
    psp_gfx_dl_mark_effective_material_dirty(ctx);
    ctx->effectiveDepth.dirty = 1;
    ctx->effectiveFog.dirty = 1;
    PspMeshDiagnostic_End(MESH_DIAG_DRAW, drawStart);
}

static __attribute__((noinline)) int psp_gfx_dl_retained_dispatch(PspGfxDlContext* ctx, const Gfx* child, u32 depth) {
    PspGfxVertex* stream;
    u32 commandsCount, slot, phaseStart, admissionStart;
    int discard, admitted;
    if (!sRetainedStats.mode) return 0;
    admissionStart = phaseStart = PspMeshDiagnostic_Start();
    slot = PspGfxRetainedSource_Find(child, &commandsCount);
    PspMeshDiagnostic_End(MESH_DIAG_LOOKUP, phaseStart);
    if (slot == PSP_RETAINED_CACHE_SLOTS) {
        if (sMeshDiagnosticEnabled) sMeshDiagnostic.unregistered++;
        return 0;
    }
    if (sRetainedStats.mode == 1 && !sRetainedSources[slot].baseline) {
        if (sMeshDiagnosticEnabled) sMeshDiagnostic.excluded++;
        return 0;
    }
    phaseStart = PspMeshDiagnostic_Start();
    sRetainedStats.fallbacks++;
    if (depth >= PSP_GFX_DL_MAX_DEPTH || ctx->stats.commandCount > PSP_GFX_DL_MAX_COMMANDS - commandsCount ||
        !ctx->hasProjection || (ctx->geometryMode & G_TEXTURE_GEN) || psp_gfx_dl_depth_bias_enabled(ctx) ||
        !((ctx->geometryMode & G_ZBUFFER) || (ctx->otherModeL & Z_UPD)) ||
        ((ctx->geometryMode & G_CULL_BOTH) == G_CULL_BOTH) ||
        (ctx->combineMode != PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA &&
         ctx->combineMode != PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA && ctx->combineMode != PSP_GFX_DL_COMBINE_SHADE)) {
        PspMeshDiagnostic_End(MESH_DIAG_STATE, phaseStart);
        PspMeshDiagnostic_Reject(MESH_REJECT_STATE, admissionStart);
        return 0;
    }
#if PSP_ORIGINAL_FOG
    if ((ctx->geometryMode & G_FOG) || ((ctx->otherModeL >> 30) == G_BL_CLR_FOG)) {
        PspMeshDiagnostic_End(MESH_DIAG_STATE, phaseStart);
        PspMeshDiagnostic_Reject(MESH_REJECT_FOG, admissionStart);
        return 0;
    }
#endif
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {
        PspMeshDiagnostic_End(MESH_DIAG_STATE, phaseStart);
        PspMeshDiagnostic_Reject(MESH_REJECT_TRACE, admissionStart);
        return 0;
    }
#endif
    sRetainedCache = PspGfxRetainedSource_Get(slot);
    PspMeshDiagnostic_End(MESH_DIAG_STATE, phaseStart);
    if (!sRetainedCache) {
        PspMeshDiagnostic_Reject(MESH_REJECT_CACHE, admissionStart); return 0;
    }
    phaseStart = PspMeshDiagnostic_Start();
    psp_gfx_dl_prepare_batch_matrices(ctx);
    admitted = psp_gfx_dl_retained_inside(ctx);
    PspMeshDiagnostic_End(MESH_DIAG_BOUNDS, phaseStart);
    if (!admitted) {
        PspMeshDiagnostic_Reject(MESH_REJECT_BOUNDS, admissionStart); return 0;
    }
    phaseStart = PspMeshDiagnostic_Start();
    admitted = psp_gfx_dl_retained_materials(ctx);
    PspMeshDiagnostic_End(MESH_DIAG_MATERIALS, phaseStart);
    if (!admitted) {
        PspMeshDiagnostic_Reject(MESH_REJECT_MATERIALS, admissionStart); return 0;
    }
    phaseStart = PspMeshDiagnostic_Start();
    stream = PspGfxBackend_AllocateMeshVertices(sRetainedCache->mesh.stream_count, sRetainedCache->mesh.span_count);
    PspMeshDiagnostic_End(MESH_DIAG_ALLOCATE, phaseStart);
    if (!stream) {
        PspMeshDiagnostic_Reject(MESH_REJECT_ALLOCATE, admissionStart); return 0;
    }
    PspMeshDiagnostic_End(MESH_DIAG_ADMIT, admissionStart);
    ctx->stats.commandCount += sRetainedCache->mesh.command_count;
    phaseStart = PspMeshDiagnostic_Start();
    discard = sRetainedStats.mode == 3 && PspGfxRetainedSource_OutputsDead(slot, depth,
        PSP_GFX_DL_MAX_COMMANDS - ctx->stats.commandCount);
    if (sRetainedStats.mode == 3) {
        PspMeshDiagnostic_End(MESH_DIAG_PROOF, phaseStart);
        if (sMeshDiagnosticEnabled) {
            if (discard) sMeshDiagnostic.proofDead++;
            else sMeshDiagnostic.proofLive++;
        }
    }
    if (discard) sRetainedStats.skipped += sRetainedWrites[slot].count;
    psp_gfx_dl_retained_draw(ctx, stream, discard);
    sRetainedStats.fallbacks--;
    sRetainedStats.hits++;
    sRetainedStats.spans += sRetainedCache->mesh.span_count;
    sRetainedStats.vertices += sRetainedCache->mesh.stream_count;
    if (depth > ctx->stats.maxDepthReached) ctx->stats.maxDepthReached = depth;
#if PSP_GFX_DL_HOT_STATS
    ctx->stats.triangleCount += sRetainedCache->mesh.index_count / 3;
#endif
    return 1;
}
#endif
