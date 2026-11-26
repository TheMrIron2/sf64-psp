#include "src/psp/gfx/gfx_psp_aquas_particles.h"
#include "src/psp/frame_interpolation.h"
#include "src/psp/gfx/gfx_psp_gu_device.h"
#include "src/psp/renderer.h"
#include "assets/ast_common.h"
#include "gfx.h"
#include <stdint.h>

static u32 sAtlas[128 * 32] __attribute__((aligned(16)));
static int sAtlasReady;
static const u32 sTint[4] = { 0xffffff, 0x0000ff, 0xff0000, 0xffff00 };

int PspAquasParticles_Available(void) { return PspGfxBackend_IsGu(); }

int PspAquasParticles_ValidMatrix(u32 address) {
    unsigned pool;
    for (pool = 0; pool < 2; pool++) {
        uintptr_t begin = (uintptr_t) &gGfxPools[pool].mtx[0];
        uintptr_t end = (uintptr_t) &gGfxPools[pool].mtx[0x480];
        if (address >= begin && address < end && (address - begin) % sizeof(Mtx) == 0) return 1;
    }
    return 0;
}

static void aquas_atlas_init(void) {
    unsigned variant, x, y, component;
    if (sAtlasReady) return;
    for (variant = 0; variant < 4; variant++) {
        for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) {
            u32 source = aOrbTex[y * 32 + x];
            u32 intensity = (source >> 4) * 17;
            u32 pixel = (source & 15) * 17;
            for (component = 0; component < 3; component++) {
                unsigned shift = 16 - component * 8;
                u32 environment = (sTint[variant] >> shift) & 255;
                u32 value = environment + ((255 - environment) * intensity + 127) / 255;
                pixel |= value << (24 - component * 8);
            }
            sAtlas[y * 128 + variant * 32 + x] = pixel;
        }
    }
    sAtlasReady = 1;
}

static void aquas_quad(PspGfxVertex* vertices, const float matrix[4][4], u32 colour) {
    static const u8 corners[6] = { 1, 2, 3, 1, 3, 0 };
    PspGfxVertex quad[4];
    unsigned i, axis;
    u32 variant = (colour >> 8) & 3;
    for (i = 0; i < 4; i++) {
        float x = i >= 2 ? 40.0f : -40.0f;
        float y = i == 0 || i == 3 ? 40.0f : -40.0f;
        float position[3];
        for (axis = 0; axis < 3; axis++)
            position[axis] = matrix[3][axis] + x * matrix[0][axis] + y * matrix[1][axis];
        quad[i].x = position[0]; quad[i].y = position[1]; quad[i].z = position[2];
        quad[i].u = (variant * 32.0f + (i >= 2 ? 31.5f : 0.5f)) / 128.0f;
        quad[i].v = (i == 0 || i == 3 ? 0.5f : 31.5f) / 32.0f;
        quad[i].color = 0xffffffu | ((colour & 255u) << 24);
    }
    for (i = 0; i < 6; i++) vertices[i] = quad[corners[i]];
}

int PspAquasParticles_Draw(const Gfx* records, u32 count, const float* projection,
                           u32 projectionSerial, PspGfxViewportPolicy viewport) {
    PspGfxTextureRequest request = {0};
    PspGfxTextureResult texture;
    PspGfxVertexReservation reservation;
    PspGfxDrawState state = {0};
    unsigned i;
    if (!PspGfxBackend_IsGu() || !count || count > PSP_AQUAS_PARTICLE_BATCH) return 0;
    for (i = 0; i < count; i++) {
        if (!PspAquasParticles_ValidMatrix(records[i * 2 + 1].words.w0) ||
            (records[i * 2 + 1].words.w1 & ~0x3ffu)) return 0;
    }
    aquas_atlas_init();
    request.format = PSP_GFX_TEXTURE_RGBA32;
    request.pixels = sAtlas;
    request.width = 128; request.height = 32;
    request.primitiveColor = request.environmentColor = 0xffffffffu;
    if (!PspGfxBackendGu_FindTexture(&request, &texture) &&
        !PspGfxBackendGu_CreateTexture(&request, &texture)) return 0;
    if (!PspGfxGuDevice_CanAppendOverlay(count * 6 * sizeof(PspGfxVertex) + 4096) ||
        !PspGfxBackendGu_ReserveVertices(count * 6, &reservation)) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const Gfx* data = &records[i * 2 + 1];
        const Mtx* source = (const Mtx*) (uintptr_t) data->words.w0;
        Matrix interpolated __attribute__((aligned(16)));
        const Matrix* matrix = (const Matrix*) source;
        if (PspFrameInterpolation_ResolveMatrix(source, G_MTX_LOAD | G_MTX_MODELVIEW, &interpolated))
            matrix = &interpolated;
        aquas_quad(reservation.vertices + i * 6, matrix->m, data->words.w1);
    }
    state.texture = texture.handle;
    state.textureEnv = PSP_GFX_TEX_MODULATE;
    state.wrapS = state.wrapT = PSP_GFX_WRAP_CLAMP;
    state.blend = 1; state.depthTest = 1;
    state.projectionMatrix = projection; state.projectionSerial = projectionSerial;
    state.viewport = viewport;
    PspGfxBackendGu_DrawReservedTriangles(&reservation, count * 6, &state);
    return 1;
}

void PspAquasParticles_Restore(Gfx* output, const Gfx* record, int draw) {
    static Gfx orbState[10];
    static int ready;
    u32 colour = record[1].words.w1;
    u32 tint = sTint[(colour >> 8) & 3];
    unsigned i;
    if (!ready) {
        for (i = 0; i < 9; i++) orbState[i] = aOrbDL[i];
        orbState[9] = aOrbDL[10];
        ready = 1;
    }
    gSPDisplayList(output++, gRcpSetupDLs[SETUPDL_67]);
    gDPSetPrimColor(output++, 0, 0, 255, 255, 255, colour & 255);
    gDPSetEnvColor(output++, (tint >> 16) & 255, (tint >> 8) & 255, tint & 255, 255);
    PSP_RENDERER_DL_MTXF(output++, (const Mtx*) (uintptr_t) record[1].words.w0,
                         G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(output++, draw ? aOrbDL : orbState);
    gSPDisplayList(output++, gRcpSetupDLs[SETUPDL_64]);
    gSPEndDisplayList(output++);
}
