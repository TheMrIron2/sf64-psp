#ifndef PSP_GFX_PSP_MERGE_H
#define PSP_GFX_PSP_MERGE_H

#include "PR/ultratypes.h"

/* Draw-merge potential analysis, see docs/psp_counter_findings.md
 * Counts how many draws would remain if same material geometry were drawn
 * together, within runs of batches that may safely be reordered
 * Diagnostic only, nothing here changes what is rendered */

#ifndef PSP_MERGE_ANALYSIS
#define PSP_MERGE_ANALYSIS 0
#endif

#if PSP_MERGE_ANALYSIS

void PspGfxMerge_ResetTask(void);
void PspGfxMerge_AddBatch(u32 key, u32 vertexCount, int barrier);
void PspGfxMerge_AnalyseTask(void);
void PspGfxMerge_Report(u32 taskIndex);

/* test seam, lets the host harness read the accumulated totals */
typedef struct {
    u32 tasks;
    u32 flushes;
    u32 potential;
    u32 barriers;
    u32 runs;
    u32 maxRun;
    u32 largestGroup;
    u32 overflowTasks;
    u32 vertices;
} PspGfxMergeTotals;

void PspGfxMerge_GetTotals(PspGfxMergeTotals* out);

#endif

#endif
