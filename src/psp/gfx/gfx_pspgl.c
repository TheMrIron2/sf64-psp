#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/gfx/gfx_pspgl.h"
#include "src/psp/profiler.h"
#include "macros.h"

#include <GLES/gl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef PSP_RENDERER_DIAGNOSTICS
#define PSP_RENDERER_DIAGNOSTICS 0
#endif

#if PSP_RENDERER_DIAGNOSTICS
/*
 * Declared directly (instead of including src/psp/platform.h) because that
 * header pulls in sf64thread.h -> libultra/ultra64.h, whose PR/os_libc.h
 * bcmp/bcopy/bzero prototypes conflict with the PSP SDK's own <strings.h>
 * once <string.h> is in scope, which it already is in this file.
 */
void PspPlatform_LogLine(const char* line);
#endif

#define PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE 96
#define PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE 64
#define PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE 96
#define PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE 48
#define PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS (256 * 32)
#define PSP_GFX_PSPGL_MIN_TEXTURE_DIMENSION 8
#define PSP_GFX_PSPGL_VERTEX_STREAM_SETS 2
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET 256
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_VERTICES 256
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET 32
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_VERTICES 3072
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SETS * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET)
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_VERTICES * sizeof(PspGfxPspglColorVertex))
#define PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_VERTICES * sizeof(PspGfxPspglColorVertex))
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_ARENA_BYTES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGES_PER_SET * PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_BYTES)
#define PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_ARENA_VERTICES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_ARENA_BYTES / sizeof(PspGfxPspglColorVertex))
#define PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES \
    (PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_ARENA_BYTES + \
     (PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES))
#define PSP_GFX_PSPGL_TEXTURE_PARAMETER_FALLBACK_CACHE_SIZE 32
#define PSP_GFX_PSPGL_N64_WIDTH 320.0f
#define PSP_GFX_PSPGL_N64_HEIGHT 240.0f
#define PSP_GFX_PSPGL_SCREEN_MARGIN 8.0f
#define PSP_GFX_PSPGL_BLACK 0xFF000000u

#ifndef SF64_PSP_PSPGL_VBO_STREAM
#define SF64_PSP_PSPGL_VBO_STREAM 1
#endif
#ifndef SF64_PSP_TEXTURE_WRAP_CACHE
#define SF64_PSP_TEXTURE_WRAP_CACHE 1
#endif
#ifndef SF64_PSP_PROFILE_VERTEX_REUSE
#define SF64_PSP_PROFILE_VERTEX_REUSE 0
#endif

struct PspGfxPspglTextureParameterState {
    GLuint texture;
    GLint wrapS;
    GLint wrapT;
    GLint minFilter;
    GLint magFilter;
    u32 generation;
    int valid;
};

typedef struct {
    const u8* indices;
    const u16* palette;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    GLuint texture;
    PspGfxPspglTextureParameterState parameterState;
} PspGfxTextureCacheEntry;

typedef struct {
    const u16* pixels;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    int premultiplied;
    GLuint texture;
    PspGfxPspglTextureParameterState parameterState;
} PspGfxRgba16TextureCacheEntry;

typedef struct {
    const void* pixels;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    int premultiplied;
    int envBlend;
    u32 primitiveColor;
    u32 environmentColor;
    GLuint texture;
    PspGfxPspglTextureParameterState parameterState;
} PspGfxRgba32TextureCacheEntry;

typedef enum {
    PSP_GFX_TEXTURE_CI4,
    PSP_GFX_TEXTURE_IA8,
    PSP_GFX_TEXTURE_IA16,
} PspGfxConvertedTextureFormat;

typedef struct {
    const void* pixels;
    const u16* palette;
    u32 width;
    u32 height;
    u32 uploadWidth;
    u32 uploadHeight;
    PspGfxConvertedTextureFormat format;
    int envBlend;
    u32 primitiveColor;
    u32 environmentColor;
    u32 colorTransfer;
    GLuint texture;
    PspGfxPspglTextureParameterState parameterState;
} PspGfxConvertedTextureCacheEntry;

typedef struct {
    int matrixModeValid;
    GLenum matrixMode;
    int projectionValid;
    int projectionIdentity;
    GLfloat projectionMatrix[16];
    int modelviewIdentityValid;
    int texCoordArrayValid;
    int texCoordArrayEnabled;
    int colorArrayValid;
    int colorArrayEnabled;
    int vertexArrayValid;
    int vertexArrayEnabled;
    int depthTestValid;
    int depthTestEnabled;
    int depthFuncValid;
    GLenum depthFunc;
    int depthMaskValid;
    GLboolean depthMask;
    int fogValid;
    int fogEnabled;
    int fogModeValid;
    GLfloat fogMode;
    int fogColorValid;
    GLfloat fogColor[4];
    int fogStartValid;
    GLfloat fogStart;
    int fogEndValid;
    GLfloat fogEnd;
    int texture2DValid;
    int texture2DEnabled;
    int alphaTestValid;
    int alphaTestEnabled;
    int alphaFuncValid;
    GLenum alphaFunc;
    GLfloat alphaRef;
    int blendValid;
    int blendEnabled;
    int blendFuncValid;
    GLenum blendSrc;
    GLenum blendDst;
    int boundTextureValid;
    GLuint boundTexture;
    PspGfxPspglTextureRef boundTextureRef;
    int textureEnvModeValid;
    GLint textureEnvMode;
    int textureEnvColorValid;
    GLfloat textureEnvColor[4];
} PspGfxPspglStateCache;

#if SF64_PSP_PSPGL_VBO_STREAM
typedef struct {
    GLuint buffer;
} PspGfxVertexStreamPage;
#endif

static PspGfxTextureCacheEntry sTextureCache[PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE];
static PspGfxRgba16TextureCacheEntry sRgba16TextureCache[PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE];
static PspGfxRgba32TextureCacheEntry sRgba32TextureCache[PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE];
static PspGfxConvertedTextureCacheEntry sConvertedTextureCache[PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE];
#if SF64_PSP_PSPGL_VBO_STREAM
static PspGfxVertexStreamPage sVertexStreamSmallArenas[PSP_GFX_PSPGL_VERTEX_STREAM_SETS];
static PspGfxVertexStreamPage sVertexStreamLargePages[PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT];
#endif
static u8 sTextureUpload[PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS * 4];
static u32 sTextureCacheCount;
static u32 sRgba16TextureCacheCount;
static u32 sRgba32TextureCacheCount;
static u32 sTextureCacheReplaceIndex;
static u32 sRgba16TextureCacheReplaceIndex;
static u32 sRgba32TextureCacheReplaceIndex;
static u32 sConvertedTextureCacheCount;
static u32 sConvertedTextureCacheReplaceIndex;
static u32 sVertexStreamSetIndex;
static u32 sVertexStreamSmallArenaVertexIndex;
static u32 sVertexStreamLargePageIndex;
static int sVertexStreamInitialized;
static int sVertexStreamAvailable;
static u32 sTextureParameterGeneration;
#if PSP_RENDERER_DIAGNOSTICS
static u32 sInvalidTextureRefDiagCount;
#endif
#if SF64_PSP_TEXTURE_WRAP_CACHE
/*
 * Resident texture cache entries own the authoritative wrap state for their
 * PSPGL texture object. The fallback cache only covers unexpected external
 * texture ids and is invalidated conservatively when a known texture is
 * deleted so recycled GLuint values cannot inherit stale parameters.
 */
static PspGfxPspglTextureParameterState
    sTextureParameterFallbackCache[PSP_GFX_PSPGL_TEXTURE_PARAMETER_FALLBACK_CACHE_SIZE];
#endif
#if SF64_PSP_PSPGL_VBO_STREAM
static void* sVertexStreamSmallArenaMapped;
static u32 sVertexStreamSmallArenaMappedSet;
static int sVertexStreamSmallArenaExhausted;
#endif
static PspGfxPspglStateCache sStateCache;

static void psp_gfx_pspgl_invalidate_state_cache(void) {
    memset(&sStateCache, 0, sizeof(sStateCache));
}

static PspGfxPspglTextureRef psp_gfx_pspgl_null_texture_ref(void) {
    PspGfxPspglTextureRef ref;

    ref.state = NULL;
    ref.texture = 0;
    ref.generation = 0;
    return ref;
}

static PspGfxPspglTextureRef psp_gfx_pspgl_texture_ref(PspGfxPspglTextureParameterState* state) {
    PspGfxPspglTextureRef ref = psp_gfx_pspgl_null_texture_ref();

    if (state != NULL) {
        ref.state = state;
        ref.texture = state->texture;
        ref.generation = state->generation;
    }
    return ref;
}

static int psp_gfx_pspgl_texture_ref_valid(GLuint texture, const PspGfxPspglTextureRef* ref) {
    return (ref != NULL) && (ref->state != NULL) && ref->state->valid && (ref->texture == texture) &&
           (ref->state->texture == texture) && (ref->generation == ref->state->generation);
}

static void psp_gfx_pspgl_note_bound_texture(GLuint texture, PspGfxPspglTextureRef textureRef) {
    sStateCache.boundTextureValid = 1;
    sStateCache.boundTexture = texture;
    if (psp_gfx_pspgl_texture_ref_valid(texture, &textureRef)) {
        sStateCache.boundTextureRef = textureRef;
    } else {
        sStateCache.boundTextureRef = psp_gfx_pspgl_null_texture_ref();
    }
}

static void psp_gfx_pspgl_invalidate_bound_texture(void) {
    sStateCache.boundTextureValid = 0;
    sStateCache.boundTextureRef = psp_gfx_pspgl_null_texture_ref();
}

static void psp_gfx_pspgl_init_texture_parameter_state(PspGfxPspglTextureParameterState* state, GLuint texture) {
    state->texture = texture;
    state->wrapS = GL_CLAMP_TO_EDGE;
    state->wrapT = GL_CLAMP_TO_EDGE;
    state->minFilter = GL_LINEAR;
    state->magFilter = GL_LINEAR;
    state->generation = ++sTextureParameterGeneration;
    state->valid = 1;
}

static void psp_gfx_pspgl_invalidate_texture_parameter_state(PspGfxPspglTextureParameterState* state) {
#if SF64_PSP_TEXTURE_WRAP_CACHE
    if (state->valid) {
        PspProfiler_CountTextureParameterCacheReplacement();
    }
#endif
    state->texture = 0;
    state->wrapS = GL_CLAMP_TO_EDGE;
    state->wrapT = GL_CLAMP_TO_EDGE;
    state->generation = ++sTextureParameterGeneration;
    state->valid = 0;
}

#if SF64_PSP_TEXTURE_WRAP_CACHE
static void psp_gfx_pspgl_invalidate_fallback_texture_parameter_state(GLuint texture) {
    u32 slot = texture % PSP_GFX_PSPGL_TEXTURE_PARAMETER_FALLBACK_CACHE_SIZE;
    PspGfxPspglTextureParameterState* state = &sTextureParameterFallbackCache[slot];

    if (state->valid && (state->texture == texture)) {
        psp_gfx_pspgl_invalidate_texture_parameter_state(state);
    }
}

static PspGfxPspglTextureParameterState* psp_gfx_pspgl_record_fallback_texture_parameter_state(GLuint texture,
                                                                                               GLint wrapS,
                                                                                               GLint wrapT) {
    u32 slot = texture % PSP_GFX_PSPGL_TEXTURE_PARAMETER_FALLBACK_CACHE_SIZE;
    PspGfxPspglTextureParameterState* state = &sTextureParameterFallbackCache[slot];

    if (state->valid && (state->texture != texture)) {
        PspProfiler_CountTextureParameterCacheReplacement();
    }
    state->texture = texture;
    state->wrapS = wrapS;
    state->wrapT = wrapT;
    state->valid = 1;
    return state;
}

static void psp_gfx_pspgl_set_texture_wrap(GLuint texture, GLint wrapS, GLint wrapT) {
    PspGfxPspglTextureParameterState* state;
    u32 fallbackSlot;

    PspProfiler_CountTextureWrapRequest(1, 1);
    if (psp_gfx_pspgl_texture_ref_valid(texture, &sStateCache.boundTextureRef)) {
        state = sStateCache.boundTextureRef.state;
    } else {
        fallbackSlot = texture % PSP_GFX_PSPGL_TEXTURE_PARAMETER_FALLBACK_CACHE_SIZE;
        state = &sTextureParameterFallbackCache[fallbackSlot];
        if (!state->valid || (state->texture != texture)) {
            PspProfiler_CountTextureParameterCacheMiss();
            PspProfiler_CountTextureWrapCall(1, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
            psp_gfx_pspgl_record_fallback_texture_parameter_state(texture, wrapS, wrapT);
            return;
        }
    }
    if (state->wrapS != wrapS) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        state->wrapS = wrapS;
        PspProfiler_CountTextureWrapCall(1, 0);
    } else {
        PspProfiler_CountTextureWrapSkip(1, 0);
    }
    if (state->wrapT != wrapT) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
        state->wrapT = wrapT;
        PspProfiler_CountTextureWrapCall(0, 1);
    } else {
        PspProfiler_CountTextureWrapSkip(0, 1);
    }
}
#else
static void psp_gfx_pspgl_set_texture_wrap(GLuint texture, GLint wrapS, GLint wrapT) {
    (void) texture;
    PspProfiler_CountTextureWrapRequest(1, 1);
    PspProfiler_CountTextureWrapCall(1, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
}
#endif

static void psp_gfx_pspgl_set_texture_filter(GLuint texture, GLint minFilter, GLint magFilter) {
    PspGfxPspglTextureParameterState* state = NULL;

    if (psp_gfx_pspgl_texture_ref_valid(texture, &sStateCache.boundTextureRef)) {
        state = sStateCache.boundTextureRef.state;
    }
    if ((state == NULL) || !state->valid || (state->minFilter != minFilter)) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        if (state != NULL) {
            state->minFilter = minFilter;
        }
    }
    if ((state == NULL) || !state->valid || (state->magFilter != magFilter)) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
        if (state != NULL) {
            state->magFilter = magFilter;
        }
    }
}

static int psp_gfx_pspgl_float_equal(GLfloat a, GLfloat b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static int psp_gfx_pspgl_float4_equal(const GLfloat* a, const GLfloat* b) {
    return memcmp(a, b, sizeof(GLfloat) * 4) == 0;
}

static void psp_gfx_pspgl_matrix_mode(GLenum mode) {
    if (!sStateCache.matrixModeValid || (sStateCache.matrixMode != mode)) {
        glMatrixMode(mode);
        sStateCache.matrixModeValid = 1;
        sStateCache.matrixMode = mode;
    }
}

static void psp_gfx_pspgl_load_projection_identity(void) {
    psp_gfx_pspgl_matrix_mode(GL_PROJECTION);
    if (!sStateCache.projectionValid || !sStateCache.projectionIdentity) {
        glLoadIdentity();
        sStateCache.projectionValid = 1;
        sStateCache.projectionIdentity = 1;
    }
}

static void psp_gfx_pspgl_load_projection_matrix(const GLfloat* matrix) {
    psp_gfx_pspgl_matrix_mode(GL_PROJECTION);
    if (!sStateCache.projectionValid || sStateCache.projectionIdentity ||
        (memcmp(sStateCache.projectionMatrix, matrix, sizeof(sStateCache.projectionMatrix)) != 0)) {
        glLoadMatrixf(matrix);
        sStateCache.projectionValid = 1;
        sStateCache.projectionIdentity = 0;
        memcpy(sStateCache.projectionMatrix, matrix, sizeof(sStateCache.projectionMatrix));
    }
}

static void psp_gfx_pspgl_load_modelview_identity(void) {
    psp_gfx_pspgl_matrix_mode(GL_MODELVIEW);
    if (!sStateCache.modelviewIdentityValid) {
        glLoadIdentity();
        sStateCache.modelviewIdentityValid = 1;
    }
}

static void psp_gfx_pspgl_client_state(GLenum array, int enabled, int* valid, int* cached) {
    if (!*valid || (*cached != enabled)) {
        if (enabled) {
            glEnableClientState(array);
        } else {
            glDisableClientState(array);
        }
        *valid = 1;
        *cached = enabled;
    }
}

static void psp_gfx_pspgl_enable_client_arrays(void) {
    psp_gfx_pspgl_client_state(GL_TEXTURE_COORD_ARRAY, 1, &sStateCache.texCoordArrayValid,
                               &sStateCache.texCoordArrayEnabled);
    psp_gfx_pspgl_client_state(GL_COLOR_ARRAY, 1, &sStateCache.colorArrayValid, &sStateCache.colorArrayEnabled);
    psp_gfx_pspgl_client_state(GL_VERTEX_ARRAY, 1, &sStateCache.vertexArrayValid, &sStateCache.vertexArrayEnabled);
}

static void psp_gfx_pspgl_capability(GLenum capability, int enabled, int* valid, int* cached) {
    if (!*valid || (*cached != enabled)) {
        if (enabled) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
        *valid = 1;
        *cached = enabled;
    }
}

static void psp_gfx_pspgl_depth_test(int enabled) {
    psp_gfx_pspgl_capability(GL_DEPTH_TEST, enabled, &sStateCache.depthTestValid, &sStateCache.depthTestEnabled);
}

static void psp_gfx_pspgl_depth_func(GLenum func) {
    if (!sStateCache.depthFuncValid || (sStateCache.depthFunc != func)) {
        glDepthFunc(func);
        sStateCache.depthFuncValid = 1;
        sStateCache.depthFunc = func;
    }
}

static void psp_gfx_pspgl_depth_mask(GLboolean mask) {
    if (!sStateCache.depthMaskValid || (sStateCache.depthMask != mask)) {
        glDepthMask(mask);
        sStateCache.depthMaskValid = 1;
        sStateCache.depthMask = mask;
    }
}

static void psp_gfx_pspgl_fog_disabled(void) {
    psp_gfx_pspgl_capability(GL_FOG, 0, &sStateCache.fogValid, &sStateCache.fogEnabled);
}

static void psp_gfx_pspgl_fog_linear(const GLfloat* color, GLfloat start, GLfloat end) {
    static const GLfloat mode = GL_LINEAR;

    psp_gfx_pspgl_capability(GL_FOG, 1, &sStateCache.fogValid, &sStateCache.fogEnabled);
    if (!sStateCache.fogModeValid || !psp_gfx_pspgl_float_equal(sStateCache.fogMode, mode)) {
        glFogf(GL_FOG_MODE, mode);
        sStateCache.fogModeValid = 1;
        sStateCache.fogMode = mode;
    }
    if (!sStateCache.fogColorValid || !psp_gfx_pspgl_float4_equal(sStateCache.fogColor, color)) {
        glFogfv(GL_FOG_COLOR, color);
        sStateCache.fogColorValid = 1;
        memcpy(sStateCache.fogColor, color, sizeof(sStateCache.fogColor));
    }
    if (!sStateCache.fogStartValid || !psp_gfx_pspgl_float_equal(sStateCache.fogStart, start)) {
        glFogf(GL_FOG_START, start);
        sStateCache.fogStartValid = 1;
        sStateCache.fogStart = start;
    }
    if (!sStateCache.fogEndValid || !psp_gfx_pspgl_float_equal(sStateCache.fogEnd, end)) {
        glFogf(GL_FOG_END, end);
        sStateCache.fogEndValid = 1;
        sStateCache.fogEnd = end;
    }
}

static void psp_gfx_pspgl_texture_2d(int enabled) {
    psp_gfx_pspgl_capability(GL_TEXTURE_2D, enabled, &sStateCache.texture2DValid, &sStateCache.texture2DEnabled);
}

static void psp_gfx_pspgl_alpha_test(int enabled) {
    psp_gfx_pspgl_capability(GL_ALPHA_TEST, enabled, &sStateCache.alphaTestValid, &sStateCache.alphaTestEnabled);
}

static void psp_gfx_pspgl_alpha_func(GLenum func, GLfloat ref) {
    if (!sStateCache.alphaFuncValid || (sStateCache.alphaFunc != func) ||
        !psp_gfx_pspgl_float_equal(sStateCache.alphaRef, ref)) {
        glAlphaFunc(func, ref);
        sStateCache.alphaFuncValid = 1;
        sStateCache.alphaFunc = func;
        sStateCache.alphaRef = ref;
    }
}

static void psp_gfx_pspgl_blend(int enabled) {
    psp_gfx_pspgl_capability(GL_BLEND, enabled, &sStateCache.blendValid, &sStateCache.blendEnabled);
}

static void psp_gfx_pspgl_blend_func(GLenum src, GLenum dst) {
    if (!sStateCache.blendFuncValid || (sStateCache.blendSrc != src) || (sStateCache.blendDst != dst)) {
        glBlendFunc(src, dst);
        sStateCache.blendFuncValid = 1;
        sStateCache.blendSrc = src;
        sStateCache.blendDst = dst;
    }
}

static void psp_gfx_pspgl_bind_texture(GLuint texture, PspGfxPspglTextureRef textureRef) {
    if (!sStateCache.boundTextureValid || (sStateCache.boundTexture != texture)) {
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    psp_gfx_pspgl_note_bound_texture(texture, textureRef);
}

static void psp_gfx_pspgl_texture_env_mode(GLint mode) {
    if (!sStateCache.textureEnvModeValid || (sStateCache.textureEnvMode != mode)) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
        sStateCache.textureEnvModeValid = 1;
        sStateCache.textureEnvMode = mode;
    }
}

static void psp_gfx_pspgl_texture_env_color(u32 color) {
    GLfloat rgba[4];

    rgba[0] = (GLfloat) (color & 0xFFU) / 255.0f;
    rgba[1] = (GLfloat) ((color >> 8) & 0xFFU) / 255.0f;
    rgba[2] = (GLfloat) ((color >> 16) & 0xFFU) / 255.0f;
    rgba[3] = (GLfloat) ((color >> 24) & 0xFFU) / 255.0f;

    if (!sStateCache.textureEnvColorValid || !psp_gfx_pspgl_float4_equal(sStateCache.textureEnvColor, rgba)) {
        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, rgba);
        sStateCache.textureEnvColorValid = 1;
        memcpy(sStateCache.textureEnvColor, rgba, sizeof(sStateCache.textureEnvColor));
    }
}

#if SF64_PSP_PROFILE_PHASES
static u64 psp_gfx_pspgl_hash_u64(u64 hash, u64 value) {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

static u64 psp_gfx_pspgl_hash_ptr(u64 hash, const void* ptr) {
    return psp_gfx_pspgl_hash_u64(hash, (u64) (unsigned long) ptr);
}

static u64 psp_gfx_pspgl_hash_start(u32 cacheClass) {
    return psp_gfx_pspgl_hash_u64(0xCBF29CE484222325ULL, cacheClass);
}

static u64 psp_gfx_pspgl_ci8_key_hash(const u8* indices, const u16* palette, u32 width, u32 height) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CI8);

    hash = psp_gfx_pspgl_hash_ptr(hash, indices);
    hash = psp_gfx_pspgl_hash_ptr(hash, palette);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    return hash;
}

static u64 psp_gfx_pspgl_ci8_base_hash(const u8* indices) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CI8);

    return psp_gfx_pspgl_hash_ptr(hash, indices);
}

static u64 psp_gfx_pspgl_rgba16_key_hash(const u16* pixels, u32 width, u32 height, int premultiply) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_RGBA16);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    hash = psp_gfx_pspgl_hash_u64(hash, premultiply ? 1 : 0);
    return hash;
}

static u64 psp_gfx_pspgl_rgba16_base_hash(const u16* pixels) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_RGBA16);

    return psp_gfx_pspgl_hash_ptr(hash, pixels);
}

static u64 psp_gfx_pspgl_rgba32_key_hash(const void* pixels, u32 width, u32 height, int premultiply, int envBlend,
                                         u32 primitiveColor, u32 environmentColor) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_RGBA32);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    hash = psp_gfx_pspgl_hash_u64(hash, premultiply ? 1 : 0);
    hash = psp_gfx_pspgl_hash_u64(hash, envBlend ? 1 : 0);
    hash = psp_gfx_pspgl_hash_u64(hash, primitiveColor);
    hash = psp_gfx_pspgl_hash_u64(hash, environmentColor);
    return hash;
}

static u64 psp_gfx_pspgl_rgba32_base_hash(const void* pixels) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_RGBA32);

    return psp_gfx_pspgl_hash_ptr(hash, pixels);
}

static u64 psp_gfx_pspgl_converted_key_hash(const void* pixels, const u16* palette, u32 width, u32 height,
                                            PspGfxConvertedTextureFormat format, int envBlend, u32 primitiveColor,
                                            u32 environmentColor) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CONVERTED);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_ptr(hash, palette);
    hash = psp_gfx_pspgl_hash_u64(hash, width);
    hash = psp_gfx_pspgl_hash_u64(hash, height);
    hash = psp_gfx_pspgl_hash_u64(hash, format);
    hash = psp_gfx_pspgl_hash_u64(hash, envBlend ? 1 : 0);
    hash = psp_gfx_pspgl_hash_u64(hash, primitiveColor);
    hash = psp_gfx_pspgl_hash_u64(hash, environmentColor);
    hash = psp_gfx_pspgl_hash_u64(hash, SF64_PSP_COLOR_TRANSFER);
    return hash;
}

static u64 psp_gfx_pspgl_converted_base_hash(const void* pixels, PspGfxConvertedTextureFormat format) {
    u64 hash = psp_gfx_pspgl_hash_start(PSP_PROFILE_TEXTURE_CACHE_CONVERTED);

    hash = psp_gfx_pspgl_hash_ptr(hash, pixels);
    hash = psp_gfx_pspgl_hash_u64(hash, format);
    return hash;
}
#endif

u8 gPspGfxColorTransferLut[256];
static int sColorTransferInitialized;

void PspGfxPspgl_InitColorTransfer(void) {
    u32 i;

    if (sColorTransferInitialized) {
        return;
    }
    /* Match the N64 brightness response used by the Dreamcast renderer. */
    for (i = 0; i < 256; i++) {
        gPspGfxColorTransferLut[i] = (u8) (255.0f * sqrtf((float) i / 255.0f));
    }
    sColorTransferInitialized = 1;
}

/* Decodes to transformed RGB (transfer policy) with raw alpha; also covers
 * CI4/CI8 palette entries, which route through this conversion. */
static void psp_gfx_pspgl_rgba16_to_rgba8(u16 color, u8* out) {
    out[0] = psp_gfx_color_transfer_u8((u8) (((color >> 11) & 0x1F) * 255 / 31));
    out[1] = psp_gfx_color_transfer_u8((u8) (((color >> 6) & 0x1F) * 255 / 31));
    out[2] = psp_gfx_color_transfer_u8((u8) (((color >> 1) & 0x1F) * 255 / 31));
    out[3] = (color & 1) ? 255 : 0;
}

static u16 psp_gfx_pspgl_read_u16(const void* base, u32 index) {
    const u8* bytes = (const u8*) base + (index * 2);

    return (u16) bytes[0] | ((u16) bytes[1] << 8);
}

/*
 * RGBA32 texels are stored as native-endian u32 values of the form
 * 0xRRGGBBAA (the asset .inc.c literals), not as big-endian byte streams.
 */
static u32 psp_gfx_pspgl_read_n64_rgba32(const void* base, u32 index) {
    const u8* bytes = (const u8*) base + (index * 4);

    return ((u32) bytes[3] << 24) | ((u32) bytes[2] << 16) | ((u32) bytes[1] << 8) | (u32) bytes[0];
}

static int psp_gfx_pspgl_is_dark_rgba16_mask(const u16* pixels, u32 width, u32 height) {
    u32 pixelCount = width * height;
    u32 opaqueCount = 0;
    u32 i;

    for (i = 0; i < pixelCount; i++) {
        u16 color = psp_gfx_pspgl_read_u16(pixels, i);

        if ((color & 1) == 0) {
            continue;
        }
        opaqueCount++;
        if (((color >> 11) & 0x1F) > 2 || ((color >> 6) & 0x1F) > 2 || ((color >> 1) & 0x1F) > 2) {
            return 0;
        }
    }
    return (opaqueCount != 0) && (opaqueCount != pixelCount);
}

static u8 psp_gfx_pspgl_filtered_rgba16_alpha(const u16* pixels, u32 width, u32 height, u32 x, u32 y) {
    static const u32 weights[3] = { 1, 2, 1 };
    u32 alpha = 0;
    s32 ky;
    s32 kx;

    for (ky = -1; ky <= 1; ky++) {
        s32 sampleY = (s32) y + ky;

        if (sampleY < 0) {
            sampleY = 0;
        } else if (sampleY >= (s32) height) {
            sampleY = (s32) height - 1;
        }
        for (kx = -1; kx <= 1; kx++) {
            s32 sampleX = (s32) x + kx;
            u32 weight = weights[kx + 1] * weights[ky + 1];

            if (sampleX < 0) {
                sampleX = 0;
            } else if (sampleX >= (s32) width) {
                sampleX = (s32) width - 1;
            }
            if ((psp_gfx_pspgl_read_u16(pixels, ((u32) sampleY * width) + (u32) sampleX) & 1) != 0) {
                alpha += 255U * weight;
            }
        }
    }
    return (u8) ((alpha + 8U) / 16U);
}

static u32 psp_gfx_pspgl_next_power_of_two(u32 value) {
    u32 result = PSP_GFX_PSPGL_MIN_TEXTURE_DIMENSION;

    while (result < value) {
        result <<= 1;
    }
    return result;
}

#if SF64_PSP_PSPGL_VBO_STREAM
static const GLvoid* psp_gfx_pspgl_vertex_offset(u32 offset) {
    return (const GLvoid*) offset;
}
#endif

static void psp_gfx_pspgl_bind_client_arrays(const PspGfxPspglColorVertex* vertices) {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(PspGfxPspglColorVertex), &vertices[0].color);
    glVertexPointer(3, GL_FLOAT, sizeof(PspGfxPspglColorVertex), &vertices[0].x);
}

#if SF64_PSP_PSPGL_VBO_STREAM
static void psp_gfx_pspgl_bind_vbo_arrays(GLuint buffer) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glTexCoordPointer(2, GL_FLOAT, sizeof(PspGfxPspglColorVertex),
                      psp_gfx_pspgl_vertex_offset(offsetof(PspGfxPspglColorVertex, u)));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(PspGfxPspglColorVertex),
                   psp_gfx_pspgl_vertex_offset(offsetof(PspGfxPspglColorVertex, color)));
    glVertexPointer(3, GL_FLOAT, sizeof(PspGfxPspglColorVertex),
                    psp_gfx_pspgl_vertex_offset(offsetof(PspGfxPspglColorVertex, x)));
}

static void psp_gfx_pspgl_unmap_small_arena(void) {
    if (sVertexStreamSmallArenaMapped == NULL) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, sVertexStreamSmallArenas[sVertexStreamSmallArenaMappedSet].buffer);
    glUnmapBuffer(GL_ARRAY_BUFFER);
    sVertexStreamSmallArenaMapped = NULL;
}
#endif

static void psp_gfx_pspgl_init_vertex_stream(void) {
    u32 i;

    if (sVertexStreamInitialized) {
        return;
    }
    sVertexStreamInitialized = 1;
#if SF64_PSP_PSPGL_VBO_STREAM
    sVertexStreamAvailable = 1;
    glGenBuffers(PSP_GFX_PSPGL_VERTEX_STREAM_SETS, &sVertexStreamSmallArenas[0].buffer);
    for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_SETS; i++) {
        if (sVertexStreamSmallArenas[i].buffer == 0) {
            sVertexStreamAvailable = 0;
            break;
        }
        glBindBuffer(GL_ARRAY_BUFFER, sVertexStreamSmallArenas[i].buffer);
        glBufferData(GL_ARRAY_BUFFER, PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_ARENA_BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    glGenBuffers(PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT, &sVertexStreamLargePages[0].buffer);
    for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT; i++) {
        if (sVertexStreamLargePages[i].buffer == 0) {
            sVertexStreamAvailable = 0;
            break;
        }
        glBindBuffer(GL_ARRAY_BUFFER, sVertexStreamLargePages[i].buffer);
        glBufferData(GL_ARRAY_BUFFER, PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES, NULL, GL_DYNAMIC_DRAW);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (!sVertexStreamAvailable) {
        for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_SETS; i++) {
            if (sVertexStreamSmallArenas[i].buffer != 0) {
                glDeleteBuffers(1, &sVertexStreamSmallArenas[i].buffer);
                sVertexStreamSmallArenas[i].buffer = 0;
            }
        }
        for (i = 0; i < PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_COUNT; i++) {
            if (sVertexStreamLargePages[i].buffer != 0) {
                glDeleteBuffers(1, &sVertexStreamLargePages[i].buffer);
                sVertexStreamLargePages[i].buffer = 0;
            }
        }
    }
#else
    (void) i;
    sVertexStreamAvailable = 0;
#endif
}

static void psp_gfx_pspgl_reset_vertex_stream(void) {
    if (!sVertexStreamAvailable) {
        return;
    }
#if SF64_PSP_PSPGL_VBO_STREAM
    psp_gfx_pspgl_unmap_small_arena();
#endif
    sVertexStreamSetIndex = (sVertexStreamSetIndex + 1) % PSP_GFX_PSPGL_VERTEX_STREAM_SETS;
    sVertexStreamSmallArenaVertexIndex = 0;
    sVertexStreamLargePageIndex = 0;
#if SF64_PSP_PSPGL_VBO_STREAM
    sVertexStreamSmallArenaExhausted = 0;
#endif
}

static int psp_gfx_pspgl_find_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                                PspGfxConvertedTextureFormat format, u32* textureId,
                                                PspGfxPspglTextureRef* textureRef, u32* uploadWidth,
                                                u32* uploadHeight, int envBlend, u32 primitiveColor,
                                                u32 environmentColor, int countHit) {
    PspGfxConvertedTextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (textureId == NULL) || (textureRef == NULL) ||
        (uploadWidth == NULL) || (uploadHeight == NULL) || ((format == PSP_GFX_TEXTURE_CI4) && (palette == NULL))) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_converted_key_hash(pixels, palette, width, height, format, envBlend, primitiveColor,
                                               environmentColor);
    baseHash = psp_gfx_pspgl_converted_base_hash(pixels, format);
#endif
    for (i = 0; i < sConvertedTextureCacheCount; i++) {
        entry = &sConvertedTextureCache[i];
        if ((entry->pixels == pixels) && (entry->palette == palette) && (entry->width == width) &&
            (entry->height == height) && (entry->format == format) && (entry->envBlend == envBlend) &&
            (entry->primitiveColor == primitiveColor) && (entry->environmentColor == environmentColor) &&
            (entry->colorTransfer == SF64_PSP_COLOR_TRANSFER)) {
            *textureId = entry->texture;
            *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
                                                 PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE,
                                                 sConvertedTextureCacheCount, keyHash, baseHash, 1);
#endif
            if (countHit) {
                PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            }
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
                                         PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE, sConvertedTextureCacheCount,
                                         keyHash, baseHash, 0);
#endif
    return 0;
}

static u32 psp_gfx_pspgl_create_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                                  PspGfxConvertedTextureFormat format, int envBlend,
                                                  u32 primitiveColor, u32 environmentColor, u32* uploadWidth,
                                                  u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    PspGfxConvertedTextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) || (uploadHeight == NULL) ||
        (textureRef == NULL) || ((format == PSP_GFX_TEXTURE_CI4) && (palette == NULL))) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_converted_key_hash(pixels, palette, width, height, format, envBlend, primitiveColor,
                                               environmentColor);
    baseHash = psp_gfx_pspgl_converted_base_hash(pixels, format);
#endif
    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;
            u8* out = &sTextureUpload[dstIndex * 4];

            if (format == PSP_GFX_TEXTURE_CI4) {
                const u8* indices = (const u8*) pixels;
                u8 packed = indices[srcIndex >> 1];
                u8 index = (srcIndex & 1) ? (packed & 0xF) : (packed >> 4);

                psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(palette, index), out);
            } else if (format == PSP_GFX_TEXTURE_IA8) {
                u8 packed = ((const u8*) pixels)[srcIndex];
                u8 intensity = psp_gfx_color_transfer_u8((packed >> 4) * 17);
                u8 alpha = (packed & 0xF) * 17;

                if (envBlend) {
                    u8 pr = (u8) (primitiveColor & 0xFFU);
                    u8 pg = (u8) ((primitiveColor >> 8) & 0xFFU);
                    u8 pb = (u8) ((primitiveColor >> 16) & 0xFFU);
                    u8 pa = (u8) ((primitiveColor >> 24) & 0xFFU);
                    u8 er = (u8) (environmentColor & 0xFFU);
                    u8 eg = (u8) ((environmentColor >> 8) & 0xFFU);
                    u8 eb = (u8) ((environmentColor >> 16) & 0xFFU);

                    out[0] = (u8) (((u32) er * (255U - intensity) + ((u32) pr * intensity) + 127U) / 255U);
                    out[1] = (u8) (((u32) eg * (255U - intensity) + ((u32) pg * intensity) + 127U) / 255U);
                    out[2] = (u8) (((u32) eb * (255U - intensity) + ((u32) pb * intensity) + 127U) / 255U);
                    out[3] = (u8) ((((u32) alpha * pa) + 127U) / 255U);
                } else {
                    out[0] = intensity;
                    out[1] = intensity;
                    out[2] = intensity;
                    out[3] = alpha;
                }
            } else {
                u16 packed = psp_gfx_pspgl_read_u16(pixels, srcIndex);
                u8 intensity = psp_gfx_color_transfer_u8((u8) (packed >> 8));

                out[0] = intensity;
                out[1] = intensity;
                out[2] = intensity;
                out[3] = (u8) packed;
            }
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sConvertedTextureCacheCount < PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE) {
        entry = &sConvertedTextureCache[sConvertedTextureCacheCount++];
    } else {
        entry = &sConvertedTextureCache[sConvertedTextureCacheReplaceIndex++];
        sConvertedTextureCacheReplaceIndex %= PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
            psp_gfx_pspgl_converted_key_hash(entry->pixels, entry->palette, entry->width, entry->height,
                                             entry->format, entry->envBlend, entry->primitiveColor,
                                             entry->environmentColor));
#endif
        psp_gfx_pspgl_invalidate_bound_texture();
#if SF64_PSP_TEXTURE_WRAP_CACHE
        psp_gfx_pspgl_invalidate_fallback_texture_parameter_state(entry->texture);
#endif
        psp_gfx_pspgl_invalidate_texture_parameter_state(&entry->parameterState);
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->palette = palette;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->format = format;
    entry->envBlend = envBlend;
    entry->primitiveColor = primitiveColor;
    entry->environmentColor = environmentColor;
    entry->colorTransfer = SF64_PSP_COLOR_TRANSFER;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_CONVERTED,
                                            PSP_GFX_PSPGL_CONVERTED_TEXTURE_CACHE_SIZE, sConvertedTextureCacheCount,
                                            keyHash, baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    psp_gfx_pspgl_init_texture_parameter_state(&entry->parameterState, entry->texture);
    *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
    psp_gfx_pspgl_note_bound_texture(entry->texture, *textureRef);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

static u32 psp_gfx_pspgl_get_converted_texture(const void* pixels, const u16* palette, u32 width, u32 height,
                                               PspGfxConvertedTextureFormat format, u32* uploadWidth,
                                               u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    u32 textureId;

    if (psp_gfx_pspgl_find_converted_texture(pixels, palette, width, height, format, &textureId, textureRef,
                                             uploadWidth, uploadHeight, 0, 0, 0, 1)) {
        return textureId;
    }
    return psp_gfx_pspgl_create_converted_texture(pixels, palette, width, height, format, 0, 0, 0, uploadWidth,
                                                  uploadHeight, textureRef);
}

void PspGfxPspgl_Init(void) {
    PspGfxPspgl_InitColorTransfer();
    psp_gfx_pspgl_invalidate_state_cache();
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glShadeModel(GL_SMOOTH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    psp_gfx_pspgl_init_vertex_stream();
    psp_gfx_pspgl_invalidate_state_cache();
}

void PspGfxPspgl_BeginFrame(void) {
    glViewport(0, 0, PspGfx_GetWidth(), PspGfx_GetHeight());

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    psp_gfx_pspgl_reset_vertex_stream();
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(GL_FALSE);
    psp_gfx_pspgl_invalidate_state_cache();
    PspGfxPspgl_DrawSolidRect(0.0f, 0.0f, PSP_GFX_PSPGL_N64_WIDTH, PSP_GFX_PSPGL_SCREEN_MARGIN,
                              PSP_GFX_PSPGL_BLACK, 0);
    PspGfxPspgl_DrawSolidRect(0.0f, PSP_GFX_PSPGL_N64_HEIGHT - PSP_GFX_PSPGL_SCREEN_MARGIN,
                              PSP_GFX_PSPGL_N64_WIDTH, PSP_GFX_PSPGL_N64_HEIGHT, PSP_GFX_PSPGL_BLACK, 0);
    PspGfxPspgl_DrawSolidRect(0.0f, PSP_GFX_PSPGL_SCREEN_MARGIN, PSP_GFX_PSPGL_SCREEN_MARGIN,
                              PSP_GFX_PSPGL_N64_HEIGHT - PSP_GFX_PSPGL_SCREEN_MARGIN,
                              PSP_GFX_PSPGL_BLACK, 0);
    PspGfxPspgl_DrawSolidRect(PSP_GFX_PSPGL_N64_WIDTH - PSP_GFX_PSPGL_SCREEN_MARGIN,
                              PSP_GFX_PSPGL_SCREEN_MARGIN, PSP_GFX_PSPGL_N64_WIDTH,
                              PSP_GFX_PSPGL_N64_HEIGHT - PSP_GFX_PSPGL_SCREEN_MARGIN,
                              PSP_GFX_PSPGL_BLACK, 0);
    psp_gfx_pspgl_invalidate_state_cache();
}

void PspGfxPspgl_Flush(void) {
    /*
     * PSPGL records state and draws in order, copies client-array vertices
     * before glDrawArrays returns, and submits internally when its GE list
     * fills. Keep explicit submission at the frame/task boundary.
     */
#if SF64_PSP_PSPGL_VBO_STREAM
    psp_gfx_pspgl_unmap_small_arena();
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_GL_FLUSH);
    glFlush();
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_GL_FLUSH);
    PspProfiler_CountGlFlush();
}

int PspGfxPspgl_FindCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight) {
    PspGfxTextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((indices == NULL) || (palette == NULL) || (width == 0) || (height == 0) || (textureId == NULL) ||
        (textureRef == NULL) || (uploadWidth == NULL) || (uploadHeight == NULL)) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_ci8_key_hash(indices, palette, width, height);
    baseHash = psp_gfx_pspgl_ci8_base_hash(indices);
#endif
    for (i = 0; i < sTextureCacheCount; i++) {
        entry = &sTextureCache[i];
        if ((entry->indices == indices) && (entry->palette == palette) && (entry->width == width) &&
            (entry->height == height)) {
            *textureId = entry->texture;
            *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CI8, PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE,
                                                 sTextureCacheCount, keyHash, baseHash, 1);
#endif
            PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_CI8, PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE,
                                         sTextureCacheCount, keyHash, baseHash, 0);
#endif
    return 0;
}

u32 PspGfxPspgl_CreateCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    PspGfxTextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;

    if ((indices == NULL) || (palette == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) ||
        (uploadHeight == NULL) || (textureRef == NULL)) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_ci8_key_hash(indices, palette, width, height);
    baseHash = psp_gfx_pspgl_ci8_base_hash(indices);
#endif

    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;

            psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(palette, indices[srcIndex]), &sTextureUpload[dstIndex * 4]);
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sTextureCacheCount < PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE) {
        entry = &sTextureCache[sTextureCacheCount++];
    } else {
        entry = &sTextureCache[sTextureCacheReplaceIndex++];
        sTextureCacheReplaceIndex %= PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_CI8,
            psp_gfx_pspgl_ci8_key_hash(entry->indices, entry->palette, entry->width, entry->height));
#endif
        psp_gfx_pspgl_invalidate_bound_texture();
#if SF64_PSP_TEXTURE_WRAP_CACHE
        psp_gfx_pspgl_invalidate_fallback_texture_parameter_state(entry->texture);
#endif
        psp_gfx_pspgl_invalidate_texture_parameter_state(&entry->parameterState);
        glDeleteTextures(1, &entry->texture);
    }
    entry->indices = indices;
    entry->palette = palette;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_CI8, PSP_GFX_PSPGL_CI8_TEXTURE_CACHE_SIZE,
                                            sTextureCacheCount, keyHash, baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    psp_gfx_pspgl_init_texture_parameter_state(&entry->parameterState, entry->texture);
    *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
    psp_gfx_pspgl_note_bound_texture(entry->texture, *textureRef);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_GetCi8Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    u32 textureId;

    if (PspGfxPspgl_FindCi8Texture(indices, palette, width, height, &textureId, textureRef, uploadWidth,
                                   uploadHeight)) {
        return textureId;
    }
    return PspGfxPspgl_CreateCi8Texture(indices, palette, width, height, uploadWidth, uploadHeight, textureRef);
}

int PspGfxPspgl_FindCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* textureId,
                               PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_find_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, textureId,
                                                textureRef, uploadWidth, uploadHeight, 0, 0, 0, 1);
}

u32 PspGfxPspgl_CreateCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_create_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, 0, 0, 0,
                                                  uploadWidth, uploadHeight, textureRef);
}

u32 PspGfxPspgl_GetCi4Texture(const u8* indices, const u16* palette, u32 width, u32 height, u32* uploadWidth,
                              u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_get_converted_texture(indices, palette, width, height, PSP_GFX_TEXTURE_CI4, uploadWidth,
                                               uploadHeight, textureRef);
}

int PspGfxPspgl_FindRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* textureId,
                                  PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight) {
    PspGfxRgba16TextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (textureId == NULL) || (textureRef == NULL) ||
        (uploadWidth == NULL) || (uploadHeight == NULL)) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_rgba16_key_hash(pixels, width, height, premultiply);
    baseHash = psp_gfx_pspgl_rgba16_base_hash(pixels);
#endif
    for (i = 0; i < sRgba16TextureCacheCount; i++) {
        entry = &sRgba16TextureCache[i];
        if ((entry->pixels == pixels) && (entry->width == width) && (entry->height == height) &&
            (entry->premultiplied == premultiply)) {
            *textureId = entry->texture;
            *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_RGBA16,
                                                 PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE, sRgba16TextureCacheCount,
                                                 keyHash, baseHash, 1);
#endif
            PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_RGBA16, PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE,
                                         sRgba16TextureCacheCount, keyHash, baseHash, 0);
#endif
    return 0;
}

u32 PspGfxPspgl_CreateRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                    u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    PspGfxRgba16TextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;
    int softenAlpha;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) || (uploadHeight == NULL) ||
        (textureRef == NULL)) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_rgba16_key_hash(pixels, width, height, premultiply);
    baseHash = psp_gfx_pspgl_rgba16_base_hash(pixels);
#endif
    softenAlpha = premultiply && psp_gfx_pspgl_is_dark_rgba16_mask(pixels, width, height);

    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;

            u8* out = &sTextureUpload[dstIndex * 4];

            psp_gfx_pspgl_rgba16_to_rgba8(psp_gfx_pspgl_read_u16(pixels, srcIndex), out);
            if (softenAlpha) {
                out[0] = 0;
                out[1] = 0;
                out[2] = 0;
                out[3] = psp_gfx_pspgl_filtered_rgba16_alpha(pixels, width, height, srcX, srcY);
            }
            if (premultiply) {
                out[0] = (u8) (((u32) out[0] * out[3] + 127U) / 255U);
                out[1] = (u8) (((u32) out[1] * out[3] + 127U) / 255U);
                out[2] = (u8) (((u32) out[2] * out[3] + 127U) / 255U);
            }
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sRgba16TextureCacheCount < PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE) {
        entry = &sRgba16TextureCache[sRgba16TextureCacheCount++];
    } else {
        entry = &sRgba16TextureCache[sRgba16TextureCacheReplaceIndex++];
        sRgba16TextureCacheReplaceIndex %= PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_RGBA16,
            psp_gfx_pspgl_rgba16_key_hash(entry->pixels, entry->width, entry->height, entry->premultiplied));
#endif
        psp_gfx_pspgl_invalidate_bound_texture();
#if SF64_PSP_TEXTURE_WRAP_CACHE
        psp_gfx_pspgl_invalidate_fallback_texture_parameter_state(entry->texture);
#endif
        psp_gfx_pspgl_invalidate_texture_parameter_state(&entry->parameterState);
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->premultiplied = premultiply;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_RGBA16,
                                            PSP_GFX_PSPGL_RGBA16_TEXTURE_CACHE_SIZE, sRgba16TextureCacheCount, keyHash,
                                            baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    psp_gfx_pspgl_init_texture_parameter_state(&entry->parameterState, entry->texture);
    *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
    psp_gfx_pspgl_note_bound_texture(entry->texture, *textureRef);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_GetRgba16Texture(const u16* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    u32 textureId;

    if (PspGfxPspgl_FindRgba16Texture(pixels, width, height, premultiply, &textureId, textureRef, uploadWidth,
                                      uploadHeight)) {
        return textureId;
    }
    return PspGfxPspgl_CreateRgba16Texture(pixels, width, height, premultiply, uploadWidth, uploadHeight, textureRef);
}

static int psp_gfx_pspgl_find_rgba32_texture(const void* pixels, u32 width, u32 height, int premultiply,
                                             int envBlend, u32 primitiveColor, u32 environmentColor,
                                             u32* textureId, PspGfxPspglTextureRef* textureRef, u32* uploadWidth,
                                             u32* uploadHeight) {
    PspGfxRgba32TextureCacheEntry* entry;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 i;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (textureId == NULL) || (textureRef == NULL) ||
        (uploadWidth == NULL) || (uploadHeight == NULL)) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_rgba32_key_hash(pixels, width, height, premultiply, envBlend, primitiveColor,
                                            environmentColor);
    baseHash = psp_gfx_pspgl_rgba32_base_hash(pixels);
#endif
    for (i = 0; i < sRgba32TextureCacheCount; i++) {
        entry = &sRgba32TextureCache[i];
        if ((entry->pixels == pixels) && (entry->width == width) && (entry->height == height) &&
            (entry->premultiplied == premultiply) && (entry->envBlend == envBlend) &&
            (entry->primitiveColor == primitiveColor) && (entry->environmentColor == environmentColor)) {
            *textureId = entry->texture;
            *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
            *uploadWidth = entry->uploadWidth;
            *uploadHeight = entry->uploadHeight;
#if SF64_PSP_PROFILE_PHASES
            PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_RGBA32,
                                                 PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE, sRgba32TextureCacheCount,
                                                 keyHash, baseHash, 1);
#endif
            PspProfiler_CountTextureEvent(1, 0, 0, 0, 0);
            return 1;
        }
    }
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheLookup(PSP_PROFILE_TEXTURE_CACHE_RGBA32, PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE,
                                         sRgba32TextureCacheCount, keyHash, baseHash, 0);
#endif
    return 0;
}

int PspGfxPspgl_FindRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply, u32* textureId,
                                  PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_find_rgba32_texture(pixels, width, height, premultiply, 0, 0, 0, textureId, textureRef,
                                             uploadWidth, uploadHeight);
}

int PspGfxPspgl_FindRgba32EnvBlendTexture(const void* pixels, u32 width, u32 height, u32 primitiveColor,
                                          u32 environmentColor, u32* textureId,
                                          PspGfxPspglTextureRef* textureRef, u32* uploadWidth, u32* uploadHeight) {
    return psp_gfx_pspgl_find_rgba32_texture(pixels, width, height, 0, 1, primitiveColor, environmentColor,
                                             textureId, textureRef, uploadWidth, uploadHeight);
}

static u32 psp_gfx_pspgl_create_rgba32_texture(const void* pixels, u32 width, u32 height, int premultiply,
                                               int envBlend, u32 primitiveColor, u32 environmentColor,
                                               u32* uploadWidth, u32* uploadHeight,
                                               PspGfxPspglTextureRef* textureRef) {
    PspGfxRgba32TextureCacheEntry* entry;
    u32 finalWidth;
    u32 finalHeight;
    u32 finalPixelCount;
#if SF64_PSP_PROFILE_PHASES
    u64 keyHash;
    u64 baseHash;
#endif
    u32 x;
    u32 y;

    if ((pixels == NULL) || (width == 0) || (height == 0) || (uploadWidth == NULL) || (uploadHeight == NULL) ||
        (textureRef == NULL)) {
        return 0;
    }
    finalWidth = psp_gfx_pspgl_next_power_of_two(width);
    finalHeight = psp_gfx_pspgl_next_power_of_two(height);
    finalPixelCount = finalWidth * finalHeight;
    if (finalPixelCount > PSP_GFX_PSPGL_MAX_TEXTURE_PIXELS) {
        return 0;
    }
#if SF64_PSP_PROFILE_PHASES
    keyHash = psp_gfx_pspgl_rgba32_key_hash(pixels, width, height, premultiply, envBlend, primitiveColor,
                                            environmentColor);
    baseHash = psp_gfx_pspgl_rgba32_base_hash(pixels);
#endif
    PspProfiler_CountTextureEvent(0, 1, 0, 0, 0);
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_DECODE);
    for (y = 0; y < finalHeight; y++) {
        u32 srcY = (y < height) ? y : (height - 1);

        for (x = 0; x < finalWidth; x++) {
            u32 srcX = (x < width) ? x : (width - 1);
            u32 srcIndex = (srcY * width) + srcX;
            u32 dstIndex = (y * finalWidth) + x;
            u32 texel = psp_gfx_pspgl_read_n64_rgba32(pixels, srcIndex);
            /* Transfer applies to texture RGB only; the envBlend inputs below
             * (primitiveColor/environmentColor) arrive already transformed
             * from the frontend, so the baked result needs no second pass. */
            u8 r = psp_gfx_color_transfer_u8((u8) ((texel >> 24) & 0xFFU));
            u8 g = psp_gfx_color_transfer_u8((u8) ((texel >> 16) & 0xFFU));
            u8 b = psp_gfx_color_transfer_u8((u8) ((texel >> 8) & 0xFFU));
            u8 a = (u8) (texel & 0xFFU);
            u8* out = &sTextureUpload[dstIndex * 4];

            if (envBlend) {
                u8 pr = (u8) (primitiveColor & 0xFFU);
                u8 pg = (u8) ((primitiveColor >> 8) & 0xFFU);
                u8 pb = (u8) ((primitiveColor >> 16) & 0xFFU);
                u8 pa = (u8) ((primitiveColor >> 24) & 0xFFU);
                u8 er = (u8) (environmentColor & 0xFFU);
                u8 eg = (u8) ((environmentColor >> 8) & 0xFFU);
                u8 eb = (u8) ((environmentColor >> 16) & 0xFFU);

                r = (u8) (((u32) er * (255U - r) + ((u32) pr * r) + 127U) / 255U);
                g = (u8) (((u32) eg * (255U - g) + ((u32) pg * g) + 127U) / 255U);
                b = (u8) (((u32) eb * (255U - b) + ((u32) pb * b) + 127U) / 255U);
                a = (u8) ((((u32) a * pa) + 127U) / 255U);
            } else if (premultiply) {
                r = (u8) ((((u32) r * a) + 127U) / 255U);
                g = (u8) ((((u32) g * a) + 127U) / 255U);
                b = (u8) ((((u32) b * a) + 127U) / 255U);
            }
            out[0] = r;
            out[1] = g;
            out[2] = b;
            out[3] = a;
        }
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_DECODE);

    if (sRgba32TextureCacheCount < PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE) {
        entry = &sRgba32TextureCache[sRgba32TextureCacheCount++];
    } else {
        entry = &sRgba32TextureCache[sRgba32TextureCacheReplaceIndex++];
        sRgba32TextureCacheReplaceIndex %= PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE;
#if SF64_PSP_PROFILE_PHASES
        PspProfiler_RecordTextureCacheEviction(
            PSP_PROFILE_TEXTURE_CACHE_RGBA32,
            psp_gfx_pspgl_rgba32_key_hash(entry->pixels, entry->width, entry->height, entry->premultiplied,
                                          entry->envBlend, entry->primitiveColor, entry->environmentColor));
#endif
        psp_gfx_pspgl_invalidate_bound_texture();
#if SF64_PSP_TEXTURE_WRAP_CACHE
        psp_gfx_pspgl_invalidate_fallback_texture_parameter_state(entry->texture);
#endif
        psp_gfx_pspgl_invalidate_texture_parameter_state(&entry->parameterState);
        glDeleteTextures(1, &entry->texture);
    }
    entry->pixels = pixels;
    entry->width = width;
    entry->height = height;
    entry->uploadWidth = finalWidth;
    entry->uploadHeight = finalHeight;
    entry->premultiplied = premultiply;
    entry->envBlend = envBlend;
    entry->primitiveColor = primitiveColor;
    entry->environmentColor = environmentColor;
#if SF64_PSP_PROFILE_PHASES
    PspProfiler_RecordTextureCacheInsertion(PSP_PROFILE_TEXTURE_CACHE_RGBA32,
                                            PSP_GFX_PSPGL_RGBA32_TEXTURE_CACHE_SIZE, sRgba32TextureCacheCount,
                                            keyHash, baseHash);
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    glGenTextures(1, &entry->texture);
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    psp_gfx_pspgl_init_texture_parameter_state(&entry->parameterState, entry->texture);
    *textureRef = psp_gfx_pspgl_texture_ref(&entry->parameterState);
    psp_gfx_pspgl_note_bound_texture(entry->texture, *textureRef);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, finalWidth, finalHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sTextureUpload);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_TEXTURE_UPLOAD);
    PspProfiler_CountTextureEvent(0, 0, 1, 1, finalPixelCount * 4);
    *uploadWidth = finalWidth;
    *uploadHeight = finalHeight;
    return entry->texture;
}

u32 PspGfxPspgl_CreateRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                    u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_create_rgba32_texture(pixels, width, height, premultiply, 0, 0, 0, uploadWidth,
                                               uploadHeight, textureRef);
}

u32 PspGfxPspgl_CreateRgba32EnvBlendTexture(const void* pixels, u32 width, u32 height, u32 primitiveColor,
                                            u32 environmentColor, u32* uploadWidth, u32* uploadHeight,
                                            PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_create_rgba32_texture(pixels, width, height, 0, 1, primitiveColor, environmentColor,
                                               uploadWidth, uploadHeight, textureRef);
}

u32 PspGfxPspgl_GetRgba32Texture(const void* pixels, u32 width, u32 height, int premultiply, u32* uploadWidth,
                                 u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    u32 textureId;

    if (PspGfxPspgl_FindRgba32Texture(pixels, width, height, premultiply, &textureId, textureRef, uploadWidth,
                                      uploadHeight)) {
        return textureId;
    }
    return PspGfxPspgl_CreateRgba32Texture(pixels, width, height, premultiply, uploadWidth, uploadHeight, textureRef);
}

int PspGfxPspgl_FindIa8Texture(const u8* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                               u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_find_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, textureId,
                                                textureRef, uploadWidth, uploadHeight, 0, 0, 0, 1);
}

u32 PspGfxPspgl_CreateIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                                 PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_create_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, 0, 0, 0,
                                                  uploadWidth, uploadHeight, textureRef);
}

u32 PspGfxPspgl_GetIa8Texture(const u8* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                              PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_get_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, uploadWidth,
                                               uploadHeight, textureRef);
}

int PspGfxPspgl_FindIa8EnvBlendTexture(const u8* pixels, u32 width, u32 height, u32 primitiveColor,
                                       u32 environmentColor, u32* textureId, u32* uploadWidth, u32* uploadHeight,
                                       PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_find_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, textureId,
                                                textureRef, uploadWidth, uploadHeight, 1, primitiveColor,
                                                environmentColor, 1);
}

u32 PspGfxPspgl_CreateIa8EnvBlendTexture(const u8* pixels, u32 width, u32 height, u32 primitiveColor,
                                         u32 environmentColor, u32* uploadWidth, u32* uploadHeight,
                                         PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_create_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA8, 1,
                                                  primitiveColor, environmentColor, uploadWidth, uploadHeight,
                                                  textureRef);
}

int PspGfxPspgl_FindIa16Texture(const u16* pixels, u32 width, u32 height, u32* textureId, u32* uploadWidth,
                                u32* uploadHeight, PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_find_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, textureId,
                                                textureRef, uploadWidth, uploadHeight, 0, 0, 0, 1);
}

u32 PspGfxPspgl_CreateIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                                  PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_create_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, 0, 0, 0,
                                                  uploadWidth, uploadHeight, textureRef);
}

u32 PspGfxPspgl_GetIa16Texture(const u16* pixels, u32 width, u32 height, u32* uploadWidth, u32* uploadHeight,
                               PspGfxPspglTextureRef* textureRef) {
    return psp_gfx_pspgl_get_converted_texture(pixels, NULL, width, height, PSP_GFX_TEXTURE_IA16, uploadWidth,
                                               uploadHeight, textureRef);
}

static u32 psp_gfx_pspgl_is_small_draw(u32 vertexCount) {
    return vertexCount <= PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_PAGE_VERTICES;
}

static u32 psp_gfx_pspgl_is_large_draw(u32 vertexCount) {
    return !psp_gfx_pspgl_is_small_draw(vertexCount) &&
           (vertexCount <= PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_VERTICES);
}

static void psp_gfx_pspgl_begin_vertex_stream_upload_phase(u32 smallDraw, u32 largeDraw) {
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
    if (smallDraw) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_SMALL);
    } else if (largeDraw) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_LARGE);
    }
}

static void psp_gfx_pspgl_end_vertex_stream_upload_phase(u32 smallDraw, u32 largeDraw) {
    if (smallDraw) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_SMALL);
    } else if (largeDraw) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD_LARGE);
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_VERTEX_STREAM_UPLOAD);
}

static void psp_gfx_pspgl_begin_submit_phase(u32 smallDraw, u32 largeDraw) {
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
    if (smallDraw) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT_SMALL);
    } else if (largeDraw) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_SUBMIT_LARGE);
    }
}

static void psp_gfx_pspgl_end_submit_phase(u32 smallDraw, u32 largeDraw) {
    if (smallDraw) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT_SMALL);
    } else if (largeDraw) {
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT_LARGE);
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_SUBMIT);
}

static void psp_gfx_pspgl_draw_client_arrays(const PspGfxPspglColorVertex* vertices, u32 vertexCount) {
    u32 smallDraw = psp_gfx_pspgl_is_small_draw(vertexCount);
    u32 largeDraw = psp_gfx_pspgl_is_large_draw(vertexCount);

#if SF64_PSP_PSPGL_VBO_STREAM
    psp_gfx_pspgl_unmap_small_arena();
#endif
    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    psp_gfx_pspgl_bind_client_arrays(vertices);
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    psp_gfx_pspgl_begin_submit_phase(smallDraw, largeDraw);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    psp_gfx_pspgl_end_submit_phase(smallDraw, largeDraw);
    PspProfiler_CountDrawCall(vertexCount);
    PspProfiler_CountPspglSubmitSplit(smallDraw, largeDraw, vertexCount);
    PspProfiler_CountVertexStream(
        0, 0, 0, 0, 1, vertexCount, 0, PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES,
        (sVertexStreamSmallArenaVertexIndex * sizeof(PspGfxPspglColorVertex)) +
            (sVertexStreamLargePageIndex * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES),
        0, 0, 0, 0);
}

static int psp_gfx_pspgl_draw_vbo_stream(const PspGfxPspglColorVertex* vertices, u32 vertexCount) {
#if SF64_PSP_PSPGL_VBO_STREAM
    PspGfxVertexStreamPage* page;
    void* mapped;
    u32 pageIndex;
    u32 firstVertex;
    u32 bytes;
    u32 highWater;
    u32 pageSwitch;
    u32 smallDraw;
    u32 largeDraw;

    if (!sVertexStreamAvailable) {
        return 0;
    }

    smallDraw = psp_gfx_pspgl_is_small_draw(vertexCount);
    largeDraw = psp_gfx_pspgl_is_large_draw(vertexCount);
    bytes = vertexCount * sizeof(PspGfxPspglColorVertex);
    if (smallDraw) {
        if (sVertexStreamSmallArenaExhausted ||
            (vertexCount > (PSP_GFX_PSPGL_VERTEX_STREAM_SMALL_ARENA_VERTICES - sVertexStreamSmallArenaVertexIndex))) {
            sVertexStreamSmallArenaExhausted = 1;
            return 0;
        }
        firstVertex = sVertexStreamSmallArenaVertexIndex;
        page = &sVertexStreamSmallArenas[sVertexStreamSetIndex];
        pageSwitch = (sVertexStreamSmallArenaVertexIndex != 0) || (sVertexStreamLargePageIndex != 0);
        highWater = ((sVertexStreamSmallArenaVertexIndex + vertexCount) * sizeof(PspGfxPspglColorVertex)) +
                    (sVertexStreamLargePageIndex * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES);
    } else if (largeDraw) {
        psp_gfx_pspgl_unmap_small_arena();
        if (sVertexStreamLargePageIndex >= PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET) {
            return 0;
        }
        pageIndex = (sVertexStreamSetIndex * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGES_PER_SET) +
                    sVertexStreamLargePageIndex;
        page = &sVertexStreamLargePages[pageIndex];
        firstVertex = 0;
        pageSwitch = (sVertexStreamSmallArenaVertexIndex != 0) || (sVertexStreamLargePageIndex != 0);
        highWater = (sVertexStreamSmallArenaVertexIndex * sizeof(PspGfxPspglColorVertex)) +
                    ((sVertexStreamLargePageIndex + 1) * PSP_GFX_PSPGL_VERTEX_STREAM_LARGE_PAGE_BYTES);
    } else {
        return 0;
    }

    (void) pageSwitch;
    (void) highWater;

    if (smallDraw && (sVertexStreamSmallArenaMapped == NULL)) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        psp_gfx_pspgl_bind_vbo_arrays(page->buffer);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    } else if (largeDraw) {
        PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
        glBindBuffer(GL_ARRAY_BUFFER, page->buffer);
        PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    }

    psp_gfx_pspgl_begin_vertex_stream_upload_phase(smallDraw, largeDraw);
    if (smallDraw) {
        if (sVertexStreamSmallArenaMapped == NULL) {
            mapped = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
            if (mapped == NULL) {
                psp_gfx_pspgl_end_vertex_stream_upload_phase(smallDraw, largeDraw);
                PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
                sVertexStreamSmallArenaExhausted = 1;
                return 0;
            }
            sVertexStreamSmallArenaMapped = mapped;
            sVertexStreamSmallArenaMappedSet = sVertexStreamSetIndex;
        }
        memcpy((u8*) sVertexStreamSmallArenaMapped + (firstVertex * sizeof(PspGfxPspglColorVertex)), vertices,
               bytes);
    } else {
        mapped = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
        if (mapped == NULL) {
            psp_gfx_pspgl_end_vertex_stream_upload_phase(smallDraw, largeDraw);
            PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
            return 0;
        }
        memcpy(mapped, vertices, bytes);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    psp_gfx_pspgl_end_vertex_stream_upload_phase(smallDraw, largeDraw);
    PspProfiler_CountPspglVertexStreamUploadSplit(smallDraw, largeDraw, bytes);

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    if (smallDraw) {
        sVertexStreamSmallArenaVertexIndex += vertexCount;
    } else {
        psp_gfx_pspgl_bind_vbo_arrays(page->buffer);
        sVertexStreamLargePageIndex++;
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    PspProfiler_CountVertexStream(0, 0, 1, bytes, 0, 0, pageSwitch, PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES,
                                  highWater, 0, 0, 0, 0);

    psp_gfx_pspgl_begin_submit_phase(smallDraw, largeDraw);
    glDrawArrays(GL_TRIANGLES, firstVertex, vertexCount);
    psp_gfx_pspgl_end_submit_phase(smallDraw, largeDraw);
    PspProfiler_CountDrawCall(vertexCount);
    PspProfiler_CountPspglSubmitSplit(smallDraw, largeDraw, vertexCount);
    PspProfiler_CountVertexStream(1, vertexCount, 0, 0, 0, 0, 0, PSP_GFX_PSPGL_VERTEX_STREAM_SET_BYTES,
                                  highWater, smallDraw, largeDraw, smallDraw ? vertexCount : 0,
                                  largeDraw ? vertexCount : 0);
    return 1;
#else
    (void) vertices;
    (void) vertexCount;
    return 0;
#endif
}

void PspGfxPspgl_DrawColoredTriangles(const PspGfxPspglColorVertex* vertices, u32 vertexCount,
    u32 textureId, PspGfxPspglTextureRef textureRef, PspGfxPspglTextureEnv textureEnv,
    u32 textureEnvColor, PspGfxPspglTextureWrap wrapS, PspGfxPspglTextureWrap wrapT, int alphaTest,
    int blend, int premultiplied, int depthTest, int depthWrite, int fog, const float* fogColor, float fogStart, float fogEnd,
    const float* projectionMatrix, int pretransformed, int pointFilter
) {
    GLint glTextureEnv;
    GLint glWrapS;
    GLint glWrapT;

    if ((vertices == NULL) || (vertexCount == 0)) {
        return;
    }

#if SF64_PSP_PROFILE_VERTEX_REUSE
    PspProfiler_AnalyzeAllPspglDrawVertexReuse(vertices, vertexCount);
#endif

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    if (pretransformed || (projectionMatrix == NULL)) {
        psp_gfx_pspgl_load_projection_identity();
    } else {
        psp_gfx_pspgl_load_projection_matrix(projectionMatrix);
    }

    psp_gfx_pspgl_load_modelview_identity();

    psp_gfx_pspgl_enable_client_arrays();

    if (depthTest) {
        psp_gfx_pspgl_depth_test(1);
        psp_gfx_pspgl_depth_func(GL_LEQUAL);
    } else {
        psp_gfx_pspgl_depth_test(0);
    }

    psp_gfx_pspgl_depth_mask(depthWrite ? GL_TRUE : GL_FALSE);

    if (fog &&
        !pretransformed &&
        (fogColor != NULL) &&
        (fogEnd > fogStart)) {

        psp_gfx_pspgl_fog_linear(fogColor, fogStart * 0.45f, fogEnd * 0.5f);
    } else {
        psp_gfx_pspgl_fog_disabled();
    }

    // this controls GL_TEXTURE_2D, but does not disable GL_TEXTURE_COORD_ARRAY for untextured geometry
    if (textureId != 0) {
#if PSP_RENDERER_DIAGNOSTICS
        /*
         * textureId is nonzero (the frontend resolved a texture) but the
         * retained textureRef/GL handle it carried along doesn't match the
         * cache slot's current generation. This distinguishes a frontend
         * preparation failure (textureId == 0) from a stale/invalidated
         * cache reference surviving past an eviction.
         */
        if (!psp_gfx_pspgl_texture_ref_valid(textureId, &textureRef) && (sInvalidTextureRefDiagCount < 32)) {
            char line[224];

            snprintf(line, sizeof(line),
                     "[pspgl-texref-invalid] diag=%lu texId=%lu refTexture=%lu refGeneration=%lu refState=%p "
                     "stateValid=%d stateTexture=%lu stateGeneration=%lu",
                     (unsigned long) sInvalidTextureRefDiagCount, (unsigned long) textureId,
                     (unsigned long) textureRef.texture, (unsigned long) textureRef.generation,
                     (void*) textureRef.state, (textureRef.state != NULL) ? textureRef.state->valid : -1,
                     (textureRef.state != NULL) ? (unsigned long) textureRef.state->texture : 0UL,
                     (textureRef.state != NULL) ? (unsigned long) textureRef.state->generation : 0UL);
            PspPlatform_LogLine(line);
            sInvalidTextureRefDiagCount++;
        }
#endif
        psp_gfx_pspgl_texture_2d(1);

        if (alphaTest) {
            psp_gfx_pspgl_alpha_test(1);
            psp_gfx_pspgl_alpha_func(GL_GREATER, (alphaTest > 1) ? 0.5f : 0.0f);
        } else {
            psp_gfx_pspgl_alpha_test(0);
        }

        if (blend) {
            psp_gfx_pspgl_blend(1);
            psp_gfx_pspgl_blend_func(
                premultiplied ? GL_ONE : GL_SRC_ALPHA,
                GL_ONE_MINUS_SRC_ALPHA
            );
        } else {
            psp_gfx_pspgl_blend(0);
        }

        psp_gfx_pspgl_bind_texture(textureId, textureRef);

        glWrapS =
            (wrapS == PSP_GFX_PSPGL_WRAP_CLAMP)
                ? GL_CLAMP_TO_EDGE
                : GL_REPEAT;

        glWrapT =
            (wrapT == PSP_GFX_PSPGL_WRAP_CLAMP)
                ? GL_CLAMP_TO_EDGE
                : GL_REPEAT;

        psp_gfx_pspgl_set_texture_wrap(textureId, glWrapS, glWrapT);
        psp_gfx_pspgl_set_texture_filter(textureId, pointFilter ? GL_NEAREST : GL_LINEAR,
                                         pointFilter ? GL_NEAREST : GL_LINEAR);

        if (textureEnv == PSP_GFX_PSPGL_TEX_BLEND) {
            glTextureEnv = GL_BLEND;
        } else if (textureEnv == PSP_GFX_PSPGL_TEX_MODULATE) {
            glTextureEnv = GL_MODULATE;
        } else {
            glTextureEnv = GL_REPLACE;
        }

        psp_gfx_pspgl_texture_env_mode(glTextureEnv);
        if (textureEnv == PSP_GFX_PSPGL_TEX_BLEND) {
            psp_gfx_pspgl_texture_env_color(textureEnvColor);
        }
    } else {
        psp_gfx_pspgl_texture_2d(0);
        psp_gfx_pspgl_alpha_test(0);
        psp_gfx_pspgl_blend(0);
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    if (!psp_gfx_pspgl_draw_vbo_stream(vertices, vertexCount)) {
        psp_gfx_pspgl_draw_client_arrays(vertices, vertexCount);
    }
}

void PspGfxPspgl_DrawSolidRect(float ulx, float uly, float lrx, float lry, u32 color, int blend) {
    PspGfxPspglColorVertex vertices[6];

    vertices[0].u = 0.0f;
    vertices[0].v = 0.0f;
    vertices[0].color = color;
    vertices[0].x = (ulx / 160.0f) - 1.0f;
    vertices[0].y = 1.0f - (uly / 120.0f);
    vertices[0].z = 0.0f;

    vertices[1].u = 0.0f;
    vertices[1].v = 0.0f;
    vertices[1].color = color;
    vertices[1].x = (lrx / 160.0f) - 1.0f;
    vertices[1].y = 1.0f - (uly / 120.0f);
    vertices[1].z = 0.0f;

    vertices[2].u = 0.0f;
    vertices[2].v = 0.0f;
    vertices[2].color = color;
    vertices[2].x = (lrx / 160.0f) - 1.0f;
    vertices[2].y = 1.0f - (lry / 120.0f);
    vertices[2].z = 0.0f;

    vertices[3] = vertices[0];
    vertices[4] = vertices[2];

    vertices[5].u = 0.0f;
    vertices[5].v = 0.0f;
    vertices[5].color = color;
    vertices[5].x = (ulx / 160.0f) - 1.0f;
    vertices[5].y = 1.0f - (lry / 120.0f);
    vertices[5].z = 0.0f;

    PspProfiler_PhaseBegin(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);
    psp_gfx_pspgl_load_projection_identity();
    psp_gfx_pspgl_load_modelview_identity();
    psp_gfx_pspgl_enable_client_arrays();
    psp_gfx_pspgl_texture_2d(0);
    psp_gfx_pspgl_alpha_test(0);
    psp_gfx_pspgl_depth_test(0);
    psp_gfx_pspgl_depth_mask(GL_FALSE);
    psp_gfx_pspgl_fog_disabled();
    if (blend) {
        psp_gfx_pspgl_blend(1);
        psp_gfx_pspgl_blend_func(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        psp_gfx_pspgl_blend(0);
    }
    PspProfiler_PhaseEnd(PSP_PROFILE_PHASE_PSPGL_STATE_SETUP);

    if (!psp_gfx_pspgl_draw_vbo_stream(vertices, ARRAY_COUNT(vertices))) {
        psp_gfx_pspgl_draw_client_arrays(vertices, ARRAY_COUNT(vertices));
    }
}
