#include <pspctrl.h>
#include <pspkernel.h>
#include <psputility_sysparam.h>
#include <pspwlan.h>
#include <stdio.h>

#include "src/psp/menu.h"
#include "src/psp/gu_census.h"
#include "src/psp/overlay.h"
#include "src/psp/settings.h"
#include "src/psp/audio_mode.h"
#include "src/psp/display.h"
#include "src/psp/frame_scheduler.h"
#include "src/psp/profiler.h"
#include "src/psp/hw_counter_profile.h"
#include "src/psp/gfx/gfx_psp_dl.h"

#define MENU_ROWS 12
#define MENU_TEXT 60
#define MENU_VISIBLE_ROWS 5

enum { MENU_MAIN, MENU_SETTINGS, MENU_GRAPHICS, MENU_DIAGNOSTICS,
       MENU_MULTIPLAYER, MENU_MULTIPLAYER_HOST, MENU_MULTIPLAYER_JOIN };
enum { ACTION_NONE, ACTION_START, ACTION_STOP, ACTION_TYPE, ACTION_SCENE_PREV, ACTION_SCENE_NEXT,
       ACTION_TRACE, ACTION_COVERAGE };

typedef struct {
    int open;
    int selected;
    int count;
    int cursor;
    int main;
    u32 accent;
    char title[MENU_TEXT];
    char subtitle[MENU_TEXT];
    char rows[MENU_ROWS][MENU_TEXT];
    char status[MENU_TEXT];
} PspMenuView;

static int sOpen;
static int sScreen;
static int sSelected;
static int sColour;
static int sMultiplayerMatchType;
static int sMultiplayerChannel;
static int sMultiplayerActive;
static PspMultiplayerMenuResult sMultiplayerResult;
static const u32 sAccents[] = { 0xCD4E5C26, 0xCD3838A0, 0xCDA06030 };
#if PSP_MENU_DIAGNOSTICS
static int sAction;
#endif
#if PSP_MENU_CAPTURES
// Phase and gprof captures take precedence when several capture types are built
static int sHardware = PROFILE_HW_COUNTERS && !(PROFILE_GPROF || PROFILE_PHASES);
#endif
static PspMenuView sView;

static const char* menu_backend_name(PspGfxBackendKind backend) {
    return backend == PSP_GFX_KIND_GU ? "GU" : "PSPGL";
}

void PspMenu_OpenMultiplayer(int matchType) {
    sMultiplayerMatchType = matchType;
    sMultiplayerActive = 1;
    sMultiplayerResult = PSP_MULTIPLAYER_PENDING;
    sOpen = 1;
    sScreen = MENU_MULTIPLAYER;
    sSelected = 0;
    sMultiplayerChannel = -1;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_ADHOC_CHANNEL, &sMultiplayerChannel);
}

PspMultiplayerMenuResult PspMenu_GetMultiplayerResult(void) {
    return sMultiplayerResult;
}

static void menu_multiplayer_finish(PspMultiplayerMenuResult result) {
    sMultiplayerResult = result;
    sMultiplayerActive = 0;
    sOpen = 0;
    sScreen = MENU_MAIN;
    sSelected = 0;
}

static void menu_multiplayer_update(u32 pressed) {
    int detail = sScreen != MENU_MULTIPLAYER;
    int rows = detail ? 2 : 4;

    if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_SELECT)) {
        if (detail) {
            sSelected = sScreen == MENU_MULTIPLAYER_HOST ? 0 : 1;
            sScreen = MENU_MULTIPLAYER;
        } else {
            menu_multiplayer_finish(PSP_MULTIPLAYER_BACK);
        }
        return;
    }
    if (pressed & PSP_CTRL_UP) sSelected = (sSelected + rows - 1) % rows;
    else if (pressed & PSP_CTRL_DOWN) sSelected = (sSelected + 1) % rows;
    if (!(pressed & PSP_CTRL_CROSS)) return;

    if (detail) {
        if (sSelected == 0) menu_multiplayer_finish(PSP_MULTIPLAYER_SOLO);
        else {
            sSelected = sScreen == MENU_MULTIPLAYER_HOST ? 0 : 1;
            sScreen = MENU_MULTIPLAYER;
        }
    } else if (sSelected < 2) {
        sScreen = sSelected == 0 ? MENU_MULTIPLAYER_HOST : MENU_MULTIPLAYER_JOIN;
        sSelected = 0;
    } else {
        menu_multiplayer_finish(sSelected == 2 ? PSP_MULTIPLAYER_SOLO : PSP_MULTIPLAYER_BACK);
    }
}

static int menu_rows(void) {
    if (sScreen == MENU_MAIN) return 3 + !!PSP_MENU_DIAGNOSTICS;
    if (sScreen == MENU_GRAPHICS) return 3 + !!PSP_FPS_OVERLAY + PSP_GFX_BACKEND_SELECTABLE;
    if (sScreen == MENU_SETTINGS) return 2;
    if (sScreen == MENU_DIAGNOSTICS) {
        return 2 + (PSP_MENU_CAPTURES ? 3 : 0) + !!PROFILE_HW_COUNTERS +
               !!PSP_RENDERER_DIAGNOSTICS + !!(PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU);
    }
    return 1;
}

int PspMenu_IsOpen(void) {
    return sOpen;
}

#if PSP_MENU_CAPTURES
void PspMenu_RequestCapture(int start) {
    sAction = start ? ACTION_START : ACTION_STOP;
}
#endif

void PspMenu_Update(u32 pressed) {
    int activate = (pressed & PSP_CTRL_CROSS) != 0;
    int adjust = (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) != 0;
    int direction = (pressed & PSP_CTRL_LEFT) ? -1 : 1;
    int row;

    if (sMultiplayerActive) {
        menu_multiplayer_update(pressed);
        return;
    }
    if (pressed & PSP_CTRL_SELECT) {
        sOpen = !sOpen;
        sScreen = MENU_MAIN;
        sSelected = 0;
        return;
    }
    if (!sOpen) return;
    if (pressed & PSP_CTRL_CIRCLE) {
        if (sScreen == MENU_MAIN) sOpen = 0;
        sScreen = MENU_MAIN;
        sSelected = 0;
        return;
    }
    if (pressed & PSP_CTRL_UP) sSelected = (sSelected + menu_rows() - 1) % menu_rows();
    else if (pressed & PSP_CTRL_DOWN) sSelected = (sSelected + 1) % menu_rows();
    if (!activate && !adjust) return;

    if (sScreen == MENU_MAIN && activate) {
        if (sSelected == 0) sOpen = 0;
        else {
            sScreen = sSelected == 1 ? MENU_SETTINGS : sSelected == 2 ? MENU_GRAPHICS : MENU_DIAGNOSTICS;
            sSelected = 0;
        }
    } else if (sScreen == MENU_SETTINGS) {
        if (sSelected == 0) PspAudioMode_Request((PspAudioMode_GetRequested() + direction + PSP_AUDIO_MODE_COUNT) % PSP_AUDIO_MODE_COUNT);
        else sColour = (sColour + direction + 3) % 3;
    } else if (sScreen == MENU_GRAPHICS) {
        if (sSelected == 0) {
            int mode = PspDisplay_GetConfig()->mode;
            PspSettings_SetDisplayMode((mode + direction + N64PSP_DISPLAY_PSP_MODE_COUNT) % N64PSP_DISPLAY_PSP_MODE_COUNT);
        } else if (sSelected == 1) {
            PspSettings_SetPresentationHz(PspFrameScheduler_GetPresentationVIs() == 1 ? 30 : 60);
        } else if (sSelected == 2) {
            PspSettings_SetGraphicsQuality(PspSettings_GetGraphicsQuality() == PSP_GRAPHICS_ACCURATE ?
                                           PSP_GRAPHICS_FAST : PSP_GRAPHICS_ACCURATE);
#if PSP_GFX_BACKEND_SELECTABLE
        } else if (sSelected == 3) {
            PspGfxBackendKind backend = PspSettings_GetGraphicsBackend();
            PspSettings_SetGraphicsBackend(backend == PSP_GFX_KIND_GU ? PSP_GFX_KIND_PSPGL : PSP_GFX_KIND_GU);
#endif
#if PSP_FPS_OVERLAY
        } else if (sSelected == (3 + PSP_GFX_BACKEND_SELECTABLE)) {
            PspSettings_TogglePerformance();
#endif
        }
    } else if (sScreen == MENU_DIAGNOSTICS) {
        row = 1;
#if PSP_MENU_CAPTURES
        if (sSelected == row++) sAction = ACTION_TYPE;
        else if (sSelected == row++ && activate) sAction = ACTION_START;
        else if (sSelected == row++ && activate) sAction = ACTION_STOP;
        else
#endif
#if PROFILE_HW_COUNTERS
        if (sSelected == row++) sAction = direction < 0 ? ACTION_SCENE_PREV : ACTION_SCENE_NEXT;
        else
#endif
#if PSP_RENDERER_DIAGNOSTICS
        if (sSelected == row++ && activate) sAction = ACTION_TRACE;
        else
#endif
#if PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU
        if (sSelected == row++) sAction = ACTION_COVERAGE;
        else
#endif
        if (sSelected == row && activate) { sScreen = MENU_MAIN; sSelected = 0; }
    } else if (activate) {
        sScreen = MENU_MAIN;
        sSelected = 0;
    }
}

int PspMenu_NeedsBoundary(void) {
#if PSP_MENU_DIAGNOSTICS
    if (sAction != ACTION_NONE) return 1;
#endif
#if PROFILE_GPROF || PROFILE_PHASES
    if (PspProfiler_NeedsService()) return 1;
#endif
#if PROFILE_HW_COUNTERS
    if (PspHwCounterProfile_NeedsService()) return 1;
#endif
    return PspSettings_NeedsBoundary();
}

// Diagnostic writes run on the scheduler thread with no graphics task in flight
void PspMenu_Apply(void) {
#if PROFILE_GPROF || PROFILE_PHASES
    PspProfiler_Service();
#endif
    PspHwCounterProfile_Service();
#if PSP_MENU_CAPTURES
#if PROFILE_CENSUS
    if (sAction == ACTION_START) PspCensus_Start();
    if (sAction == ACTION_STOP) PspCensus_Save();
#else
    if (sAction == ACTION_START) {
        if (!PspProfiler_IsCapturing()
#if PROFILE_HW_COUNTERS
            && !PspHwCounterProfile_IsBusy()
#endif
        ) {
#if PROFILE_HW_COUNTERS
            if (sHardware) PspHwCounterProfile_StartCapture();
            else
#endif
            PspProfiler_StartCapture();
        }
    } else if (sAction == ACTION_STOP) {
#if PROFILE_HW_COUNTERS
        if (sHardware) PspHwCounterProfile_StopCapture();
        else
#endif
        {
            PspProfiler_StopCapture();
            PspProfiler_DumpCapture();
        }
    } else if (sAction == ACTION_TYPE) {
#if PROFILE_HW_COUNTERS && (PROFILE_GPROF || PROFILE_PHASES)
        if (!PspProfiler_IsCapturing() && !PspHwCounterProfile_IsBusy()) sHardware = !sHardware;
#endif
    }
#endif
#endif
#if PROFILE_HW_COUNTERS
    if (sAction == ACTION_SCENE_PREV) PspHwCounterProfile_CycleScene(-1);
    if (sAction == ACTION_SCENE_NEXT) PspHwCounterProfile_CycleScene(1);
#endif
#if PSP_RENDERER_DIAGNOSTICS
    if (sAction == ACTION_TRACE) PspGfxDl_TraceRequest();
#endif
#if PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU
    if (sAction == ACTION_COVERAGE) PspGfxDl_ToggleNativeCoverage();
#endif
#if PSP_MENU_DIAGNOSTICS
    sAction = ACTION_NONE;
#endif
    PspSettings_Apply();
}

void PspMenu_Publish(void) {
    PspMenuView view;
    int n = 0;
    int lock;
    if (!sOpen) {
        lock = sceKernelCpuSuspendIntr();
        sView.open = 0;
        sceKernelCpuResumeIntr(lock);
        return;
    }
    view = (PspMenuView){ 0 };
    view.open = 1;
    view.selected = sSelected;
    view.cursor = sSelected;
    view.main = sScreen == MENU_MAIN;
    view.accent = sAccents[sColour];
#define ROW(...) snprintf(view.rows[n++], MENU_TEXT, __VA_ARGS__)
    if (sMultiplayerActive) {
        static const char* const matches[] = { "Point Match", "Battle Royal", "Time Trial" };
        int wlan = sceWlanGetSwitchState();
        char channel[16];

        if (sMultiplayerChannel == 0) snprintf(channel, sizeof(channel), "%s", "Auto");
        else if (sMultiplayerChannel == 1 || sMultiplayerChannel == 6 || sMultiplayerChannel == 11)
            snprintf(channel, sizeof(channel), "%d", sMultiplayerChannel);
        else snprintf(channel, sizeof(channel), "%s", "Unknown");
        snprintf(view.status, MENU_TEXT, "WLAN: %s   Channel: %s",
                 wlan < 0 ? "Unknown" : wlan ? "On" : "Off", channel);
        if (sScreen == MENU_MULTIPLAYER) {
            snprintf(view.title, MENU_TEXT, "%s", "Ad-Hoc Multiplayer");
            snprintf(view.subtitle, MENU_TEXT, "%s", sMultiplayerMatchType >= 0 && sMultiplayerMatchType < 3 ?
                     matches[sMultiplayerMatchType] : "Versus");
            ROW("Host"); ROW("Join"); ROW("Continue Solo"); ROW("Back");
        } else {
            int host = sScreen == MENU_MULTIPLAYER_HOST;
            snprintf(view.title, MENU_TEXT, "Ad-Hoc %s", host ? "Host" : "Join");
            snprintf(view.subtitle, MENU_TEXT, "%s", host ? "Hosting is not available yet" : "Search is not available yet");
            ROW("Continue Solo"); ROW("Back");
        }
    } else if (sScreen == MENU_MAIN) {
        snprintf(view.title, MENU_TEXT, "%s", "Star Fox 64 PSP");
        ROW("Resume Game"); ROW("Settings"); ROW("Graphics & Display");
#if PSP_MENU_DIAGNOSTICS
        ROW("Performance / Diagnostics");
#endif
    } else if (sScreen == MENU_SETTINGS) {
        static const char* const audio[] = { "Off", "Sync", "Async (fast)" };
        static const char* const colours[] = { "Green", "Red", "Blue" };
        snprintf(view.title, MENU_TEXT, "%s", "Settings");
        ROW("Audio: %s", audio[PspAudioMode_GetRequested()]);
        ROW("Colour: %s", colours[sColour]);
        if (PspAudioMode_GetRequested() != PspAudioMode_GetActive())
            snprintf(view.status, MENU_TEXT, "%s", "Switching audio...");
    } else if (sScreen == MENU_GRAPHICS) {
        int mode = PspDisplay_GetConfig()->mode;
        int vis = PspFrameScheduler_GetPresentationVIs();
        snprintf(view.title, MENU_TEXT, "%s", "Graphics & Display");
        static const char* const modes[] = { "Original", "4:3", "16:9" };
        ROW("Aspect: %s", modes[mode]);
        ROW("Presentation: %d FPS", 60 / vis);
        ROW("Graphics: %s", PspSettings_GetGraphicsQuality() == PSP_GRAPHICS_FAST ? "Fast" : "Accurate");
#if PSP_GFX_BACKEND_SELECTABLE
        ROW("Graphics Backend: %s", menu_backend_name(PspSettings_GetGraphicsBackend()));
#endif
#if PSP_FPS_OVERLAY
        ROW("Performance overlay: %s", PspSettings_PerformanceVisible() ? "On" : "Off");
#endif
    } else if (sScreen == MENU_DIAGNOSTICS) {
        snprintf(view.title, MENU_TEXT, "%s", "Performance / Diagnostics");
        ROW("Active backend: %s", menu_backend_name(PspSettings_GetActiveGraphicsBackend()));
#if PSP_MENU_CAPTURES
        ROW("Capture: %s", PROFILE_CENSUS ? "Coarse census" : sHardware ? "Hardware counters" : "Phase / gprof");
        ROW("Start capture"); ROW("Stop and save capture");
        snprintf(view.status, MENU_TEXT, "%s: %s", sHardware ? "HW" : "PROF",
#if PROFILE_HW_COUNTERS
                 sHardware ? PspHwCounterProfile_GetStatus() :
#endif
                 PROFILE_CENSUS ? PspCensus_Status() : PspProfiler_GetStatus());
#endif
#if PROFILE_HW_COUNTERS
        ROW("Capture scene: %s", PspHwCounterProfile_GetScene());
#endif
#if PSP_RENDERER_DIAGNOSTICS
        ROW("Trace: %s", PspGfxDl_TracePending() ? "Armed" : "Capture next task");
#endif
#if PSP_NATIVE_COVERAGE_AB && !PSP_GFX_BACKEND_GU
        {
            PspGfxDlNativeCoverageStats stats;
            PspGfxDl_GetNativeCoverageStats(&stats);
            ROW("Native coverage mode: %lu", (unsigned long) stats.mode);
        }
#endif
        ROW("Back");
    }

#undef ROW
    view.count = n;
    lock = sceKernelCpuSuspendIntr();
    sView = view;
    sceKernelCpuResumeIntr(lock);
}

int PspMenu_Visible(void) {
    int lock = sceKernelCpuSuspendIntr();
    int open = sView.open;
    sceKernelCpuResumeIntr(lock);
    return open;
}

static void menu_label(int x, int y, int width, int centered, u32 color, const char* text) {
    char label[MENU_TEXT];
    int length = 0;
    int clipped = 0;
    while (text[length] && length < MENU_TEXT - 1) {
        label[length] = text[length];
        length++;
    }
    label[length] = 0;
    if (PspOverlay_MenuTextWidth(label) > width) {
        clipped = 1;
        while (length > 3 && PspOverlay_MenuTextWidth(label) + PspOverlay_MenuTextWidth("...") > width) {
            label[--length] = 0;
        }
    }
    if (clipped && length < MENU_TEXT - 3) {
        label[length++] = '.';
        label[length++] = '.';
        label[length++] = '.';
        label[length] = 0;
    }
    if (centered) x += (width - PspOverlay_MenuTextWidth(label)) / 2;
    PspOverlay_MenuText(x, y, color, label);
}

void PspMenu_Draw(void) {
    PspMenuView view;
    int visible, first, height, top, rowTop, rowHeight, subtitleHeight, i;
    int lock = sceKernelCpuSuspendIntr();
    view = sView;
    sceKernelCpuResumeIntr(lock);
    if (!view.open) return;
    visible = view.count < MENU_VISIBLE_ROWS ? view.count : MENU_VISIBLE_ROWS;
    first = view.cursor - (view.selected >= 0 ? visible / 2 : 0);
    if (first > view.count - visible) first = view.count - visible;
    if (first < 0) first = 0;
    rowHeight = view.main ? 36 : 24;
    subtitleHeight = view.subtitle[0] ? 20 : 0;
    height = visible * rowHeight + (view.main ? 20 : 50) + subtitleHeight + (view.status[0] ? 24 : 0);
    top = (272 - height) / 2;
    rowTop = top + (view.main ? 10 : 38) + subtitleHeight;
    PspOverlay_MenuBegin();
    PspOverlay_Rect(90, top, 300, height, 0xE0000000);
    PspOverlay_Rect(90, top, 300, 1, view.accent);
    PspOverlay_Rect(90, top + height - 1, 300, 1, view.accent);
    PspOverlay_Rect(90, top + 1, 1, height - 2, view.accent);
    PspOverlay_Rect(389, top + 1, 1, height - 2, view.accent);
    if (!view.main) menu_label(110, top + 9, 260, 1, 0xFFF5FFFF, view.title);
    if (view.subtitle[0]) menu_label(110, top + 31, 260, 1, 0xFFB4BEAA, view.subtitle);
    for (i = 0; i < visible; i++) {
        int row = first + i;
        int y = rowTop + i * rowHeight;
        u32 color = 0xFFDAE0DA;
        if (row == view.selected) {
            PspOverlay_Rect(104, y + (view.main ? 2 : 0), 272, view.main ? 30 : 24, view.accent);
            color = 0xFFF5FFFF;
        }
        menu_label(114, y + (view.main ? 7 : 2), 252, view.main, color, view.rows[row]);
    }
    if (first > 0) PspOverlay_MenuText(375, rowTop - 4, 0xFFB4BEAA, "^");
    if (first + visible < view.count)
        PspOverlay_MenuText(375, rowTop + (visible - 1) * rowHeight + 3, 0xFFB4BEAA, "v");
    if (view.status[0]) menu_label(110, rowTop + visible * rowHeight + 4, 260, 1, 0xFFB4BEAA, view.status);
}
