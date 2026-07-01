# PSPGL Command-List Sizing Experiment

This experiment tests PSPGL command-list capacity only. It does not change the
number of command-list slots, GE command ordering, rendering state, texture
handling, buffer pinning, or queue synchronization semantics.

## Current Design

PSPGL uses a ring of 16 command-list slots. The historical capacity is 512 total
32-bit command words per slot. Four of those words are reserved for the FINISH, END,
and two NOP termination commands. With 16 slots, the raw command-buffer storage is:

```text
512 words  x 4 x 16 = 32 KiB
1024 words x 4 x 16 = 64 KiB
2048 words x 4 x 16 = 128 KiB
4096 words x 4 x 16 = 256 KiB
```

Structure fields, pointer storage, and cache-line alignment add a small amount
of overhead beyond the raw command words.

## Hypothesis

Real PSP captures showed the 512-word lists consistently reaching capacity. The
title screen also appears to run at full speed briefly, then settles lower even
though the scene remains visually similar.

One plausible explanation is queue fill and backpressure: PSPGL initially queues
into empty list slots, the CPU runs ahead of the GE, then the 16-slot ring wraps
and normal slot reuse reaches a list that has not been reclaimed yet. At that
point `sceGeListSync()` can make submission wait for GE progress.

This is not proven. Larger lists may reduce rollover and submission overhead,
only lengthen the initial queue-fill burst, shift work into later waits, have no
meaningful effect, or regress due to memory footprint or queue granularity.

## Build Options

Parent repository:

```make
SF64_PSP_PSPGL_DLIST_SIZE_WORDS ?= 512
```

PSPGL submodule:

```make
PSPGL_DLIST_SIZE_WORDS ?= 512
```

The parent option is passed to the local PSPGL sub-make. Values other than 512
require `USE_LOCAL_PSPGL=1`; default 512 still permits normal system-PSPGL
builds. PSPGL records both `PSPGL_PROFILE` and `PSPGL_DLIST_SIZE_WORDS` in its
configuration stamp so changing either option rebuilds the archive.

## Counters

Existing command-list counters keep their meanings:

```text
command_words
command_list_submissions
command_list_rollovers
command_list_high_water_words
command_list_capacity_words
command_list_insert_space_calls
command_list_insert_space_words
command_list_insert_space_rollovers
queue_waits
```

New profiling counters:

```text
command_list_pool_wraps
```

Increments when normal submission advances from slot 15 back to slot 0. This is
ring traversal, not necessarily a stall.

```text
command_list_outstanding_current
command_list_outstanding_high_water
```

Outstanding means a command-list slot has a valid queue ID and has not yet been
reclaimed by PSPGL. It does not prove that the GE is actively executing that
list. The current value may be nonzero when a capture ends.

```text
command_list_reuse_syncs
```

Increments only when normal submission advances to the next ring slot and finds
that slot still queued before calling `sync_list()` to reclaim it.

```text
command_list_reuse_sync_wait_us
command_list_reuse_sync_wait_max_us
```

Measure total and maximum elapsed microseconds around reuse-triggered
`sync_list()` calls only. The duration includes function and syscall overhead;
very small values can mean the slot had already completed by the time PSPGL
checked it. Timer reads compile out when `PSPGL_PROFILE=0`.

## Hardware Test Matrix

Timing builds, with PSPGL profiling disabled:

```text
512 words, profiling OFF
1024 words, profiling OFF
2048 words, profiling OFF
4096 words, profiling OFF
```

Counting builds:

```text
512 words, profiling ON
2048 words, profiling ON
```

Primary scene: title screen. It is visually stable, command volume is high, the
512-word high-water mark reaches capacity, and it shows the brief full-speed
period followed by slower sustained performance. Also smoke-test title-to-map,
Corneria introduction, and opening gameplay.

Compare multiple runs by median frame or graphics-task timing. Use
profiling-enabled builds to verify the mechanical effects: capacity changes,
rollovers and submissions usually fall, high-water can rise above 512, pool
wraps should fall, reuse syncs may move or fall, and `command_words` should stay
nearly unchanged apart from fewer list terminators. Draws, vertices, uploads,
GE state emissions, and visible output should remain unchanged.

## Interpretation

Result A: longer initial burst, unchanged steady speed.

Larger lists increased queue depth, but sustained GE throughput remains the
limit. Treat this as diagnostic rather than a sustained performance win.

Result B: improved steady speed.

Submission, rollover, slot-reuse, or synchronization overhead was materially
limiting performance. Compare CPU-side phase reductions with reuse wait changes
and choose the smallest size that delivers most of the benefit.

Result C: CPU work falls but synchronization rises.

Work moved from command generation into later waiting. Total frame or graphics
task time determines whether the change is useful.

Result D: little change.

Command-list sizing is not a significant bottleneck. Keep 512 and move to the
GE set-bit iteration experiment.

Result E: regression at large sizes.

Memory footprint, reduced queue granularity, latency, or another side effect
outweighs submission savings. Test intermediate sizes and keep the smallest safe
value.

## Risks And Rollback

Larger command lists increase static storage in PSPGL. The 4096-word variant
uses 256 KiB of raw command-buffer storage, before structure overhead and
alignment. Hardware testing must confirm that memory placement, latency, and
steady-state frame timing remain acceptable.

Rollback is to build with:

```make
SF64_PSP_PSPGL_DLIST_SIZE_WORDS=512
```

or use the normal system PSPGL path with `USE_LOCAL_PSPGL=0`.

## Hardware Results

Real PSP testing found that 512, 1024, 2048, and 4096 word command-list
capacities produced effectively identical sustained warm-title graphics-task
timing. The complete spread across the four tested capacities was approximately
0.025%.

The 2048-word configuration eliminated capacity rollovers, but total graphics
time did not improve. The 16-slot command-list ring did not saturate, and no
reuse-triggered waits occurred. Larger lists reduced PSPGL submission work, but
the saved time shifted into other work or later synchronization.

The default remains 512 words. The configurable capacity and command-list
counters remain useful diagnostic infrastructure, but the command-list sizing
experiment is closed as an explanation for the sustained warm-title slowdown.
