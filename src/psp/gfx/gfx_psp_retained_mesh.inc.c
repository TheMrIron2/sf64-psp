#if PSP_GFX_BACKEND_GU
#include <n64psp/native_mesh.h>

#include "gfx_psp_retained_sources.h"

static PspGfxDlRetainedCache* sRetainedCache;

static int psp_gfx_dl_retained_materials(const PspGfxDlContext* ctx) {
    PspGfxTextureRequest request = { 0 };
    u32 i, fmt = ctx->textureFormat, size = ctx->textureSize;
    request.width = ctx->textureWidth;
    request.height = ctx->textureHeight;
    request.pixels = ctx->textureImage;
    request.mirrorS = (ctx->textureCms & G_TX_MIRROR) && ctx->textureMaskS;
    request.mirrorT = (ctx->textureCmt & G_TX_MIRROR) && ctx->textureMaskT;
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

static __attribute__((noinline)) void psp_gfx_dl_retained_material(PspGfxDlContext* ctx,
    const n64psp_mesh_material* material) {
    u32 flags = material->flags;
    if (!flags) return;
    if ((flags & (N64PSP_MESH_IMAGE | N64PSP_MESH_TILE)) &&
        (ctx->textureFormat != material->format || ctx->textureSize != material->size ||
         ((flags & N64PSP_MESH_IMAGE) &&
          psp_gfx_dl_is_training_backdrop_texture(ctx->textureImage) !=
          psp_gfx_dl_is_training_backdrop_texture((const void*) (uintptr_t) material->image)))) {
        psp_gfx_dl_mark_material_classification_dirty(ctx);
    }
    if (flags & N64PSP_MESH_IMAGE) {
        ctx->textureImage = (const void*) (uintptr_t) material->image;
        ctx->texture = PspGfxTextureHandle_Null();
        ctx->textureUploadWidth = ctx->textureUploadHeight = 0;
        ctx->textureUploadX = ctx->textureUploadY = 0;
        ctx->textureUvCoefficientsDirty = 1;
    }
    if (flags & N64PSP_MESH_TILE) {
        if ((flags & N64PSP_MESH_MIRROR_CHANGED) ||
            (((ctx->textureCms & G_TX_MIRROR) && ctx->textureMaskS) !=
             ((material->cms & G_TX_MIRROR) && material->mask_s)) ||
            (((ctx->textureCmt & G_TX_MIRROR) && ctx->textureMaskT) !=
             ((material->cmt & G_TX_MIRROR) && material->mask_t))) {
            ctx->texture = PspGfxTextureHandle_Null();
        }
        ctx->texturePaletteIndex = material->palette;
        ctx->textureCms = material->cms;
        ctx->textureCmt = material->cmt;
        ctx->textureMaskS = material->mask_s;
        ctx->textureMaskT = material->mask_t;
#if PSP_RENDERER_DIAGNOSTICS
        ctx->textureShiftS = material->shift_s;
        ctx->textureShiftT = material->shift_t;
#endif
    }
    if (flags & (N64PSP_MESH_IMAGE | N64PSP_MESH_TILE)) {
        ctx->textureFormat = material->format;
        ctx->textureSize = material->size;
    }
    if (flags & N64PSP_MESH_SIZE) {
        ctx->textureWidth = material->width;
        ctx->textureHeight = material->height;
        ctx->textureTileUls = material->uls;
        ctx->textureTileUlt = material->ult;
        ctx->textureUvCoefficientsDirty = 1;
    }
    ctx->textureUploadAttempted = 0;
    psp_gfx_dl_mark_effective_material_dirty(ctx);
}

static void psp_gfx_dl_retained_draw(PspGfxDlContext* ctx, PspGfxVertex* stream, int discard,
                                    const PspGfxRetainedProgram* program) {
    u32 i;
    PspGfxDlFogProjection fogProjection;
    float fogColor[4], fogStart, fogEnd;
    int requestedFog = (ctx->otherModeL >> 30) == G_BL_CLR_FOG;
    psp_gfx_dl_get_fog_projection(ctx->fogProjection, &fogProjection);
    psp_gfx_dl_resolve_fog_values(ctx, requestedFog, &fogProjection, fogColor, &fogStart, &fogEnd);

    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_OTHER);

    psp_gfx_dl_prepare_effective_lights(ctx);

    for (i = 0; i < program->count; i++) {
        const n64psp_mesh_span* span = &program->spans[i];
        u32 vertexFirst, vertexCount, indexFirst, indexCount;
        psp_gfx_dl_retained_material(ctx, &span->material);
        if (!span->count) continue;
        vertexFirst = span->vertex_first;
        vertexCount = span->vertex_count;
        indexFirst = span->first;
        indexCount = span->count;
        {
            PspGfxDrawState state = { 0 };
            PspGfxVertex* vertices = stream + vertexFirst;
            u32 j;

            psp_gfx_dl_resolve_effective_material_state(ctx);
            psp_gfx_dl_resolve_effective_depth_state(ctx);
            psp_gfx_dl_update_texture_uv_coefficients(ctx);

            for (j = 0; j < vertexCount; j += 64) {
                u32 count = vertexCount - j;
                if (count > 64) count = 64;
                psp_gfx_dl_retained_vertices(ctx, vertices + j,
                    sRetainedCache->mesh.vertices + vertexFirst + j, count);
            }

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

            PspGfxBackend_DrawMesh(vertices, sRetainedCache->mesh.indices + indexFirst, indexCount, &state,
                &ctx->alignedMatrices.modelview.m[0][0], (ctx->geometryMode & G_CULL_FRONT) != 0,
                (ctx->geometryMode & G_CULL_BACK) != 0);

        }
    }

    if (discard) psp_gfx_dl_retained_discard_slots(ctx);
    else psp_gfx_dl_retained_final_slots(ctx);

    psp_gfx_dl_mark_effective_material_dirty(ctx);
    ctx->effectiveDepth.dirty = 1;
    ctx->effectiveFog.dirty = 1;

}

static __attribute__((noinline)) int psp_gfx_dl_retained_dispatch(PspGfxDlContext* ctx, const Gfx* child, u32 depth) {
    PspGfxVertex* stream;
    const PspGfxRetainedProgram* program;
    u32 commandsCount, slot;
    int discard, admitted;

    slot = PspGfxRetainedSource_Find(child, &commandsCount);

    if (slot == PSP_RETAINED_CACHE_SLOTS) {
        sRetainedStats.unregistered++;
        return 0;
    }

    sRetainedStats.fallbacks++;
    if (depth >= PSP_GFX_DL_MAX_DEPTH || ctx->stats.commandCount > PSP_GFX_DL_MAX_COMMANDS - commandsCount ||
        !ctx->hasProjection || (ctx->geometryMode & G_TEXTURE_GEN) || psp_gfx_dl_depth_bias_enabled(ctx) ||
        !((ctx->geometryMode & G_ZBUFFER) || (ctx->otherModeL & Z_UPD)) ||
        ((ctx->geometryMode & G_CULL_BOTH) == G_CULL_BOTH) ||
        (ctx->combineMode != PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA &&
         ctx->combineMode != PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA && ctx->combineMode != PSP_GFX_DL_COMBINE_SHADE)) {

        sRetainedStats.rejects[MESH_REJECT_STATE]++;
        return 0;
    }
#if PSP_ORIGINAL_FOG
    if ((ctx->geometryMode & G_FOG) || ((ctx->otherModeL >> 30) == G_BL_CLR_FOG)) {

        sRetainedStats.rejects[MESH_REJECT_FOG]++;
        return 0;
    }
#endif
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) {

        sRetainedStats.rejects[MESH_REJECT_TRACE]++;
        return 0;
    }
#endif
    sRetainedCache = PspGfxRetainedSource_Get(slot);

    program = sRetainedPrograms[slot];
    if (!sRetainedCache || !program) {
        sRetainedStats.rejects[MESH_REJECT_CACHE]++; return 0;
    }

    psp_gfx_dl_prepare_batch_matrices(ctx);
    // Projection handlers maintain one common render and fog projection
    admitted = n64psp_aabb_inside_clip_affine(sRetainedCache->bounds,
        &ctx->alignedMatrices.modelview, &ctx->alignedMatrices.projection, 1.0f, 0.001f, 0.0005f);

    if (!admitted) {
        sRetainedStats.rejects[MESH_REJECT_BOUNDS]++; return 0;
    }

    admitted = program->support ? (!ctx->textureEnabled ||
        (program->support == 1 && (!program->palette || ctx->texturePalette))) :
        psp_gfx_dl_retained_materials(ctx);

    if (!admitted) {
        sRetainedStats.rejects[MESH_REJECT_MATERIALS]++; return 0;
    }

    stream = PspGfxBackend_AllocateMeshVertices(sRetainedCache->mesh.stream_count, sRetainedCache->mesh.span_count);

    if (!stream) {
        sRetainedStats.rejects[MESH_REJECT_ALLOCATE]++; return 0;
    }

    ctx->stats.commandCount += sRetainedCache->mesh.command_count;

    discard = PspGfxRetainedSource_OutputsDead(slot, depth,
        PSP_GFX_DL_MAX_COMMANDS - ctx->stats.commandCount);
    if (discard) sRetainedStats.skipped += sRetainedWrites[slot].count;
    psp_gfx_dl_retained_draw(ctx, stream, discard, program);
    sRetainedStats.fallbacks--;
    sRetainedStats.hits++;
    sRetainedStats.spans += sRetainedCache->mesh.span_count;
    sRetainedStats.vertices += sRetainedCache->mesh.stream_count;
    sRetainedStats.triangles += sRetainedCache->mesh.index_count / 3;
    sRetainedStats.loaded += sRetainedCache->mesh.loaded_count;
    if (depth > ctx->stats.maxDepthReached) ctx->stats.maxDepthReached = depth;
#if PSP_GFX_DL_HOT_STATS
    ctx->stats.triangleCount += sRetainedCache->mesh.index_count / 3;
#endif
    return 1;
}
#endif
