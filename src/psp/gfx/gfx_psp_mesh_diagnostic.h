#ifndef PSP_GFX_MESH_DIAGNOSTIC_H
#define PSP_GFX_MESH_DIAGNOSTIC_H

#include <pspkernel.h>
#include <stdint.h>

enum {
    MESH_DIAG_LOOKUP, MESH_DIAG_STATE, MESH_DIAG_BOUNDS, MESH_DIAG_MATERIALS,
    MESH_DIAG_ALLOCATE, MESH_DIAG_ADMIT, MESH_DIAG_REJECT, MESH_DIAG_PROOF,
    MESH_DIAG_DRAIN, MESH_DIAG_LIGHTS, MESH_DIAG_VERTICES, MESH_DIAG_DRAW_STATE,
    MESH_DIAG_SUBMIT, MESH_DIAG_RESTORE, MESH_DIAG_DISCARD, MESH_DIAG_DRAW,
    MESH_DIAG_FALLBACK, MESH_DIAG_PHASES
};
enum {
    MESH_REJECT_STATE, MESH_REJECT_FOG, MESH_REJECT_TRACE, MESH_REJECT_CACHE,
    MESH_REJECT_BOUNDS, MESH_REJECT_MATERIALS, MESH_REJECT_ALLOCATE, MESH_REJECT_REASONS
};
typedef struct {
    uint32_t us[MESH_DIAG_PHASES], calls[MESH_DIAG_PHASES];
    uint32_t rejects[MESH_REJECT_REASONS];
    uint32_t unregistered, excluded, proofEffects, proofLookups, proofUnknown, proofDead, proofLive;
    uint32_t taskUs, frontendUs, finishUs, syncUs, commands, loaded, submitted, workStats;
} PspMeshDiagnostic;
extern PspMeshDiagnostic sMeshDiagnostic;
extern uint32_t sMeshDiagnosticEnabled;
static inline uint32_t PspMeshDiagnostic_Start(void) {
    return sMeshDiagnosticEnabled ? sceKernelGetSystemTimeLow() : 0;
}
void __attribute__((noinline)) PspMeshDiagnostic_End(uint32_t phase, uint32_t start);
void PspMeshDiagnostic_Reject(uint32_t reason, uint32_t start);
int PspMeshDiagnostic_PollControls(uint32_t buttons);
void PspMeshDiagnostic_BeginTask(void);
void PspMeshDiagnostic_SubmitStart(void);
void PspMeshDiagnostic_SubmitEnd(void);
void PspMeshDiagnostic_RecordSync(uint32_t us);
int PspMeshDiagnostic_Sync(void);
void PspMeshDiagnostic_EndTask(int presented);
void PspMeshDiagnostic_Status(uint32_t* enabled, uint32_t* remaining, uint32_t* status);

#endif
