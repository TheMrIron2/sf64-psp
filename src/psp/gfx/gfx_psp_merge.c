#include "src/psp/gfx/gfx_psp_merge.h"

#if PSP_MERGE_ANALYSIS

#include "src/psp/platform.h"

#include <stdio.h>

#define PSP_MERGE_MAX_BATCHES 512
#define PSP_MERGE_RUN_BUCKETS 8

typedef struct {
    u32 key;
    u32 vertexCount;
    u8 barrier;
} PspGfxMergeBatch;

static PspGfxMergeBatch sBatches[PSP_MERGE_MAX_BATCHES];
static u32 sBatchCount;
static u32 sTaskOverflow;

static u32 sTasks;
static u32 sFlushes;
static u32 sPotential;
static u32 sBarriers;
static u32 sRuns;
static u32 sMaxRun;
static u32 sLargestGroup;
static u32 sOverflowTasks;
static u32 sVertices;
static u32 sRunHistogram[PSP_MERGE_RUN_BUCKETS];

void PspGfxMerge_ResetTask(void) {
    sBatchCount = 0;
    sTaskOverflow = 0;
}

void PspGfxMerge_AddBatch(u32 key, u32 vertexCount, int barrier) {
    PspGfxMergeBatch* entry;

    if (sBatchCount >= PSP_MERGE_MAX_BATCHES) {
        sTaskOverflow++;
        return;
    }

    entry = &sBatches[sBatchCount++];
    entry->key = key;
    entry->vertexCount = vertexCount;
    entry->barrier = (u8) (barrier != 0);
}

void PspGfxMerge_AnalyseTask(void) {
    u32 runStart = 0;
    u32 i;

    sTasks++;
    sFlushes += sBatchCount + sTaskOverflow;
    if (sTaskOverflow != 0) {
        sOverflowTasks++;
    }
    // batches past the cap count as unmergeable so the estimate stays conservative
    sPotential += sTaskOverflow;

    for (i = 0; i <= sBatchCount; i++) {
        int atEnd = (i == sBatchCount);
        u32 length;
        u32 distinct = 0;
        u32 bucket = 0;
        u32 a;
        u32 b;

        if (!atEnd && (sBatches[i].barrier == 0)) {
            continue;
        }

        length = i - runStart;
        if (length != 0) {
            for (a = runStart; a < i; a++) {
                u32 group = 1;
                int seen = 0;

                for (b = runStart; b < a; b++) {
                    if (sBatches[b].key == sBatches[a].key) {
                        seen = 1;
                        break;
                    }
                }
                if (seen) {
                    continue;
                }

                distinct++;
                for (b = a + 1; b < i; b++) {
                    if (sBatches[b].key == sBatches[a].key) {
                        group++;
                    }
                }
                if (group > sLargestGroup) {
                    sLargestGroup = group;
                }
            }

            sPotential += distinct;
            sRuns++;
            if (length > sMaxRun) {
                sMaxRun = length;
            }
            while ((bucket < (PSP_MERGE_RUN_BUCKETS - 1)) && (length >= (2u << bucket))) {
                bucket++;
            }
            sRunHistogram[bucket]++;
        }

        if (!atEnd) {
            // a barrier stays where it is and remains its own draw
            sPotential++;
            sBarriers++;
        }
        runStart = i + 1;
    }

    for (i = 0; i < sBatchCount; i++) {
        sVertices += sBatches[i].vertexCount;
    }
}

void PspGfxMerge_GetTotals(PspGfxMergeTotals* out) {
    if (out == NULL) {
        return;
    }
    out->tasks = sTasks;
    out->flushes = sFlushes;
    out->potential = sPotential;
    out->barriers = sBarriers;
    out->runs = sRuns;
    out->maxRun = sMaxRun;
    out->largestGroup = sLargestGroup;
    out->overflowTasks = sOverflowTasks;
    out->vertices = sVertices;
}

void PspGfxMerge_Report(u32 taskIndex) {
    char line[512];
    u32 saved = (sFlushes > sPotential) ? (sFlushes - sPotential) : 0;
    u32 reduction = (sFlushes != 0) ? ((saved * 1000u) / sFlushes) : 0;
    u32 tasks = (sTasks != 0) ? sTasks : 1;
    u32 used;
    u32 i;

    snprintf(line, sizeof(line),
             "[pspgl-merge] task=%lu tasks=%lu flushes=%lu potential=%lu savedPermille=%lu "
             "drawsPerTaskX1000=%lu potentialPerTaskX1000=%lu barriers=%lu runs=%lu maxRun=%lu "
             "largestGroup=%lu vtxPerDrawX1000=%lu vtxPerPotentialDrawX1000=%lu overflowTasks=%lu",
             (unsigned long) taskIndex, (unsigned long) sTasks, (unsigned long) sFlushes,
             (unsigned long) sPotential, (unsigned long) reduction,
             (unsigned long) ((sFlushes * 1000u) / tasks), (unsigned long) ((sPotential * 1000u) / tasks),
             (unsigned long) sBarriers, (unsigned long) sRuns, (unsigned long) sMaxRun,
             (unsigned long) sLargestGroup,
             (unsigned long) ((sFlushes != 0) ? ((sVertices * 1000u) / sFlushes) : 0),
             (unsigned long) ((sPotential != 0) ? ((sVertices * 1000u) / sPotential) : 0),
             (unsigned long) sOverflowTasks);
    PspPlatform_LogLine(line);

    used = (u32) snprintf(line, sizeof(line), "[pspgl-merge-runs] len:count");
    for (i = 0; i < PSP_MERGE_RUN_BUCKETS; i++) {
        used += (u32) snprintf(line + used, sizeof(line) - used, " %lu:%lu", (unsigned long) (1u << i),
                               (unsigned long) sRunHistogram[i]);
    }
    PspPlatform_LogLine(line);
}

#endif
