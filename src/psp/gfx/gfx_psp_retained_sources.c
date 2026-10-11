#include "gfx_psp_retained_sources.h"
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include "gfx_psp_backend.h"
#include "macros.h"
#include "../renderer.h"

#include "retained_assets.inc.c"

static u32 sRetainedAllocatedBytes;

void* PspGfxRetainedSource_Allocate(u32 bytes) {
    void* allocation;
    if (bytes > PSP_RETAINED_CACHE_BYTES - sRetainedAllocatedBytes) return NULL;
    allocation = memalign(16, bytes);
    if (allocation) sRetainedAllocatedBytes += bytes;
    return allocation;
}

u32 PspGfxRetainedSource_Find(const Gfx* commands, u32* count) {
    u32 first = 0, last = PSP_RETAINED_CACHE_SLOTS;
    while (first < last) {
        u32 slot = first + (last - first) / 2;
        uintptr_t target = (uintptr_t) sRetainedSources[slot].commands;
        if ((uintptr_t) commands < target) last = slot;
        else if ((uintptr_t) commands > target) first = slot + 1;
        else { *count = sRetainedSources[slot].count; return slot; }
    }
    return PSP_RETAINED_CACHE_SLOTS;
}

static PspGfxDlRetainedCache* sRetainedCaches[PSP_RETAINED_CACHE_SLOTS];
PspGfxRetainedProgram* sRetainedPrograms[PSP_RETAINED_CACHE_SLOTS];
static int sRetainedBuilt[PSP_RETAINED_CACHE_SLOTS];
PspGfxDlRetainedStats sRetainedStats;

void PspGfxDl_GetRetainedStats(PspGfxDlRetainedStats* stats) {
    *stats = sRetainedStats;
}
static const void* psp_gfx_dl_retained_resolve(void* user, uint32_t raw, size_t bytes, int* immutable) {
    (void) user;
    if (!PSP_IS_NATIVE_PTR(raw) || (raw & 7) || !bytes || bytes > 64 * sizeof(Vtx)) return NULL;
    // The qualified leaf and its vertex ranges are qualified from the frozen asset object
    *immutable = 1;
    return (const void*) (uintptr_t) raw;
}

static void psp_gfx_retained_program_support(PspGfxRetainedProgram* program) {
    PspGfxTextureRequest request = {0};
    u32 i, known = 0, format = 0, size = 0;
    int supported = 1, complete = 1;
    program->palette = 0;
    for (i = 0; i < program->count; i++) {
        const n64psp_mesh_span* span = &program->spans[i];
        const n64psp_mesh_material* material = &span->material;
        if (material->flags & N64PSP_MESH_IMAGE) {
            request.pixels = (const void*) (uintptr_t) material->image;
            if (!PSP_IS_NATIVE_PTR(material->image)) complete = 0;
        }
        if (material->flags & (N64PSP_MESH_IMAGE | N64PSP_MESH_TILE)) {
            format = material->format;
            size = material->size;
        }
        if (material->flags & N64PSP_MESH_TILE) {
            request.mirrorS = (material->cms & G_TX_MIRROR) && material->mask_s;
            request.mirrorT = (material->cmt & G_TX_MIRROR) && material->mask_t;
        }
        if (material->flags & N64PSP_MESH_SIZE) {
            request.width = material->width;
            request.height = material->height;
        }
        known |= material->flags;
        if (!span->count) continue;
        if ((known & 7) != 7) { complete = 0; continue; }
        request.palette = NULL;
        if (format == G_IM_FMT_RGBA && size == G_IM_SIZ_16b) request.format = PSP_GFX_TEXTURE_RGBA16;
        else if (format == G_IM_FMT_RGBA && size == G_IM_SIZ_32b) request.format = PSP_GFX_TEXTURE_RGBA32;
        else if (format == G_IM_FMT_IA && size == G_IM_SIZ_8b) request.format = PSP_GFX_TEXTURE_IA8;
        else if (format == G_IM_FMT_IA && size == G_IM_SIZ_16b) request.format = PSP_GFX_TEXTURE_IA16;
        else if (format == G_IM_FMT_CI && (size == G_IM_SIZ_4b || size == G_IM_SIZ_8b)) {
            program->palette = 1;
            request.palette = request.pixels;
            request.format = size == G_IM_SIZ_4b ? PSP_GFX_TEXTURE_CI4 : PSP_GFX_TEXTURE_CI8;
        } else { supported = 0; continue; }
        if (!PspGfxBackend_TextureSupported(&request)) supported = 0;
    }
    program->support = complete ? (supported ? 1 : 2) : 0;
}

static void psp_gfx_retained_program_build(u32 slot, const n64psp_mesh_packet* mesh) {
    u32 count = mesh->span_count + 1;
    u32 bytes = sizeof(PspGfxRetainedProgram) + count * sizeof(n64psp_mesh_span);
    PspGfxRetainedProgram* program = PspGfxRetainedSource_Allocate(bytes);
    if (!program) return;
    program->count = n64psp_mesh_compile_spans(mesh, program->spans, count);
    if (!program->count) return;
    psp_gfx_retained_program_support(program);
    sRetainedPrograms[slot] = program;
}

static int psp_gfx_dl_retained_build(const Gfx* child, u32 commandsCount, u32 slot) {
    u32 i;
    PspGfxDlRetainedCache* cache;
    size_t bytes, prefix = (sizeof(PspGfxDlRetainedCache) + 15) & ~(size_t) 15;
    n64psp_mesh* mesh;
    n64psp_mesh_command commands[N64PSP_MESH_COMMAND_LIMIT];

    sRetainedBuilt[slot] = -1;
    mesh = malloc(sizeof(*mesh));
    if (!mesh) return 0;
    for (i = 0; i < commandsCount; i++) {
        commands[i].w0 = child[i].words.w0;
        commands[i].w1 = child[i].words.w1;
    }
    if (!n64psp_mesh_build(mesh, commands, commandsCount, 1, psp_gfx_dl_retained_resolve, NULL)) {
        free(mesh);
        return 0;
    }
    bytes = n64psp_mesh_packet_bytes(mesh);
    cache = bytes ? PspGfxRetainedSource_Allocate(prefix + bytes) : NULL;
    if (!cache || !n64psp_mesh_packet_pack(&cache->mesh,
        (u8*) cache + prefix, bytes, mesh)) {
        free(mesh);
        return 0;
    }
    free(mesh);
    sRetainedCaches[slot] = cache;
    for (i = 0; i < cache->mesh.vertex_count; i++) {
        u32 axis;
        for (axis = 0; axis < 3; axis++) {
            float value = cache->mesh.vertices[i].position[axis];
            if (!i || value < cache->bounds[0][axis]) cache->bounds[0][axis] = value;
            if (!i || value > cache->bounds[1][axis]) cache->bounds[1][axis] = value;
        }
    }
    PspGfxBackend_SealMeshIndices(cache->mesh.indices, cache->mesh.index_count);
    psp_gfx_retained_program_build(slot, &cache->mesh);
    sRetainedBuilt[slot] = 1;
    return 1;
}

const n64psp_mesh_command* sRetainedContinuations[32];
u32 sRetainedCallerLevels;

void PspGfxRetainedSource_Prepare(void) {
    static int prepared;
    u32 slot;
    if (!prepared) {
        for (slot = 0; slot < PSP_RETAINED_CACHE_SLOTS; slot++) {
            PspGfxRetainedSource_Get(slot);
        }
        prepared = 1;
    }
    sRetainedCallerLevels = 0;
    sRetainedStats = (PspGfxDlRetainedStats) { 0 };
}

PspGfxDlRetainedCache* PspGfxRetainedSource_Get(u32 slot) {
    if (slot >= PSP_RETAINED_CACHE_SLOTS) return NULL;
    if (!sRetainedBuilt[slot]) {
        psp_gfx_dl_retained_build(sRetainedSources[slot].commands, sRetainedSources[slot].count, slot);
    }
    return sRetainedBuilt[slot] > 0 ? sRetainedCaches[slot] : NULL;
}

extern Gfx gRcpSetupDLs[][9];

static int psp_gfx_retained_effect(void* user, const n64psp_mesh_command* command,
                                    n64psp_mesh_vertex_effect* effect) {
    u32 op = command->w0 >> 24;
    (void) user;
    if (op == 6) {
        u32 count, slot;
        uintptr_t address = command->w1, setup = (uintptr_t) gRcpSetupDLs;
        *effect = (n64psp_mesh_vertex_effect) { {0,0}, {0,0}, 0 };
        slot = PspGfxRetainedSource_Find((const Gfx*) address, &count);
        if (slot != PSP_RETAINED_CACHE_SLOTS) {
            effect->writes[0] = sRetainedWrites[slot].slots[0];
            effect->writes[1] = sRetainedWrites[slot].slots[1];
            effect->commands = count;
            return 1;
        }
        if (address >= setup && address - setup < PSP_RETAINED_SETUP_ROWS * 72U &&
            (address - setup) % 72U == 0) {
            effect->commands = 9;
            return 1;
        }
        return 0;
    }
    if (op == 4 && (!PSP_IS_NATIVE_PTR(command->w1) || (command->w1 & 7))) {
        return 0;
    }
    if (op == PSP_RENDERER_DL_OP_MTXF || op == PSP_RENDERER_DL_OP_INVALIDATE_RGBA16 ||
        (command->w0 == 0xc0000000 && !command->w1)) {
        *effect = (n64psp_mesh_vertex_effect) { {0,0}, {0,0}, 0 };
        return 1;
    }
    {
        int known = n64psp_mesh_command_effect(command, effect);
        return known;
    }
}
int PspGfxRetainedSource_OutputsDead(u32 slot, u32 depth, u32 budget) {
    u32 required;
    if (!depth || depth > 32 || slot >= PSP_RETAINED_CACHE_SLOTS) return 0;
    required = depth == 32 ? UINT32_MAX : (1u << depth) - 1;
    if ((sRetainedCallerLevels & required) != required) return 0;
    return n64psp_mesh_outputs_dead(sRetainedContinuations, depth - 1,
        sRetainedWrites[slot].slots, budget, 32, 32, psp_gfx_retained_effect, NULL);
}
