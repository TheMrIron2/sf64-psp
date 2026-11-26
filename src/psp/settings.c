#include "src/psp/settings.h"
#include "src/psp/aquas_particles.h"
#include "src/psp/display.h"
#include "src/psp/frame_scheduler.h"
#include "src/psp/frame_interpolation.h"
#include "src/psp/settings_path.h"

#include <stdio.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_fcntl.h>

#define PSP_SETTINGS_PATH_SIZE 256

static char sSettingsPath[PSP_SETTINGS_PATH_SIZE] = "sf64-settings.cfg";
static int sSettingsPathValid = 1;
static PspGfxBackendKind sGraphicsBackend = PSP_GFX_KIND_GU;
static PspGfxBackendKind sActiveGraphicsBackend = PSP_GFX_KIND_GU;
static int sBackendSaveFailed;
static int sRecoveredBackup;
static PspGraphicsQuality sGraphicsQuality;
static PspGraphicsQuality sActiveGraphicsQuality;
static int sQualityPending;
static PspGeLightingMode sGeLighting;
static int sGeSideClip;
static int sGePersistentVertices;
static int sVfpuMatrixLerp;
static char sExtraSettings[384];

static int sDisplayMode = -1;
static int sPresentationHz;
#if PSP_FPS_OVERLAY
static int sPerformanceVisible = 1;
static int sTogglePerformance;
#endif

static int psp_settings_equal(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int psp_settings_prefix(const char* text, const char* prefix) {
    while (*prefix) if (*text++ != *prefix++) return 0;
    return 1;
}

static int psp_settings_assignment(const char* text) {
    while (*text) if (*text++ == '=') return 1;
    return 0;
}

static void psp_settings_copy(char* output, const char* input, unsigned bytes) {
    unsigned i;
    for (i = 0; i < bytes; i++) output[i] = input[i];
}

static void psp_settings_set_path(const char* executablePath) {
    static const char filename[] = "sf64-settings.cfg";

    sSettingsPathValid = 1;
    if (executablePath == NULL) {
        static const char fallback[] = "sf64-settings.cfg";
        unsigned i;
        for (i = 0; i < sizeof(fallback); i++) sSettingsPath[i] = fallback[i];
        return;
    }
    sSettingsPathValid = PspSettingsPath_FromExecutable(sSettingsPath, sizeof(sSettingsPath), executablePath, filename);
}

static int psp_settings_suffix(char* output, const char* suffix) {
    return sSettingsPathValid &&
           PspSettingsPath_AppendSuffix(output, PSP_SETTINGS_PATH_SIZE, sSettingsPath, suffix);
}

static int psp_settings_read(const char* path) {
    char data[512], extras[sizeof(sExtraSettings)];
    PspGfxBackendKind backend = sGraphicsBackend;
    PspGraphicsQuality quality = PSP_GRAPHICS_ACCURATE;
    PspGeLightingMode geLighting = PSP_GE_LIGHTING_OFF;
    int geSideClip = 0;
    int gePersistentVertices = 0;
    int vfpuMatrixLerp = 0;
    unsigned version = 0, seenBackend = 0, seenQuality = 0, extraBytes = 0, i, begin;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int bytes;

    if (fd < 0) return 0;
    bytes = sceIoRead(fd, data, sizeof(data) - 1);
    sceIoClose(fd);
    if (bytes <= 0 || bytes >= (int) sizeof(data) - 1) return 0;
    data[bytes] = 0;
    for (begin = 0; begin < (unsigned) bytes; begin = i + 1) {
        for (i = begin; i < (unsigned) bytes && data[i] != '\n'; i++) {
            if (data[i] == 0 || data[i] == '\r') return 0;
        }
        if (i == (unsigned) bytes) return 0;
        data[i] = 0;
        if (psp_settings_prefix(data + begin, "version=")) {
            if (version || (!psp_settings_equal(data + begin + 8, "1") &&
                            !psp_settings_equal(data + begin + 8, "2"))) return 0;
            version = data[begin + 8] - '0';
        } else if (psp_settings_prefix(data + begin, "graphics_backend=")) {
            if (seenBackend++) return 0;
            if (psp_settings_equal(data + begin + 17, "gu")) backend = PSP_GFX_KIND_GU;
            else if (psp_settings_equal(data + begin + 17, "pspgl")) backend = PSP_GFX_KIND_PSPGL;
            else return 0;
        } else if (psp_settings_prefix(data + begin, "graphics_quality=")) {
            if (!seenQuality++ && psp_settings_equal(data + begin + 17, "fast")) quality = PSP_GRAPHICS_FAST;
            else quality = PSP_GRAPHICS_ACCURATE;
        } else if (i != begin) {
            unsigned length = i - begin;
            if ( !psp_settings_assignment(data + begin) || extraBytes + length + 1 >= sizeof(extras)) return 0;
            if (psp_settings_prefix(data + begin, "ge_lighting=")) {
                const char* value = data + begin + sizeof("ge_lighting=") - 1;
                if (psp_settings_equal(value, "diffuse")) geLighting = PSP_GE_LIGHTING_DIFFUSE;
                else if (psp_settings_equal(value, "powered")) geLighting = PSP_GE_LIGHTING_POWERED;
                else geLighting = PSP_GE_LIGHTING_OFF;
            } else if (psp_settings_prefix(data + begin, "ge_side_clip=")) {
                geSideClip = psp_settings_equal(data + begin + sizeof("ge_side_clip=") - 1, "on");
            } else if (psp_settings_prefix(data + begin, "ge_persistent_vertices=")) {
                gePersistentVertices = psp_settings_equal(data + begin + sizeof("ge_persistent_vertices=") - 1, "on");
            } else if (psp_settings_prefix(data + begin, "matrix_lerp=")) {
                vfpuMatrixLerp = psp_settings_equal(data + begin + sizeof("matrix_lerp=") - 1, "vfpu");
            }
            psp_settings_copy(extras + extraBytes, data + begin, length);
            extraBytes += length;
            extras[extraBytes++] = '\n';
        }
    }
    if (!version || !seenBackend) return 0;
    extras[extraBytes] = 0;
    sGraphicsBackend = backend;
    sGraphicsQuality = quality;
    sGeLighting = geLighting;
    sGeSideClip = geSideClip;
    sGePersistentVertices = gePersistentVertices;
    sVfpuMatrixLerp = vfpuMatrixLerp;
    psp_settings_copy(sExtraSettings, extras, extraBytes + 1);
    return 1;
}

static int psp_settings_save_backend(PspGfxBackendKind backend) {
    char temporary[PSP_SETTINGS_PATH_SIZE];
    char backup[PSP_SETTINGS_PATH_SIZE];
    char data[512];
    unsigned length = 0;
    int hadPrevious = 0;
    int writeResult;
    SceUID fd;

    if (!psp_settings_suffix(temporary, ".tmp") || !psp_settings_suffix(backup, ".bak")) return 0;

    snprintf(data, sizeof(data), "version=2\ngraphics_backend=%s\ngraphics_quality=%s\n%s",
             backend == PSP_GFX_KIND_PSPGL ? "pspgl" : "gu",
             sGraphicsQuality == PSP_GRAPHICS_FAST ? "fast" : "accurate", sExtraSettings);
    sceIoRemove(temporary);
    fd = sceIoOpen(temporary, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0666);
    if (fd < 0) return 0;
    while (data[length]) length++;
    writeResult = sceIoWrite(fd, data, length);
    if (sceIoClose(fd) < 0 || writeResult != (int) length) {
        sceIoRemove(temporary);
        return 0;
    }

    fd = sceIoOpen(sSettingsPath, PSP_O_RDONLY, 0);
    if (fd >= 0 && sRecoveredBackup) {
        sceIoClose(fd);
        if (sceIoRemove(sSettingsPath) < 0) {
            sceIoRemove(temporary);
            return 0;
        }
    } else if (fd >= 0) {
        sceIoClose(fd);
        sceIoRemove(backup);
        if (sceIoRename(sSettingsPath, backup) < 0) {
            sceIoRemove(temporary);
            return 0;
        }
        hadPrevious = 1;
    }
    if (sceIoRename(temporary, sSettingsPath) < 0) {
        if (hadPrevious) sceIoRename(backup, sSettingsPath);
        sceIoRemove(temporary);
        return 0;
    }
    if (hadPrevious || sRecoveredBackup) sceIoRemove(backup);
    sRecoveredBackup = 0;
    return 1;
}

void PspSettings_Init(const char* executablePath, PspGfxBackendKind buildDefault) {
    char backup[PSP_SETTINGS_PATH_SIZE];
    char temporary[PSP_SETTINGS_PATH_SIZE];

    psp_settings_set_path(executablePath);
    sGraphicsBackend = buildDefault;
    sActiveGraphicsBackend = buildDefault;
    sBackendSaveFailed = 0;
    sRecoveredBackup = 0;
    sGraphicsQuality = PSP_GRAPHICS_ACCURATE;
    sGeLighting = PSP_GE_LIGHTING_OFF;
    sGeSideClip = 0;
    sGePersistentVertices = 0;
    sVfpuMatrixLerp = 0;
    sQualityPending = 0;
    sExtraSettings[0] = 0;
    if (!psp_settings_suffix(backup, ".bak") || !psp_settings_suffix(temporary, ".tmp")) {
        sSettingsPathValid = 0;
    } else if (!psp_settings_read(sSettingsPath)) {
        if (psp_settings_read(backup)) sRecoveredBackup = 1;
        else psp_settings_read(temporary);
    }
    sActiveGraphicsQuality = sGraphicsQuality;
    PspFrameInterpolation_SetVfpuMatrixLerp(sVfpuMatrixLerp);
}

int PspSettings_UseFastGraphics(void) {
    return sActiveGraphicsBackend == PSP_GFX_KIND_GU &&
           sActiveGraphicsQuality == PSP_GRAPHICS_FAST;
}

PspGeLightingMode PspSettings_GetGeLighting(void) {
    return PspSettings_UseFastGraphics() ? sGeLighting : PSP_GE_LIGHTING_OFF;
}

int PspSettings_UseGeSideClip(void) {
    return sGeSideClip && PspSettings_UseFastGraphics();
}

int PspSettings_PrepareGePersistentVertices(void) {
    return sGePersistentVertices && PspSettings_GetGeLighting() != PSP_GE_LIGHTING_OFF;
}

PspGraphicsQuality PspSettings_GetGraphicsQuality(void) {
    return sGraphicsQuality;
}

PspGraphicsQuality PspSettings_GetActiveGraphicsQuality(void) {
    return sActiveGraphicsQuality;
}

void PspSettings_SetGraphicsQuality(PspGraphicsQuality quality) {
    if (quality != PSP_GRAPHICS_ACCURATE && quality != PSP_GRAPHICS_FAST) return;
    if (quality == sGraphicsQuality && !sBackendSaveFailed) return;
    sGraphicsQuality = quality;
    sQualityPending = 1;
}

PspGfxBackendKind PspSettings_GetGraphicsBackend(void) {
    return sGraphicsBackend;
}

int PspSettings_SetGraphicsBackend(PspGfxBackendKind backend) {
    if (backend != PSP_GFX_KIND_GU && backend != PSP_GFX_KIND_PSPGL) return 0;
    if (backend == sGraphicsBackend && !sBackendSaveFailed) return 1;
    sGraphicsBackend = backend;
    sBackendSaveFailed = !psp_settings_save_backend(backend);
    return !sBackendSaveFailed;
}

int PspSettings_GraphicsBackendSaveFailed(void) {
    return sBackendSaveFailed;
}

void PspSettings_SetActiveGraphicsBackend(PspGfxBackendKind backend) {
    if (backend == PSP_GFX_KIND_GU || backend == PSP_GFX_KIND_PSPGL) sActiveGraphicsBackend = backend;
}

PspGfxBackendKind PspSettings_GetActiveGraphicsBackend(void) {
    return sActiveGraphicsBackend;
}

void PspSettings_SetDisplayMode(n64psp_display_mode mode) {
    if (mode >= 0 && mode < N64PSP_DISPLAY_PSP_MODE_COUNT) {
        sDisplayMode = mode;
    }
}

void PspSettings_SetPresentationHz(int hz) {
    if (hz == 30 || hz == 60) {
        sPresentationHz = hz;
    }
}

int PspSettings_NeedsBoundary(void) {
    return sDisplayMode >= 0 || sPresentationHz != 0 || sQualityPending
#if PSP_FPS_OVERLAY
           || sTogglePerformance
#endif
           ;
}

// Called before simulation generation with the previous graphics task complete
void PspSettings_Apply(void) {
    if (sQualityPending) {
        if (sActiveGraphicsQuality != sGraphicsQuality) {
            sActiveGraphicsQuality = sGraphicsQuality;
            PspFrameInterpolation_Reset();
            PspGfxBackend_ReplayCacheInvalidate();
        }
        sBackendSaveFailed = !psp_settings_save_backend(sGraphicsBackend);
        sQualityPending = 0;
    }
#if PSP_FPS_OVERLAY
    if (sTogglePerformance) {
        sPerformanceVisible = !sPerformanceVisible;
        sTogglePerformance = 0;
    }
#endif
    if (sDisplayMode >= 0) {
        if (PspDisplay_SetMode(sDisplayMode)) {
            PspFrameInterpolation_Reset();
        }
        sDisplayMode = -1;
    }
    if (sPresentationHz != 0) {
        PspFrameScheduler_SetPresentationHz(sPresentationHz);
        sPresentationHz = 0;
    }
}

int PspSettings_PerformanceVisible(void) {
#if PSP_FPS_OVERLAY
    return sPerformanceVisible;
#else
    return 0;
#endif
}

void PspSettings_TogglePerformance(void) {
#if PSP_FPS_OVERLAY
    sTogglePerformance = !sTogglePerformance;
#endif
}
