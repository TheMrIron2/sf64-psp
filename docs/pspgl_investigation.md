# PSPGL Client-Array and Display-List Pressure in Perfect Dark PSP

## Status

Research note for future PSPGL and `n64psp` work.

This document describes a likely PSPGL-level performance problem exposed by the Perfect Dark PSP renderer. It deliberately separates that problem from the SF64 application bug where `glFlush()` was explicitly called after every material batch.

## Reference versions

The initial comparison used:

* Perfect Dark PSP: `z2442/perfect_dark-PSP`
* Perfect Dark reference commit: `0871c907aea105cd2e7002219d047c733011f668`
* PSPGL: `pspdev/pspgl`, current architecture derived from the original PSPGL implementation
* SF64 control capture: `TheMrIron2/sf64-psp` at `a62109bbc082e02a49f9398ac3caa8a50b0dd582`

Relevant Perfect Dark source:

* `port/fast3d/gfx_opengl.cpp`
* function: `gfx_opengl_draw_triangles()`

Relevant PSPGL source:

* `glDrawArrays.c`
* `pspgl_varray_draw.c`
* `pspgl_varray.c`
* `glLockArraysEXT.c`
* `pspgl_dlist.c`
* `pspgl_buffers.c`
* `glBindTexture.c`
* `eglSwapBuffers.c`

## Executive summary

Perfect Dark does **not** appear to call `glFlush()` after every material draw. Its normal triangle path configures client arrays, issues one or more `glDrawArrays()` calls, and eventually presents through `eglSwapBuffers()`.

This means Perfect Dark is not evidence that PSPGL automatically flushes whenever a texture or material changes.

It does, however, expose a broader architectural mismatch:

> PSPGL's ordinary client-array `glDrawArrays()` path can allocate, map, copy or convert, unmap, pin, and later free a temporary vertex buffer for every draw call.

An N64 renderer commonly produces many small batches because texture, combiner, depth, blend, clipping, and matrix state change frequently. On PSPGL, each small batch can therefore become a separate CPU-side vertex repack operation plus additional GE command-list and buffer-lifetime pressure.

The resulting bottleneck may look like poor triangle throughput, but the limiting factor may actually be:

* CPU-side vertex preparation;
* temporary-buffer allocation and reclamation;
* redundant fixed-function state setup;
* command-list submission pressure;
* synchronization caused by the finite display-list ring;
* multi-pass rendering of N64 combiner effects.

This should be measured before attributing the slowdown to the PSP GE's raw rasterization capacity.

## The important distinction from SF64

The SF64 profile exposed an application-level error:

```text
glDrawArrays()
glFlush()
```

for every material batch.

In PSPGL, `glFlush()` closes and enqueues the current GE command list. SF64 was therefore creating hundreds of tiny submitted lists per graphics task.

Perfect Dark's regular triangle path does not show the same explicit per-draw flush. Its problem is more likely located inside or immediately above PSPGL's draw pipeline.

These findings must not be merged into the inaccurate claim that PSPGL flushes on every texture change.

## Perfect Dark draw behaviour

At the reference commit, `gfx_opengl_draw_triangles()` uses a floating-point interleaved client buffer with a nine-float stride. It configures:

* vertex coordinates;
* texture coordinates;
* colour data;
* projection and model-view state;
* culling;
* depth state;
* blending;
* alpha testing;
* texture environment state;
* texture selection.

It then issues `glDrawArrays(GL_TRIANGLES, ...)`.

Some material modes can issue additional passes, including:

* a text-outline pass;
* a second texture pass;
* a fallback two-pass combiner path.

Thus one frontend batch can cause two or more PSPGL draws over the same vertex data.

Perfect Dark also shadows some state itself, such as the most recently bound texture, but still performs substantial fixed-function setup in the draw function.

## PSPGL's ordinary `glDrawArrays()` path

The public `glDrawArrays()` wrapper calls `__pspgl_varray_draw()`.

Unless PSPGL can use a cached or locked array, the draw follows the slow path:

1. Compute the current PSP GE vertex format.
2. Allocate a new `GL_STREAM_DRAW_ARB` internal buffer sized for this draw.
3. Map that buffer for writing.
4. Inspect the enabled client arrays.
5. Copy or convert each enabled attribute into PSP GE interleaved order.
6. Unmap the buffer.
7. Emit the PSP GE vertex pointer, state, and primitive commands.
8. Pin the temporary buffer to the current GE display list.
9. Drop the caller-side reference.
10. Reclaim the buffer only after the GE list using it has completed.

Even when the application's source data is already interleaved, ordinary client memory is still copied into the temporary PSPGL buffer. Native layout avoids per-attribute conversion, but not necessarily the per-draw allocation and copy.

## PSPGL's fast paths

PSPGL can avoid much of this work when:

* the attributes are in PSP GE-native interleaved layout;
* they reside in a suitable PSPGL buffer object; or
* the application explicitly uses `glLockArraysEXT()` and the array remains reusable.

A native interleaved VBO can be referenced directly by the draw path. This is the most relevant route for N64 ports.

`glLockArraysEXT()` can also cache converted data, but it is less natural for continuously generated dynamic geometry and may not fit multi-pass, per-frame N64 workloads as cleanly as a persistent VBO ring.

## PSPGL command-list architecture

PSPGL records PSP GE commands into a small ring of display lists.

In the examined implementation:

* there are 16 command lists;
* each list contains 512 32-bit words;
* space is reserved for `FINISH`, `END`, and padding;
* a list is automatically submitted when it approaches capacity;
* explicit `glFlush()` also submits the current list;
* when the ring wraps to a list still in use, PSPGL waits for that list to complete.

This architecture is reasonable for moderate fixed-function workloads, but it can become stressed by an N64 renderer generating many tiny draws and temporary buffers.

Importantly, `glDrawArrays()` itself does not normally submit the list after every call. It appends commands to the current list. The list is submitted on capacity, explicit flush, finish, or synchronization paths.

## Texture changes are not implicit list flushes

`glBindTexture()` restores the texture object's PSP GE register state and emits a texture-cache-flush command into the current command stream. It does not inherently require a GE list submission.

The following sequence is valid inside one list:

```text
bind texture A
draw
bind texture B
draw
```

Therefore, a high texture-change count can increase state-command traffic and break application batches, but it is not proof of an implicit PSPGL list flush.

## Why N64 renderers are a difficult workload

An N64 display-list renderer tends to have characteristics that amplify PSPGL's overhead:

* small geometry batches;
* frequent texture changes;
* frequent combiner and blend changes;
* dynamic vertices generated every frame;
* software clipping or subdivision;
* two-pass approximations for unsupported combiner modes;
* immediate-mode-style state transitions inherited from the source renderer;
* low triangle counts per API call.

Desktop OpenGL drivers are designed to absorb a large amount of this API traffic. PSPGL is a compact translation layer over a much simpler command processor and does much of the work synchronously on the Allegrex CPU.

The PSP GE may therefore spend less time rasterizing than the CPU spends preparing and feeding it.

## Working hypotheses

The following hypotheses should be tested independently.

### H1: Temporary vertex-buffer work dominates

Every client-array draw allocates or recycles an internal buffer and copies the full vertex payload. Small batches make the fixed cost per draw disproportionately large.

Expected signature:

* high CPU time inside `__pspgl_varray_draw()`, `__pspgl_varray_convert()`, `__pspgl_gen_varray()`, buffer allocation, and buffer mapping;
* cost scales strongly with draw count and vertex bytes copied;
* native VBOs produce a large improvement even with unchanged draw count.

### H2: Display-list ring backpressure contributes

Many draw and state commands fill the 512-word lists quickly. The 16-list ring can wrap while the GE is still consuming earlier work, causing `sceGeListSync()` waits.

Expected signature:

* frequent capacity-triggered list submissions;
* significant PSPGL queue-wait time;
* short average list length if other synchronization paths submit early;
* larger command lists or a larger ring reduce CPU stalls.

### H3: Redundant state traffic is significant

Perfect Dark performs substantial fixed-function setup around each draw. PSPGL caches register values, but public API calls, matrix bookkeeping, texture-object handling, and dirty-state scans still cost CPU time.

Expected signature:

* many calls that do not change effective state;
* reduced CPU time after application-side state shadowing;
* limited change in GE execution time.

### H4: Multi-pass combiner emulation multiplies the cost

Some N64 materials require two or more passes. Each pass repeats PSPGL draw setup and may repeat vertex copying if PSPGL does not reuse the converted array.

Expected signature:

* frames with more two-pass materials show disproportionately high draw-pipeline time;
* drawing multiple passes from one persistent VBO is much cheaper than repeated client-array draws;
* precomposited textures or a specialized native path reduce cost.

### H5: Texture allocation, migration, or eviction contributes

Texture uploads and limited VRAM can trigger PSPGL buffer movement, eviction, compaction, or finish synchronization.

Expected signature:

* spikes correlate with texture uploads or VRAM pressure rather than triangle count;
* `glFinish()` appears in eviction or framebuffer-copy paths;
* persistent texture allocation or reduced texture formats improve the outliers.

### H6: Raw GE throughput is the final limit

After CPU-side and submission costs are reduced, the remaining time may genuinely be rasterization, overdraw, blending, texture filtering, or fill rate.

This must be treated as the final hypothesis, not the first.

## Instrumentation required in PSPGL

Add optional statistics that are cheap enough for real-hardware captures.

### Vertex-array statistics

Record:

* total `glDrawArrays()` and `glDrawElements()` calls;
* fast-path draws;
* slow-path draws;
* native-layout detections;
* locked-array hits;
* VBO-direct draws;
* temporary buffers allocated;
* temporary bytes allocated;
* vertex bytes copied;
* attribute conversions by type;
* time in format analysis;
* time in allocation/map/unmap;
* time in copy/conversion;
* time in render setup.

### Display-list statistics

Record:

* submitted list count;
* submission reason: explicit flush, capacity, finish, buffer synchronization, swap, other;
* command words used per submitted list;
* empty-list submissions;
* ring-wrap count;
* `sceGeListSync()` calls;
* queue-wait microseconds;
* pinned buffers per list;
* buffer-release work per completed list.

### Buffer and VRAM statistics

Record:

* system-memory allocations and frees;
* EDRAM allocations and frees;
* migrations;
* evictions;
* compactions;
* bytes moved;
* synchronization caused by eviction or compaction;
* high-water marks for live temporary vertex buffers.

### State statistics

Record:

* public state calls;
* effective state changes;
* redundant calls;
* texture binds that change the object;
* texture binds to the already bound object;
* matrix uploads;
* texture-cache-flush commands.

## Instrumentation required in Perfect Dark

At the frontend/backend boundary, record:

* batches per frame;
* triangles per batch histogram;
* vertices per draw histogram;
* one-pass, two-pass, and three-pass draw counts;
* texture binds per frame;
* effective texture changes;
* combiner-mode changes;
* blend/depth/alpha changes;
* matrix changes;
* client vertex bytes submitted;
* time in `gfx_opengl_draw_triangles()` excluding PSPGL;
* time inside the PSPGL call itself;
* frame or scene labels for repeatable captures.

The important correlation is not just frame time versus triangle count, but frame time versus:

```text
draw calls
vertex bytes copied
PSPGL slow-path calls
GE list submissions
queue-wait time
number of rendering passes
```

## Controlled experiments

### Experiment 1: Client-array microbenchmark

Build a PSP test that renders the same static triangle data repeatedly while varying:

* triangles per draw;
* number of draws;
* total triangles held constant;
* vertex layout native versus non-native;
* one pass versus two passes.

This separates fixed API cost from per-vertex and per-pixel cost.

### Experiment 2: `glLockArraysEXT()`

Render the same static data using ordinary client arrays and locked arrays.

Measure:

* CPU submission time;
* temporary allocations;
* bytes copied;
* list submissions;
* total frame time.

This determines how much of the bottleneck is specifically repeated conversion/copying.

### Experiment 3: Native interleaved VBO

Upload PSP GE-native vertices into a PSPGL VBO and draw the same ranges repeatedly.

A strong improvement would validate the persistent-buffer direction for both Perfect Dark and SF64.

### Experiment 4: Persistent dynamic VBO ring

Implement a two- or three-buffer ring for frame-generated vertices:

1. map or update the next buffer;
2. write all frame or task geometry in native order;
3. issue draw ranges by offset;
4. do not overwrite a buffer still used by the GE;
5. rotate each frame or after a fence/synchronization point.

Compare against one temporary PSPGL buffer per draw.

### Experiment 5: Larger PSPGL command lists

Test larger values than 512 words and, separately, more than 16 lists.

Record:

* capacity submissions;
* average list occupancy;
* queue waits;
* memory cost;
* total frame time.

Do not interpret a gain as proof that list size was the only problem; it may simply hide application draw overhead.

### Experiment 6: State-shadowed backend

Avoid redundant public GL calls when the effective state has not changed.

Test texture, blend, alpha, depth, cull, client-array, pointer, texture-environment, and matrix state independently.

### Experiment 7: Native GU control path

Render a representative batch through a minimal native GU/GE path using the same vertices, textures, and render state.

This establishes the lower-level cost of the hardware work without PSPGL's client-array and object-management overhead.

### Experiment 8: Fill-rate control

Run the same draw stream with:

* tiny off-screen or scissored geometry;
* full-screen geometry;
* texturing disabled;
* blending disabled;
* nearest and linear filtering;
* reduced overdraw.

This separates frontend/CPU cost from raster and fill cost.

## Candidate application-level fixes

These do not require changing PSPGL itself.

### Native interleaved VBO ring

Use the PSP GE-native attribute order and a persistent dynamic VBO ring. This is the leading candidate.

Benefits:

* avoids per-draw vertex conversion;
* can avoid per-draw allocation;
* allows multiple passes to reuse identical vertex storage;
* provides stable lifetime until the GE completes;
* maps naturally to `n64psp` reusable infrastructure.

### Draw coalescing

Merge adjacent batches only when all effective state is identical and ordering remains correct.

Do not sort translucent geometry or otherwise violate N64 display-list order.

### State shadowing

Track effective PSPGL state and avoid redundant public calls.

Perfect Dark already shadows some texture state; this can be extended systematically.

### Multi-pass reuse

When a material needs multiple passes, draw both passes from the same VBO range rather than resubmitting client arrays.

### Specialized 2D and text paths

Sprites, text, rectangles, and UI may benefit from native GU sprite primitives or a separate compact vertex format.

## Candidate PSPGL-level fixes

### Stream-buffer arena or ring

Replace per-draw heap-style `GL_STREAM_DRAW_ARB` buffer creation with a persistent internal stream arena.

Desired behaviour:

* suballocate aligned ranges;
* associate ranges with display-list completion;
* reclaim ranges in order;
* avoid general allocation and list manipulation per draw;
* preserve client-array lifetime guarantees.

### Reuse converted arrays across passes

If the source pointers, enabled arrays, format, and generation are unchanged, allow repeated draws to reuse the same converted internal buffer for the duration of a frame or explicit lock scope.

### Improve native client-array handling

Investigate whether truly native, suitably aligned, uncached client data can be referenced directly under an explicit lifetime contract or extension. Standard OpenGL client-array semantics must not be weakened silently.

### Larger or adaptive display lists

Increase list capacity or make it configurable. An adaptive approach could use larger lists for high-draw workloads while preserving bounded memory use.

### Empty-submit guard

Do not enqueue a display list that contains no useful commands before `FINISH` and `END`.

### Better statistics

Promote the proposed counters into a reusable PSPGL diagnostics extension so applications can identify slow-path draws and queue pressure without maintaining a private fork.

## `n64psp` integration strategy

Reusable infrastructure should live in `TheMrIron2/n64psp`, while game-specific renderer policy remains in each port.

Appropriate `n64psp` components include:

* PSP-native interleaved vertex definitions;
* aligned dynamic VBO or GU vertex-ring allocator;
* frame/task lifetime management;
* draw-submission counters;
* GE list and queue-wait profiling helpers;
* portable scalar tests for packing and layout;
* PSP hardware microbenchmarks;
* optional PSPGL and native-GU backends over the same upload API.

Perfect Dark- or SF64-specific combiner selection, texture policy, clipping, and draw ordering should stay in their respective renderers unless a genuinely reusable abstraction emerges.

## Success criteria

A proposed solution should be evaluated using all of the following:

* total graphics-task time;
* CPU submission time;
* GE queue-wait time;
* draw calls per frame;
* triangles per draw;
* temporary allocations per frame;
* vertex bytes copied per frame;
* display-list submissions per frame;
* average command-list occupancy;
* visual correctness;
* stability on real PSP hardware.

The central success condition is:

> Reduce the cost per small N64 material batch without changing display-list ordering or rendering semantics.

A lower `glDrawArrays()` profile percentage alone is insufficient if the cost merely moves into swap synchronization or causes the GE to starve.

## What is currently known

* Perfect Dark uses PSPGL client-array draws.
* It can issue multiple passes for a single frontend batch.
* Its normal triangle path does not explicitly call `glFlush()` after each draw.
* PSPGL's uncached client-array path creates and fills an internal stream buffer per draw.
* PSPGL uses a finite ring of small GE display lists.
* Texture binds update state within the current list rather than inherently submitting it.
* A native interleaved VBO is available as a PSPGL fast path.

## What remains unproven

* which PSPGL substage dominates Perfect Dark on real hardware;
* how often Perfect Dark reaches the native or locked-array fast path;
* actual list occupancy and capacity-submit frequency;
* actual queue-wait time;
* the contribution of texture migration and VRAM pressure;
* the gain from a persistent VBO ring;
* the point at which raw GE fill or raster throughput becomes dominant.

Until those measurements exist, the correct description is:

> Perfect Dark exposes a probable PSPGL client-array and submission-pressure bottleneck, not a proven implicit flush on every material change.
