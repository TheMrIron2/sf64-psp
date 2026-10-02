#include "global.h"
#include "src/psp/frame_interpolation.h"
#include "src/psp/profiler.h"
#include "src/psp/renderer_starfield.h"

#define PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY 0x480
#define PSP_FRAME_INTERPOLATION_BUCKETS 256

typedef struct {
    u32 requested;
    u32 interpolated;
    u32 exact;
    u32 fallback;
    u32 missingHistory;
    u32 topologyMismatch;
    u32 matricesInterpolated;
    u32 repeatFallback;
    u32 repeated;
    f32 alpha;
} PspFrameInterpolationPresentation;

typedef struct {
    u32 simulationVi;
    u32 generation;
    s32 gameState;
    s32 drawMode;
    s32 level;
    u16 worldEnd;
    u16 worldCount;
    u8 simulationVIs;
    u8 valid;
    u8 eligible;
    u8 flags[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    u8 world[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    u8 view[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    u8 wrapAxis[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    f32 wrapPeriod[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    const void* drawSite[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    const void* identity[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    PspFrameInterpolationPresentation presentation;
} PspFrameInterpolationPool;

typedef struct {
    s16 nextGroup;
    s16 nextMatrix;
    s16 remaining;
} PspFrameInterpolationMatch;

static PspFrameInterpolationPool sPools[2];
static s16 sPreviousMatrix[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
static u32 sGeneration;
static s32 sRecordingPool = -1;
static s32 sPresentationPool = -1;
static s32 sWorldScope;
static const void* sMatrixIdentity;
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
    sMatrixIdentity = NULL;
    sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
    sWrapPeriod = 0.0f;
    PspStarfield_Reset();
}

void PspFrameInterpolation_BeginSimulationFrame(SPTask* task, u32 simulationVi, u8 simulationVIs, s32 record) {
    s32 pool = psp_frame_interpolation_pool_for_task(task);
    PspFrameInterpolationPool* state;

    sRecordingPool = -1;
    sWorldScope = false;
    sMatrixIdentity = NULL;
    sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
    sWrapPeriod = 0.0f;
    PspStarfield_BeginSimulationFrame(task);
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

void PspFrameInterpolation_SetMatrixIdentity(const void* identity) {
    sMatrixIdentity = identity;
}

void PspFrameInterpolation_ForgetIdentity(const void* identity) {
    PspFrameInterpolationPool* previous;
    u32 i;

    if (sRecordingPool < 0) {
        return;
    }
    previous = &sPools[sRecordingPool ^ 1];
    // The scheduler protects both history pools while an interpolated task is in flight
    for (i = 0; previous->valid && (i < previous->worldEnd); i++) {
        if (previous->identity[i] == identity) {
            previous->world[i] = false;
        }
    }
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
    state->view[index] = (gPlayerNum << 1) | (gReflectY < 0);
    state->drawSite[index] = drawSite;
    state->identity[index] = world ? sMatrixIdentity : NULL;
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
        sPools[pool].gameState = gGameState;
        sPools[pool].drawMode = gDrawMode;
        sPools[pool].level = gCurrentLevel;
        sPools[pool].valid = true;
        sPools[pool].eligible = (eligible != 0) && (sPools[pool].worldCount != 0);
    }
    sRecordingPool = -1;
    sWorldScope = false;
    sMatrixIdentity = NULL;
    sWrapAxis = PSP_FRAME_INTERPOLATION_WRAP_NONE;
    sWrapPeriod = 0.0f;
}

static u32 psp_frame_interpolation_bucket(const PspFrameInterpolationPool* state, u32 index) {
    return (((uintptr_t) state->identity[index] >> 4) ^ ((uintptr_t) state->drawSite[index] >> 2) ^
            state->view[index]) & (PSP_FRAME_INTERPOLATION_BUCKETS - 1);
}

static s32 psp_frame_interpolation_find_group(const PspFrameInterpolationPool* current, u32 index,
                                             const PspFrameInterpolationPool* previous,
                                             const PspFrameInterpolationMatch* matches, s32 group) {
    while (group >= 0) {
        if ((current->identity[index] == previous->identity[group]) &&
            (current->drawSite[index] == previous->drawSite[group]) &&
            (current->view[index] == previous->view[group])) {
            break;
        }
        group = matches[group].nextGroup;
    }
    return group;
}

static void psp_frame_interpolation_match_matrices(PspFrameInterpolationPool* current,
                                                  const PspFrameInterpolationPool* previous) {
    PspFrameInterpolationMatch matches[PSP_FRAME_INTERPOLATION_MATRIX_CAPACITY];
    s16 buckets[PSP_FRAME_INTERPOLATION_BUCKETS];
    s32 i;
    s32 group;
    s32 old;
    u32 bucket;

    for (i = 0; i < PSP_FRAME_INTERPOLATION_BUCKETS; i++) {
        buckets[i] = -1;
    }
    // Build ordered matrix chains for each object and draw site within a view
    for (i = previous->worldEnd - 1; i >= 0; i--) {
        if (!previous->world[i]) {
            continue;
        }
        bucket = psp_frame_interpolation_bucket(previous, i);
        group = psp_frame_interpolation_find_group(previous, i, previous, matches, buckets[bucket]);
        if (group < 0) {
            group = i;
            matches[group].nextGroup = buckets[bucket];
            matches[group].remaining = -1;
            buckets[bucket] = group;
        }
        matches[i].nextMatrix = matches[group].remaining;
        matches[group].remaining = i;
    }
    for (i = 0; i < current->worldEnd; i++) {
        sPreviousMatrix[i] = -1;
        if (!current->world[i]) {
            continue;
        }
        bucket = psp_frame_interpolation_bucket(current, i);
        group = psp_frame_interpolation_find_group(current, i, previous, matches, buckets[bucket]);
        if (group < 0) {
            current->presentation.topologyMismatch = 1;
            continue;
        }
        old = matches[group].remaining;
        if ((old < 0) || (current->flags[i] != previous->flags[old]) ||
            (current->wrapAxis[i] != previous->wrapAxis[old]) ||
            (current->wrapPeriod[i] != previous->wrapPeriod[old])) {
            matches[group].remaining = -2;
            current->presentation.topologyMismatch = 1;
            continue;
        }
        sPreviousMatrix[i] = old;
        matches[group].remaining = matches[old].nextMatrix;
    }
    // A changed repeat count makes limb or repeated draw correspondence ambiguous
    for (i = 0; i < current->worldEnd; i++) {
        if (sPreviousMatrix[i] < 0) {
            continue;
        }
        bucket = psp_frame_interpolation_bucket(current, i);
        group = psp_frame_interpolation_find_group(current, i, previous, matches, buckets[bucket]);
        if (matches[group].remaining != -1) {
            sPreviousMatrix[i] = -1;
            current->presentation.topologyMismatch = 1;
        }
    }
}

SPTask* PspFrameInterpolation_PreparePresentation(SPTask* task, u32 presentationVi, u8 presentationVIs) {
    PspFrameInterpolationPool* current;
    PspFrameInterpolationPool* previous;
    PspFrameInterpolationPresentation* presentation;
    u32 span;
    u32 targetVi;
    u32 offset;
    s32 pool = psp_frame_interpolation_pool_for_task(task);

    sPresentationPool = -1;
    PspStarfield_PreparePresentation(task, NULL, 1.0f);
    if (pool < 0) {
        return NULL;
    }

    current = &sPools[pool];
    previous = &sPools[pool ^ 1];
    presentation = &current->presentation;
    psp_frame_interpolation_clear_presentation(presentation);
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
    if (!previous->valid || !previous->eligible || (current->generation != (previous->generation + 1)) ||
        (current->gameState != previous->gameState) || (current->drawMode != previous->drawMode) ||
        (current->level != previous->level)) {
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
    psp_frame_interpolation_match_matrices(current, previous);
    presentation->interpolated = 1;
    presentation->alpha = (f32) offset / (f32) span;
    sPresentationPool = pool;
    PspStarfield_PreparePresentation(task, &gGfxPools[pool ^ 1].task, presentation->alpha);
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
        (current->flags[index] != (u8) flags) || (sPreviousMatrix[index] < 0)) {
        return false;
    }

    currentValue = (const Matrix*) currentMatrix;
    previousValue = (const Matrix*) &gGfxPools[pool ^ 1].mtx[sPreviousMatrix[index]];
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
                                    presentation->repeated);
    (void) state;
    (void) presentation;
}
