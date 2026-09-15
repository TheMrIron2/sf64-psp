#include "global.h"
#include "src/psp/frame_interpolation.h"
#include "src/psp/profiler.h"

#define PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY 0x480

typedef struct {
    u32 requested;
    u32 interpolated;
    u32 exact;
    u32 fallback;
    u32 missingHistory;
    u32 topologyMismatch;
    u32 matricesInterpolated;
    u32 renderOnly;
    u32 repeatFallback;
    u32 repeated;
    f32 alpha;
} PspFrameInterpolationPresentation;

typedef struct {
    u32 simulationVi;
    u32 generation;
    u16 worldEnd;
    u16 worldCount;
    u8 simulationVIs;
    u8 valid;
    u8 eligible;
    u8 flags[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    u8 world[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    u8 wrapAxis[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    f32 wrapPeriod[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    const void* drawSite[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    PspFrameInterpolationPresentation presentation;
} PspFrameInterpolationPool;

static PspFrameInterpolationPool sPools[2];
static u32 sGeneration;
static s32 sRecordingPool = -1;
static s32 sPresentationPool = -1;
static s32 sWorldScope;
static PspFrameInterpolationWrapAxis sWrapAxis;
static f32 sWrapPeriod;

static s32 psp_frame_interpolation_pool_for_task(const SPTask* task) {
    if (task == &gGfxPools[0].task) {
        return 0;
    }
    if (task == &gGfxPools[1].task) {
        return 1;
    }
    return -1;
}

static s32 psp_frame_interpolation_matrix_index(const Mtx* matrix, s32 pool) {
    uintptr_t address = (uintptr_t) matrix;
    uintptr_t base = (uintptr_t) gGfxPools[pool].mtx;
    uintptr_t end = base + sizeof(gGfxPools[pool].mtx);

    if ((address < base) || (address >= end) || (((address - base) % sizeof(Mtx)) != 0)) {
        return -1;
    }
    return (s32) ((address - base) / sizeof(Mtx));
}

static void psp_frame_interpolation_clear_presentation(PspFrameInterpolationPresentation* presentation) {
    presentation->requested = 0;
    presentation->interpolated = 0;
    presentation->exact = 0;
    presentation->fallback = 0;
    presentation->missingHistory = 0;
    presentation->topologyMismatch = 0;
    presentation->matricesInterpolated = 0;
    presentation->renderOnly = 0;
    presentation->repeatFallback = 0;
    presentation->repeated = 0;
    presentation->alpha = 1.0f;
}

void PspFrameInterpolation_Reset(void) {
    sPools[0].valid = false;
    sPools[1].valid = false;
    sPools[0].eligible = false;
    sPools[1].eligible = false;
    psp_frame_interpolation_clear_presentation(&sPools[0].presentation);
    psp_frame_interpolation_clear_presentation(&sPools[1].presentation);
    sGeneration = 0;
    sRecordingPool = -1;
    sPresentationPool = -1;
    sWorldScope = false;
    sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
    sWrapPeriod = 0.0f;
}

void PspFrameInterpolation_BeginSimulationFrame(SPTask* task, u32 simulationVi, u8 simulationVIs, s32 record) {
    s32 pool = psp_frame_interpolation_pool_for_task(task);
    PspFrameInterpolationPool* state;

    sRecordingPool = -1;
    sWorldScope = false;
    sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
    sWrapPeriod = 0.0f;
    if (pool < 0) {
        return;
    }

    state = &sPools[pool];
    state->simulationVi = simulationVi;
    state->generation = ++sGeneration;
    state->worldEnd = 0;
    state->worldCount = 0;
    state->simulationVIs = simulationVIs;
    state->valid = false;
    state->eligible = false;
    psp_frame_interpolation_clear_presentation(&state->presentation);
    if (record) {
        sRecordingPool = pool;
    }
}

void PspFrameInterpolation_SetWorldScope(s32 enabled) {
    sWorldScope = enabled != 0;
}

void PspFrameInterpolation_SetMatrixWrap(PspFrameInterpolationWrapAxis axis, f32 distance, f32 scale) {
    if ((axis <= PSP_FRAME_INTERPOLATION_WRAP_NONE) || (axis > PSP_FRAME_INTERPOLATION_WRAP_Z) ||
        (distance == 0.0f) || (scale == 0.0f)) {
        sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
        sWrapPeriod = 0.0f;
        return;
    }
    sWrapAxis = axis;
    sWrapPeriod = distance / scale;
}

void PspFrameInterpolation_RecordMatrix(const Mtx* matrix, u32 flags, const void* drawSite) {
    PspFrameInterpolationPool* state;
    s32 world;
    s32 index;

    if (sRecordingPool < 0) {
        return;
    }
    index = psp_frame_interpolation_matrix_index(matrix, sRecordingPool);
    if ((index < 0) || (index >= PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY)) {
        return;
    }

    state = &sPools[sRecordingPool];
    world = sWorldScope && ((flags & G_MTX_PROJECTION) == 0);
    state->flags[index] = (u8) flags;
    state->world[index] = world;
    state->drawSite[index] = drawSite;
    state->wrapAxis[index] = world ? (u8) sWrapAxis : PSP_FRAME_INTERPOLATION_WRAP_NONE;
    state->wrapPeriod[index] = world ? sWrapPeriod : 0.0f;
    if (world) {
        state->worldCount++;
        state->worldEnd = index + 1;
    }
}

void PspFrameInterpolation_EndSimulationFrame(SPTask* task, s32 eligible) {
    s32 pool = psp_frame_interpolation_pool_for_task(task);

    if ((pool >= 0) && (pool == sRecordingPool)) {
        sPools[pool].valid = true;
        sPools[pool].eligible = (eligible != 0) && (sPools[pool].worldCount != 0);
    }
    sRecordingPool = -1;
    sWorldScope = false;
    sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
    sWrapPeriod = 0.0f;
}

static s32 psp_frame_interpolation_topology_matches(const PspFrameInterpolationPool* current,
                                                    const PspFrameInterpolationPool* previous) {
    u32 i;

    if ((current->worldEnd != previous->worldEnd) || (current->worldCount != previous->worldCount)) {
        return false;
    }
    for (i = 0; i < current->worldEnd; i++) {
        if ((current->flags[i] != previous->flags[i]) || (current->world[i] != previous->world[i]) ||
            (current->world[i] && ((current->drawSite[i] != previous->drawSite[i]) ||
                                   (current->wrapAxis[i] != previous->wrapAxis[i]) ||
                                   (current->wrapPeriod[i] != previous->wrapPeriod[i])))) {
            return false;
        }
    }
    return true;
}

SPTask* PspFrameInterpolation_PreparePresentation(SPTask* task, u32 presentationVi, u8 presentationVIs,
                                                  s32 renderOnly) {
    PspFrameInterpolationPool* current;
    PspFrameInterpolationPool* previous;
    PspFrameInterpolationPresentation* presentation;
    u32 span;
    u32 targetVi;
    u32 offset;
    s32 pool = psp_frame_interpolation_pool_for_task(task);

    sPresentationPool = -1;
    if (pool < 0) {
        return NULL;
    }

    current = &sPools[pool];
    previous = &sPools[pool ^ 1];
    presentation = &current->presentation;
    psp_frame_interpolation_clear_presentation(presentation);
    presentation->renderOnly = renderOnly != 0;
    if (!current->valid || !current->eligible || (presentationVIs >= current->simulationVIs)) {
        return NULL;
    }

    presentation->requested = 1;
    targetVi = presentationVi - presentationVIs;
    if (targetVi == current->simulationVi) {
        presentation->exact = 1;
        return NULL;
    }
    presentation->repeatFallback = presentationVIs == 1;
    if (!previous->valid || !previous->eligible || (current->generation != (previous->generation + 1))) {
        presentation->fallback = 1;
        presentation->missingHistory = 1;
        return NULL;
    }
    span = current->simulationVi - previous->simulationVi;
    offset = targetVi - previous->simulationVi;
    if ((span != current->simulationVIs) || ((s32) offset < 0) || (offset > span)) {
        presentation->fallback = 1;
        presentation->missingHistory = 1;
        return NULL;
    }
    if (!psp_frame_interpolation_topology_matches(current, previous)) {
        presentation->fallback = 1;
        presentation->topologyMismatch = 1;
        return NULL;
    }
    presentation->interpolated = 1;
    presentation->alpha = (f32) offset / (f32) span;
    sPresentationPool = pool;
    return &gGfxPools[pool ^ 1].task;
}

s32 PspFrameInterpolation_ShouldPresent(SPTask* task) {
    s32 pool = psp_frame_interpolation_pool_for_task(task);
    PspFrameInterpolationPresentation* presentation;

    if (pool < 0) {
        return true;
    }
    presentation = &sPools[pool].presentation;
    if ((presentation->repeatFallback != 0) && (presentation->fallback != 0)) {
        // fallback would draw current simulation state in a presentation intended for intermediate state
        // Keep the displayed image instead so the invalid midpoint cannot jump ahead for a frame
        presentation->repeated = 1;
        return false;
    }
    return true;
}

s32 PspFrameInterpolation_ResolveMatrix(const Mtx* currentMatrix, u32 flags, Matrix* result) {
    PspFrameInterpolationPool* current;
    PspFrameInterpolationPresentation* presentation;
    const Matrix* currentValue;
    const Matrix* previousValue;
    Matrix adjustedCurrent;
    f32 alpha;
    f32 rawDistanceSq;
    f32 negativeDistanceSq;
    f32 positiveDistanceSq;
    f32 difference;
    f32 wrapStep;
    f32 wrapDirection;
    u32 row;
    u32 column;
    u32 wrapRow;
    s32 pool;
    s32 index;

    pool = sPresentationPool;
    if (pool < 0) {
        return false;
    }
    index = psp_frame_interpolation_matrix_index(currentMatrix, pool);
    if (index < 0) {
        return false;
    }

    current = &sPools[pool];
    presentation = &current->presentation;
    if (!presentation->interpolated || (index >= current->worldEnd) || !current->world[index] ||
        (current->flags[index] != (u8) flags)) {
        return false;
    }

    currentValue = (const Matrix*) currentMatrix;
    previousValue = (const Matrix*) &gGfxPools[pool ^ 1].mtx[index];
    if (current->wrapAxis[index] != PSP_FRAME_INTERPOLATION_WRAP_NONE) {
        /* A wrapped coordinate has equivalent transforms one period in either
         * direction. Interpolate from the copy closest to the previous frame
         * instead of crossing the discontinuity in the raw coordinate. */
        wrapRow = current->wrapAxis[index] - 1;
        rawDistanceSq = negativeDistanceSq = positiveDistanceSq = 0.0f;
        for (column = 0; column < 3; column++) {
            difference = currentValue->m[3][column] - previousValue->m[3][column];
            wrapStep = currentValue->m[wrapRow][column] * current->wrapPeriod[index];
            rawDistanceSq += difference * difference;
            negativeDistanceSq += (difference - wrapStep) * (difference - wrapStep);
            positiveDistanceSq += (difference + wrapStep) * (difference + wrapStep);
        }
        wrapDirection = 0.0f;
        if ((negativeDistanceSq < rawDistanceSq) && (negativeDistanceSq <= positiveDistanceSq)) {
            wrapDirection = -1.0f;
        } else if (positiveDistanceSq < rawDistanceSq) {
            wrapDirection = 1.0f;
        }
    } else {
        wrapDirection = 0.0f;
    }
    if (wrapDirection != 0.0f) {
        adjustedCurrent = *currentValue;
        for (column = 0; column < 4; column++) {
            adjustedCurrent.m[3][column] += adjustedCurrent.m[wrapRow][column] *
                                            current->wrapPeriod[index] * wrapDirection;
        }
        currentValue = &adjustedCurrent;
    }
    alpha = presentation->alpha;
    for (row = 0; row < 4; row++) {
        for (column = 0; column < 4; column++) {
            result->m[row][column] = previousValue->m[row][column] +
                                     ((currentValue->m[row][column] - previousValue->m[row][column]) * alpha);
        }
    }
    presentation->matricesInterpolated++;
    return true;
}

void PspFrameInterpolation_FinishPresentation(SPTask* task) {
    PspFrameInterpolationPool* state;
    PspFrameInterpolationPresentation* presentation;
    s32 pool = psp_frame_interpolation_pool_for_task(task);

    sPresentationPool = -1;
    if (pool < 0) {
        return;
    }
    state = &sPools[pool];
    presentation = &state->presentation;
    PspProfiler_RecordInterpolation(presentation->requested, presentation->interpolated, presentation->exact,
                                    presentation->fallback, presentation->missingHistory,
                                    presentation->topologyMismatch,
                                    presentation->requested ? state->worldCount : 0,
                                    presentation->matricesInterpolated,
                                    presentation->repeated ||
                                        (presentation->renderOnly && !presentation->interpolated));
    (void) state;
    (void) presentation;
}
