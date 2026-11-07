#include "src/psp/spectate.h"
#include <pspctrl.h>

#include <pspkernel.h>
#include "src/psp/input.h"
#include "src/psp/menu.h"

static OSContPad sPad;
static u32 sPreviousButtons;
static volatile int sReleaseGate;
static volatile int sReleaseReady;
#if PSP_MENU_CAPTURES
static u32 sCaptureCombo;
static u32 sCaptureSince;
static int sCaptureFired;
#endif

static const PspInputBinding sBindings[] = {
    { "D-pad Up", "C-Up", PSP_CTRL_UP, U_CBUTTONS },
    { "D-pad Down", "C-Down", PSP_CTRL_DOWN, D_CBUTTONS },
    { "D-pad Left", "C-Left", PSP_CTRL_LEFT, L_CBUTTONS },
    { "D-pad Right", "C-Right", PSP_CTRL_RIGHT, R_CBUTTONS },
    { "Cross", "A", PSP_CTRL_CROSS, A_BUTTON },
    { "Circle", "B", PSP_CTRL_CIRCLE, B_BUTTON },
    { "Square", "C-Down", PSP_CTRL_SQUARE, D_CBUTTONS },
    { "Triangle", "C-Left", PSP_CTRL_TRIANGLE, L_CBUTTONS },
    { "L", "Z", PSP_CTRL_LTRIGGER, Z_TRIG },
    { "R", "R", PSP_CTRL_RTRIGGER, R_TRIG },
    { "Start", "Start", PSP_CTRL_START, START_BUTTON }
};

const PspInputBinding* PspInput_GetBindings(int* count) {
    *count = sizeof(sBindings) / sizeof(sBindings[0]);
    return sBindings;
}

static s8 psp_input_scale_axis(u8 value, int invert) {
    s32 centered = (s32) value - 128;
    s32 scaled;

    if (centered < 0) {
        scaled = (centered * 80) / 128;
    } else {
        scaled = (centered * 80) / 127;
    }

    if (invert) {
        scaled = -scaled;
    }

    if (scaled < -80) {
        scaled = -80;
    } else if (scaled > 80) {
        scaled = 80;
    }
    return (s8) scaled;
}

static void psp_input_map_buttons(const SceCtrlData* in, OSContPad* out) {
    const u32 buttons = in->Buttons;

    out->button = 0;
    out->stick_x = psp_input_scale_axis(in->Lx, 0);
    out->stick_y = psp_input_scale_axis(in->Ly, 1);
    out->errno = 0;

    {
        unsigned i;
        for (i = 0; i < sizeof(sBindings) / sizeof(sBindings[0]); i++) {
            if (buttons & sBindings[i].pspMask) out->button |= sBindings[i].n64Mask;
        }
    }
}

void PspInput_Init(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

// Only the scheduler thread polls the PSP controller and changes menu state
void PspInput_Update(void) {
    SceCtrlData pad;
    OSContPad mapped = { 0 };
    u32 pressed;
    int wasOpen;
    int lock;
    if (sceCtrlPeekBufferPositive(&pad, 1) <= 0) return;
    pressed = pad.Buttons & ~sPreviousButtons;
    sPreviousButtons = pad.Buttons;
    wasOpen = PspMenu_IsOpen();
    PspMenu_Update(pressed);
    if (wasOpen || PspMenu_IsOpen()) sReleaseGate = 1;

#if PSP_MENU_CAPTURES
    if (!wasOpen && !PspMenu_IsOpen() && !sReleaseGate) {
        u32 combo = 0;
        u32 triggers = pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER);
        if ((pad.Buttons & PSP_CTRL_UP) &&
            (triggers == PSP_CTRL_LTRIGGER || triggers == PSP_CTRL_RTRIGGER)) combo = triggers;
        if (combo != sCaptureCombo) {
            sCaptureCombo = combo;
            sCaptureSince = sceKernelGetSystemTimeLow();
            sCaptureFired = 0;
        }
        if (combo) {
            if (!sCaptureFired && (u32)(sceKernelGetSystemTimeLow() - sCaptureSince) >= 250000) {
                PspMenu_RequestCapture(combo == PSP_CTRL_LTRIGGER);
                sCaptureFired = 1;
            }
            pad.Buttons &= ~(PSP_CTRL_UP | combo);
        }
    } else {
        sCaptureCombo = 0;
        sCaptureFired = 0;
    }
#endif
    if (!PspMenu_IsOpen() && !sReleaseGate) psp_input_map_buttons(&pad, &mapped);
    sReleaseReady = 0;
    if (sReleaseGate && !PspMenu_IsOpen() && pad.Buttons == 0) {
        s8 x = psp_input_scale_axis(pad.Lx, 0);
        s8 y = psp_input_scale_axis(pad.Ly, 0);
        sReleaseReady = x >= -16 && x <= 16 && y >= -16 && y <= 16;
    }
    lock = sceKernelCpuSuspendIntr();
    sPad = mapped;
    sceKernelCpuResumeIntr(lock);
}

// The serial interface reads a coherent snapshot without polling hardware
void PspInput_Poll(OSContPad* pads) {
    int lock;
    s32 i;
    for (i = 0; i < MAXCONTROLLERS; i++) {
        pads[i].button = 0;
        pads[i].stick_x = 0;
        pads[i].stick_y = 0;
        pads[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }
    lock = sceKernelCpuSuspendIntr();
    pads[0] = sPad;
    sceKernelCpuResumeIntr(lock);
}

void PspInput_RequireRelease(void) {
    int lock = sceKernelCpuSuspendIntr();
    sReleaseGate = 1;
    sReleaseReady = 0;
    sPad.button = 0;
    sPad.stick_x = sPad.stick_y = 0;
    sceKernelCpuResumeIntr(lock);
}

// Preserve serial input locks and supply one neutral simulation sample on close
void PspInput_FilterGameInput(OSContPad* pads) {
    int i;
    if (PspSpectate_IsWatching()) {
        for (i = 0; i < 4; i++) {
            pads[i].button = 0;
            pads[i].stick_x = pads[i].stick_y = 0;
        }
    }
    if (sReleaseGate || PspMenu_IsOpen() || PspSpectate_IsWatching()) {
        pads[0].button = 0;
        pads[0].stick_x = 0;
        pads[0].stick_y = 0;
        if (sReleaseReady) sReleaseGate = 0;
    }
}
