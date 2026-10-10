#include "gfx_psp_mesh_diagnostic.h"
#include "gfx_psp_dl.h"
#include <pspctrl.h>
#include <pspgu.h>
#include <stdio.h>

#define MESH_DIAG_WARMUP 60
#define MESH_DIAG_SAMPLES 180

typedef struct {
    PspMeshDiagnostic cost;
    PspGfxDlRetainedStats work;
    uint32_t timed;
} PspMeshDiagnosticRow;

PspMeshDiagnostic sMeshDiagnostic;
uint32_t sMeshDiagnosticEnabled;
static PspMeshDiagnosticRow sRows[MESH_DIAG_SAMPLES];
static uint32_t sRemaining, sStatus, sCapture, sMode, sTimed, sStarted;
static uint32_t sTaskStart, sSubmitStart, sPreviousButtons;

void PspMeshDiagnostic_End(uint32_t phase, uint32_t start) {
    if (sMeshDiagnosticEnabled) {
        sMeshDiagnostic.us[phase] += sceKernelGetSystemTimeLow() - start;
        sMeshDiagnostic.calls[phase]++;
    }
}

void PspMeshDiagnostic_Reject(uint32_t reason, uint32_t start) {
    if (sMeshDiagnosticEnabled) sMeshDiagnostic.rejects[reason]++;
    PspMeshDiagnostic_End(MESH_DIAG_REJECT, start);
}

int PspMeshDiagnostic_PollControls(uint32_t buttons) {
    uint32_t combo = PSP_CTRL_SELECT | PSP_CTRL_UP;
    int pressed = (buttons & combo) == combo && (sPreviousButtons & combo) != combo;
    sPreviousButtons = buttons;
    if ((buttons & combo) != combo) return 0;
    if (pressed) {
        if (buttons & PSP_CTRL_LTRIGGER) {
            sMeshDiagnosticEnabled ^= 1;
            if (sRemaining) { sRemaining = 0; sStatus = 4; }
        } else {
            sRemaining = MESH_DIAG_WARMUP + MESH_DIAG_SAMPLES;
            sStatus = 1;
            sStarted = 0;
            sCapture++;
        }
    }
    return 1;
}

void PspMeshDiagnostic_BeginTask(void) {
    sMeshDiagnostic = (PspMeshDiagnostic) {0};
    sTaskStart = sceKernelGetSystemTimeLow();
}

void PspMeshDiagnostic_SubmitStart(void) {
    sSubmitStart = sceKernelGetSystemTimeLow();
    sMeshDiagnostic.frontendUs = sSubmitStart - sTaskStart;
}

void PspMeshDiagnostic_SubmitEnd(void) {
    uint32_t now = sceKernelGetSystemTimeLow();
    sMeshDiagnostic.finishUs = now - sSubmitStart;
    sMeshDiagnostic.taskUs = now - sTaskStart;
}

void PspMeshDiagnostic_RecordSync(uint32_t us) {
    sMeshDiagnostic.syncUs += us;
}

int PspMeshDiagnostic_Sync(void) {
    uint32_t start = sceKernelGetSystemTimeLow();
    int result = sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sMeshDiagnostic.syncUs += sceKernelGetSystemTimeLow() - start;
    return result;
}

static int psp_mesh_diagnostic_write(void) {
    static const char* names[MESH_DIAG_PHASES] = {
        "lookup", "state", "bounds", "materials", "allocate", "admit", "reject", "proof",
        "drain", "lights", "vertices", "draw_state", "submit", "restore", "discard", "draw", "fallback"
    };
    FILE* file = fopen("ms0:/native-mesh-diagnostic.csv", "a");
    uint32_t i, phase;
    int failed = 0;
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) || ftell(file) < 0) failed = 1;
    if (!failed && ftell(file) == 0) {
        fprintf(file, "capture,frame,mode,timed,H,F,D,V,R,S,task_us,frontend_us,finish_us,sync_us,commands,loaded,submitted,work_stats");
        for (phase = 0; phase < MESH_DIAG_PHASES; phase++) fprintf(file, ",%s_us,%s_calls", names[phase], names[phase]);
        fprintf(file, ",reject_state,reject_fog,reject_trace,reject_cache,reject_bounds,reject_materials,reject_allocate,unregistered,excluded,proof_effects,proof_lookups,proof_unknown,proof_dead,proof_live\n");
    }
    for (i = 0; !failed && i < MESH_DIAG_SAMPLES; i++) {
        const PspMeshDiagnosticRow* row = &sRows[i];
        const PspMeshDiagnostic* c = &row->cost;
        const PspGfxDlRetainedStats* w = &row->work;
        fprintf(file, "%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
            (unsigned long) sCapture, (unsigned long) i, (unsigned long) w->mode, (unsigned long) row->timed,
            (unsigned long) w->hits, (unsigned long) w->fallbacks, (unsigned long) w->spans,
            (unsigned long) w->vertices, (unsigned long) w->restored, (unsigned long) w->skipped,
            (unsigned long) c->taskUs, (unsigned long) c->frontendUs, (unsigned long) c->finishUs,
            (unsigned long) c->syncUs, (unsigned long) c->commands, (unsigned long) c->loaded,
            (unsigned long) c->submitted, (unsigned long) c->workStats);
        for (phase = 0; phase < MESH_DIAG_PHASES; phase++) fprintf(file, ",%lu,%lu", (unsigned long) c->us[phase], (unsigned long) c->calls[phase]);
        for (phase = 0; phase < MESH_REJECT_REASONS; phase++) fprintf(file, ",%lu", (unsigned long) c->rejects[phase]);
        fprintf(file, ",%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", (unsigned long) c->unregistered,
            (unsigned long) c->excluded, (unsigned long) c->proofEffects, (unsigned long) c->proofLookups,
            (unsigned long) c->proofUnknown, (unsigned long) c->proofDead, (unsigned long) c->proofLive);
        if (ferror(file)) failed = 1;
    }
    if (fclose(file)) failed = 1;
    return !failed;
}

void PspMeshDiagnostic_EndTask(int presented) {
    PspGfxDlRetainedStats work;
    if (!sRemaining || !presented) return;
    PspGfxDl_GetRetainedStats(&work);
    if (!sStarted) {
        sMode = work.mode; sTimed = sMeshDiagnosticEnabled; sStarted = 1;
    }
    if (sMode != work.mode || sTimed != sMeshDiagnosticEnabled) {
        sRemaining = 0; sStatus = 4; return;
    }
    if (sRemaining <= MESH_DIAG_SAMPLES) {
        PspMeshDiagnosticRow* row = &sRows[MESH_DIAG_SAMPLES - sRemaining];
        row->cost = sMeshDiagnostic; row->work = work; row->timed = sMeshDiagnosticEnabled;
    }
    if (!--sRemaining) sStatus = psp_mesh_diagnostic_write() ? 2 : 3;
}

void PspMeshDiagnostic_Status(uint32_t* enabled, uint32_t* remaining, uint32_t* status) {
    *enabled = sMeshDiagnosticEnabled; *remaining = sRemaining; *status = sStatus;
}
