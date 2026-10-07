#if PSP_GFX_BACKEND_GU
#include <n64psp/native_mesh.h>
#include <malloc.h>

extern Gfx aVenomFighter1DL[];
extern Gfx ast_title_seg6_gfx_2C4B0[];

#define PSP_RETAINED_CACHE_SLOTS 2

typedef struct {
    n64psp_mesh mesh;
    u16 indices[N64PSP_MESH_INDEX_LIMIT] __attribute__((aligned(16)));
    float bounds[2][3];
    u32 streamVertices;
} PspGfxDlRetainedCache;

static PspGfxDlRetainedCache* sRetainedCache;
static PspGfxDlRetainedCache* sRetainedCaches[PSP_RETAINED_CACHE_SLOTS];
static int sRetainedBuilt[PSP_RETAINED_CACHE_SLOTS];
static const void* psp_gfx_dl_retained_resolve(void* user, uint32_t raw, size_t bytes, int* immutable) {
    (void) user;
    if (!psp_gfx_dl_is_native_ptr(raw) || (raw & 15) || !bytes || bytes > 64 * sizeof(Vtx)) return NULL;
    // The selected leaf and its vertex ranges are qualified from the frozen asset object
    *immutable = 1;
    return (const void*) (uintptr_t) raw;
}

static int psp_gfx_dl_retained_build(const Gfx* child, u32 commandsCount, u32 slot) {
    u32 i;
    n64psp_mesh_command commands[N64PSP_MESH_COMMAND_LIMIT];

    sRetainedBuilt[slot] = -1;
    sRetainedCache = memalign(16, sizeof(*sRetainedCache));
    if (!sRetainedCache) return 0;
    sRetainedCaches[slot] = sRetainedCache;
    sRetainedCache->streamVertices = 0;
    for (i = 0; i < commandsCount; i++) {
        commands[i].w0 = child[i].words.w0;
        commands[i].w1 = child[i].words.w1;
    }
    if (!n64psp_mesh_build(&sRetainedCache->mesh, commands, commandsCount, 1, psp_gfx_dl_retained_resolve, NULL)) return 0;
    for (i = 0; i < sRetainedCache->mesh.vertex_count; i++) {
        u32 axis;
        for (axis = 0; axis < 3; axis++) {
            float value = sRetainedCache->mesh.vertices[i].position[axis];
            if (!i || value < sRetainedCache->bounds[0][axis]) sRetainedCache->bounds[0][axis] = value;
            if (!i || value > sRetainedCache->bounds[1][axis]) sRetainedCache->bounds[1][axis] = value;
        }
    }
    for (i = 0; i < sRetainedCache->mesh.action_count; i++) {
        n64psp_mesh_action* action = &sRetainedCache->mesh.actions[i];
        if (action->kind == N64PSP_MESH_SPAN) {
            u32 j, first = UINT16_MAX, last = 0;
            for (j = 0; j < action->count; j++) {
                u32 version = sRetainedCache->mesh.indices[action->first + j];
                if (version < first) first = version;
                if (version > last) last = version;
            }
            action->w0 = first;
            action->w1 = last - first + 1;
            if (action->w1 > 64 || sRetainedCache->streamVertices + action->w1 > N64PSP_MESH_INDEX_LIMIT) return 0;
            action->reserved = sRetainedCache->streamVertices;
            sRetainedCache->streamVertices += action->w1;
            for (j = 0; j < action->count; j++) {
                sRetainedCache->indices[action->first + j] = sRetainedCache->mesh.indices[action->first + j] - first;
            }
        }
    }
    PspGfxBackend_SealMeshIndices(sRetainedCache->indices, sRetainedCache->mesh.index_count);
    sRetainedBuilt[slot] = 1;
    return 1;
}

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
                    if ((w1 & 15) || ((w1 >> 10) & 15)) return 0;
                    request.mirrorS = ((w1 >> 8) & G_TX_MIRROR) && ((w1 >> 4) & 15);
                    request.mirrorT = ((w1 >> 18) & G_TX_MIRROR) && ((w1 >> 14) & 15);
                    break;
                case 0xf2:
                    request.width = ((w1 >> 12) & 4095) / 4 + 1;
                    request.height = (w1 & 4095) / 4 + 1;
                    break;
            }
        } else if (action->kind == N64PSP_MESH_SPAN && ctx->textureEnabled) {
            request.format = PSP_GFX_TEXTURE_RGBA16;
            if (fmt != G_IM_FMT_RGBA || size != G_IM_SIZ_16b || !PspGfxBackend_TextureSupported(&request)) return 0;
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
#if PSP_GFX_DL_HOT_STATS || PROFILE_HW_COUNTERS
    ctx->stats.vertexCount += sRetainedCache->mesh.vertex_count - finalCount;
#endif
}

static void psp_gfx_dl_retained_draw(PspGfxDlContext* ctx, PspGfxVertex* stream) {
    u32 i;
    PspGfxDlFogProjection fogProjection;
    float fogColor[4], fogStart, fogEnd;
    int requestedFog = (ctx->otherModeL >> 30) == G_BL_CLR_FOG;
    psp_gfx_dl_get_fog_projection(ctx->fogProjection, &fogProjection);
    psp_gfx_dl_resolve_fog_values(ctx, requestedFog, &fogProjection, fogColor, &fogStart, &fogEnd);
    psp_gfx_dl_pool_drain(ctx, PSP_PROFILE_FLUSH_OTHER);
    psp_gfx_dl_prepare_effective_lights(ctx);
    for (i = 0; i < sRetainedCache->mesh.action_count; i++) {
        const n64psp_mesh_action* action = &sRetainedCache->mesh.actions[i];
        if (action->kind == N64PSP_MESH_MATERIAL) {
            Gfx command;
            command.words.w0 = action->w0;
            command.words.w1 = action->w1;
            switch (action->w0 >> 24) {
                case 0xfd: psp_gfx_dl_handle_set_texture_image(ctx, &command); break;
                case 0xf5: psp_gfx_dl_handle_set_tile(ctx, &command); break;
                case 0xf2: psp_gfx_dl_handle_set_tile_size(ctx, &command); break;
            }
        } else if (action->kind == N64PSP_MESH_SPAN) {
            PspGfxDrawState state = { 0 };
            PspGfxVertex* vertices = stream + action->reserved;
            u32 j;
            psp_gfx_dl_resolve_effective_material_state(ctx);
            psp_gfx_dl_resolve_effective_depth_state(ctx);
            psp_gfx_dl_update_texture_uv_coefficients(ctx);
            for (j = 0; j < action->w1; j++) {
                sPspGfxDlLightingNormals[j] = *(const n64psp_snorm8x4*) sRetainedCache->mesh.vertices[action->w0 + j].color;
            }
            if (ctx->geometryMode & G_LIGHTING) {
                n64psp_directional_light_snorm8_batch(sPspGfxDlLightingOutput, &ctx->alignedMatrices.modelview,
                    sPspGfxDlLightingNormals,
                    ctx->groupedLightCount ? sPspGfxDlGroupedLightingLights : sPspGfxDlLightingLights,
                    &sPspGfxDlLightingAmbient, ctx->groupedLightCount ? ctx->groupedLightCount : ctx->lightCount,
                    action->w1);
            }
            for (j = 0; j < action->w1; j++) {
                const n64psp_mesh_vertex* source = &sRetainedCache->mesh.vertices[action->w0 + j];
                u32 r, g, b, a = source->color[3];
                if (ctx->geometryMode & G_LIGHTING) {
                    r = psp_gfx_dl_remap_lighting(sPspGfxDlLightingOutput[j].x);
                    g = psp_gfx_dl_remap_lighting(sPspGfxDlLightingOutput[j].y);
                    b = psp_gfx_dl_remap_lighting(sPspGfxDlLightingOutput[j].z);
                } else {
                    r = psp_gfx_color_transfer_u8(source->color[0]);
                    g = psp_gfx_color_transfer_u8(source->color[1]);
                    b = psp_gfx_color_transfer_u8(source->color[2]);
                }
                if (ctx->combineMode == PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA) a = 255;
                vertices[j].color = psp_gfx_dl_pack_rgba_u8(r, g, b, a, ctx->effectiveMaterial.classification.premultiplied);
                vertices[j].x = source->position[0];
                vertices[j].y = source->position[1];
                vertices[j].z = source->position[2];
                vertices[j].u = source->uv[0] * ctx->textureUvMulS + ctx->textureUvAddS;
                vertices[j].v = source->uv[1] * ctx->textureUvMulT + ctx->textureUvAddT;
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
            PspGfxBackend_DrawMesh(vertices, sRetainedCache->indices + action->first, action->count, &state,
                &ctx->alignedMatrices.modelview.m[0][0], (ctx->geometryMode & G_CULL_FRONT) != 0,
                (ctx->geometryMode & G_CULL_BACK) != 0);
        }
    }
    psp_gfx_dl_retained_final_slots(ctx);
    psp_gfx_dl_mark_effective_material_dirty(ctx);
    ctx->effectiveDepth.dirty = 1;
    ctx->effectiveFog.dirty = 1;
}

static int psp_gfx_dl_retained_dispatch(PspGfxDlContext* ctx, const Gfx* child, u32 depth) {
    PspGfxVertex* stream;
    u32 commandsCount, slot;
    if (child == aVenomFighter1DL) {
        commandsCount = 59;
        slot = 0;
    } else if (child == ast_title_seg6_gfx_2C4B0) {
        commandsCount = 160;
        slot = 1;
    } else {
        return 0;
    }
    if (depth >= PSP_GFX_DL_MAX_DEPTH || ctx->stats.commandCount > PSP_GFX_DL_MAX_COMMANDS - commandsCount ||
        !ctx->hasProjection || (ctx->geometryMode & G_TEXTURE_GEN) || psp_gfx_dl_depth_bias_enabled(ctx) ||
        ((ctx->geometryMode & G_CULL_BOTH) == G_CULL_BOTH) ||
        (ctx->combineMode != PSP_GFX_DL_COMBINE_MODULATE_SHADE_DECAL_ALPHA &&
         ctx->combineMode != PSP_GFX_DL_COMBINE_MODULATE_SHADE_ALPHA && ctx->combineMode != PSP_GFX_DL_COMBINE_SHADE)) {
        return 0;
    }
#if PSP_ORIGINAL_FOG
    if ((ctx->geometryMode & G_FOG) || ((ctx->otherModeL >> 30) == G_BL_CLR_FOG)) {
        return 0;
    }
#endif
#if PSP_RENDERER_DIAGNOSTICS
    if (ctx->traceActive) { return 0; }
#endif
    if (!sRetainedBuilt[slot] && !psp_gfx_dl_retained_build(child, commandsCount, slot)) { return 0; }
    if (sRetainedBuilt[slot] < 0) { return 0; }
    sRetainedCache = sRetainedCaches[slot];
    psp_gfx_dl_prepare_batch_matrices(ctx);
    if (!psp_gfx_dl_retained_inside(ctx) || !psp_gfx_dl_retained_materials(ctx)) {
        return 0;
    }
    stream = PspGfxBackend_AllocateMeshVertices(sRetainedCache->streamVertices, sRetainedCache->mesh.span_count);
    if (!stream) { return 0; }
    psp_gfx_dl_retained_draw(ctx, stream);
    ctx->stats.commandCount += sRetainedCache->mesh.command_count;
    if (depth > ctx->stats.maxDepthReached) ctx->stats.maxDepthReached = depth;
#if PSP_GFX_DL_HOT_STATS
    ctx->stats.triangleCount += sRetainedCache->mesh.index_count / 3;
#endif
    return 1;
}
#endif
