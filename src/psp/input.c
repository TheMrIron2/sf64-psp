#include <pspctrl.h>

#include "src/psp/input.h"
#include "src/psp/profiler.h"

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
    const int cButtonMode = (buttons & PSP_CTRL_SELECT) != 0;

    out->button = 0;
    out->stick_x = psp_input_scale_axis(in->Lx, 0);
    out->stick_y = psp_input_scale_axis(in->Ly, 1);
    out->errno = 0;

    if (buttons & PSP_CTRL_CROSS) {
        out->button |= A_BUTTON;
    }
    if (buttons & PSP_CTRL_CIRCLE) {
        out->button |= B_BUTTON;
    }
    if (buttons & PSP_CTRL_SQUARE) {
        out->button |= D_CBUTTONS;
    }
    if (buttons & PSP_CTRL_TRIANGLE) {
        out->button |= L_CBUTTONS;
    }
    if (buttons & PSP_CTRL_LTRIGGER) {
        out->button |= Z_TRIG;
    }
    if (buttons & PSP_CTRL_RTRIGGER) {
        out->button |= R_TRIG;
    }
    if (buttons & PSP_CTRL_START) {
        out->button |= START_BUTTON;
    }

    if (cButtonMode) {
        if (buttons & PSP_CTRL_UP) {
            out->button |= U_CBUTTONS;
        }
        if (buttons & PSP_CTRL_DOWN) {
            out->button |= D_CBUTTONS;
        }
        if (buttons & PSP_CTRL_LEFT) {
            out->button |= L_CBUTTONS;
        }
        if (buttons & PSP_CTRL_RIGHT) {
            out->button |= R_CBUTTONS;
        }
    } else {
        if (buttons & PSP_CTRL_UP) {
            out->button |= U_JPAD;
        }
        if (buttons & PSP_CTRL_DOWN) {
            out->button |= D_JPAD;
        }
        if (buttons & PSP_CTRL_LEFT) {
            out->button |= L_JPAD;
        }
        if (buttons & PSP_CTRL_RIGHT) {
            out->button |= R_JPAD;
        }
    }
}

void PspInput_Init(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

int PspInput_Poll(OSContPad* pads) {
    SceCtrlData pad;
    s32 i;

    for (i = 0; i < MAXCONTROLLERS; i++) {
        pads[i].button = 0;
        pads[i].stick_x = 0;
        pads[i].stick_y = 0;
        pads[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }

    if (sceCtrlPeekBufferPositive(&pad, 1) > 0) {
        if (PspProfiler_PollControls(pad.Buttons)) {
            pad.Buttons &= ~(PSP_CTRL_SELECT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER);
        }
        psp_input_map_buttons(&pad, &pads[0]);
        return (pad.Buttons & (PSP_CTRL_SELECT | PSP_CTRL_START)) == (PSP_CTRL_SELECT | PSP_CTRL_START);
    }

    return 0;
}
