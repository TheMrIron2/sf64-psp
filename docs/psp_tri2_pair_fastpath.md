# PSP TRI2 Pair Fast Path Experiment

This document describes the optional paired direct fast path for F3DEX `TRI2`
commands in the PSPGL-era display-list renderer. The option is an experiment and
is disabled by default until real PSP A/B captures justify enabling it.

## Motivation

A stable 300-frame title-screen component capture on real PSP found graphics
task time dominated by display-list traversal and triangle/batch construction:

```text
graphics task:                  ~83.492 ms/frame
display-list traversal:         ~79.277 ms/frame
triangle processing:            ~49.581 ms/frame inclusive
batch construction:             ~42.669 ms/frame inclusive
G_VTX processing:               ~15.509 ms/frame inclusive
```

The same capture showed a stable workload:

```text
TRI2 commands:                  997/frame
TRI1 commands:                   99/frame
input triangles:              2093/frame
direct-fast-path triangles:   2091.55/frame
clipped triangles:               1.45/frame
```

So `TRI2` carries about 95.27% of title triangles, and about 99.93% of triangles
already use the direct triangle path. The four title characters and Arwing
accounted for about 86.5% of component-scoped graphics task time. Raw capture
files are not committed.

## Current TRI2 Flow

`psp_gfx_dl_run_internal()` decodes `TRI2`, calls
`PspProfiler_CountTriangleCommand(2, 0, 1)`, begins the outer
`PSP_PROFILE_PHASE_TRIANGLE` scope, then calls `psp_gfx_dl_emit_tri()` once for
each triangle in command order.

Each `psp_gfx_dl_emit_tri()` call begins `PSP_PROFILE_PHASE_BATCH_CONSTRUCTION`,
validates three vertices, classifies pretransformed versus projected output,
checks clip codes and triangle statistics, applies effective batch state, counts
depth/fog/material statistics, and then emits through the direct, general, or
clipping path. For the title workload, almost every triangle reaches the direct
path, so a `TRI2` commonly repeats effective-state application, batch
construction timing, direct texture-scale calculation, and capacity checks twice.

## Build Options

```make
SF64_PSP_TRI2_PAIR_FASTPATH ?= 0
SF64_PSP_TRI2_PAIR_VALIDATE ?= 0
```

Validation requires both the paired fast path and phase profiling:

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_COMPONENTS=1 \
     SF64_PSP_TRI2_PAIR_FASTPATH=1 \
     SF64_PSP_TRI2_PAIR_VALIDATE=1 \
     SF64_PSP_PROFILE_CAPTURE_FRAMES=300 \
     PSP_FPS_OVERLAY=0 \
     BUILD_DIR=build/psp-tri2-validate \
     psp
```

Invalid validation combinations fail at Makefile parse time. Both options are
compiler definitions, are captured by the compile-flags stamp, and are recorded
in profile build metadata, reconstructed build commands, and `profile-NNN.txt`.

## Eligibility

The paired helper returns nonzero only when it fully handles both triangles. Its
eligibility probe is side-effect-free: before acceptance it reads only vertex
validity, clip codes, projection serials, transform mode, and texture enable
state. It does not mutate renderer state, counters, batch vertices, or texture
state.

Fallback classification is deterministic:

```text
invalid vertex
clipped or rejected
transform/projection incompatibility
direct path not conservatively guaranteed
```

The pair is accepted only when all six vertex indices are valid, both triangles
have zero combined clip code, both triangles have the same pretransformed mode,
projected triangles share a nonzero projection serial, and direct emission is
guaranteed without speculative texture resolution. That final rule means either
texturing is disabled or the shared mode is projected. Pretransformed textured
pairs fall back even if the existing single-triangle path might later resolve a
zero texture ID.

Every fallback executes the original two `psp_gfx_dl_emit_tri()` calls.

## Paired Emission

Accepted pairs begin one `PSP_PROFILE_PHASE_BATCH_CONSTRUCTION` scope, apply
effective batch state once using the first triangle's first vertex, and preserve
the existing texture preparation and state resolution semantics. The helper then
checks capacity once:

```text
ctx->batchCount + 6 > PSP_GFX_DL_BATCH_VERTICES
```

If needed, it flushes once with the existing buffer-full reason before writing
any paired vertices. It computes direct UV scales once, then appends six
non-indexed vertices in original command order:

```text
triangle 0: a0, b0, c0
triangle 1: a1, b1, c1
```

It does not deduplicate shared indices, does not emit indexed geometry, and does
not reorder winding, batches, draw calls, or floating-point calculations. The
unchecked direct-vertex writer assumes capacity was reserved and uses the same
position, perspective divide, color, premultiplication, and UV arithmetic as the
existing direct vertex writer. Component batch ownership is marked for emitted
vertices before submission.

## Statistics

Accepted pairs reproduce the logical statistics of two successful direct
`psp_gfx_dl_emit_tri()` calls: projected/pretransformed counts, behind-eye and
eye-plane-crossing counts, degenerate counts, depth-test/write counts, fog
triangle and fog-depth range updates across all six source vertices, two
accepted input triangles, two output triangles, two direct-fast-path triangles,
six direct vertices written, two total triangles, and textured/alpha/blend
triangle counts when applicable. The paired path produces no clipping counters.

The intentional difference is effective-state reuse accounting. A pair performs
one state application instead of two, so the redundant second reuse is not
preserved. `tri2_pair_state_applications_saved` records one saved application
per hit.

## Instrumentation

Aggregate and component-local counters are emitted for:

```text
tri2_pair_attempts
tri2_pair_fastpath_hits
tri2_pair_fastpath_triangles
tri2_pair_fallback_invalid_vertex
tri2_pair_fallback_clipped_or_rejected
tri2_pair_fallback_transform_mismatch
tri2_pair_fallback_direct_ineligible
tri2_pair_buffer_preflushes
tri2_pair_state_applications_saved
tri2_pair_vertices_emitted
tri2_pair_validation_mismatches
```

Expected invariants:

```text
attempts == hits + all fallback classes
fastpath_triangles == hits * 2
state_applications_saved == hits
vertices_emitted == hits * 6
```

Aggregate counters appear in `profile-NNN.txt`; component-local counters appear
in `profile-NNN-components.csv`.

## Validation Mode

With `SF64_PSP_TRI2_PAIR_VALIDATE=1`, accepted pairs build six reference
`PspGfxPspglColorVertex` values in fixed local storage, emit the paired vertices
to the real batch, and compare the six appended vertices against the reference.
Color, UV, XYZ, output ordering, and a batch-count delta of exactly six are
checked exactly because the arithmetic is shared. Validation increments
`tri2_pair_validation_mismatches` on any mismatch and stores compact metadata
for the first mismatch in the final text report. It performs no file I/O during
capture and never replaces rendered output.

## Hardware A/B Procedure

Structural control:

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PSPGL_DLIST_SIZE_WORDS=512 \
     SF64_PSP_PSPGL_PROFILE=0 \
     SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_COMPONENTS=1 \
     SF64_PSP_PROFILE_FRAME_TRACE=0 \
     SF64_PSP_TRI2_PAIR_FASTPATH=0 \
     SF64_PSP_TRI2_PAIR_VALIDATE=0 \
     SF64_PSP_PROFILE_CAPTURE_FRAMES=300 \
     PSP_FPS_OVERLAY=0 \
     BUILD_DIR=build/psp-tri2-profile-off \
     psp
```

Structural candidate:

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PSPGL_DLIST_SIZE_WORDS=512 \
     SF64_PSP_PSPGL_PROFILE=0 \
     SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_COMPONENTS=1 \
     SF64_PSP_PROFILE_FRAME_TRACE=0 \
     SF64_PSP_TRI2_PAIR_FASTPATH=1 \
     SF64_PSP_TRI2_PAIR_VALIDATE=1 \
     SF64_PSP_PROFILE_CAPTURE_FRAMES=300 \
     PSP_FPS_OVERLAY=0 \
     BUILD_DIR=build/psp-tri2-profile-on \
     psp
```

For each build, launch fresh, wait 8 to 10 seconds on the stable title screen,
capture 300 frames without input, and confirm the rendering is visually
identical. Workload counters should match except for the documented
effective-state reuse and paired-path counters. Validation mismatches must be
zero.

Profiling-disabled timing builds:

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PROFILE_PHASES=0 \
     SF64_PSP_PROFILE_COMPONENTS=0 \
     SF64_PSP_PSPGL_PROFILE=0 \
     SF64_PSP_TRI2_PAIR_FASTPATH=0 \
     SF64_PSP_TRI2_PAIR_VALIDATE=0 \
     PSP_FPS_OVERLAY=1 \
     BUILD_DIR=build/psp-tri2-timing-off \
     psp
```

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PROFILE_PHASES=0 \
     SF64_PSP_PROFILE_COMPONENTS=0 \
     SF64_PSP_PSPGL_PROFILE=0 \
     SF64_PSP_TRI2_PAIR_FASTPATH=1 \
     SF64_PSP_TRI2_PAIR_VALIDATE=0 \
     PSP_FPS_OVERLAY=1 \
     BUILD_DIR=build/psp-tri2-timing-on \
     psp
```

Take at least three stable-title readings for each build, alternating builds
where practical. Do not adopt based only on instrumented timings.

## Adoption Criteria

Leave the default disabled until hardware shows zero validation mismatches,
visually identical output, exact workload/counter agreement apart from the
documented state-reuse change, no increase in mixed batches, flushes, or draw
calls, repeatable profiling-disabled graphics-time improvement, and no
meaningful regression in another gameplay scene.
