# PSP Early Trivial Reject

`SF64_PSP_EARLY_TRIVIAL_REJECT` is an optional renderer experiment for the
PSPGL display-list frontend. It defaults to `0`.

```sh
make SF64_PSP_EARLY_TRIVIAL_REJECT=1 psp
```

The Makefile accepts only `0` or `1`. The option is compiled into the PSP
objects, included in the compile-flags stamp, reported in generated profile
metadata, reconstructed build commands, and `profile-NNN.txt`.

## Motivation

Real PSP trivial-reject captures in Corneria showed a steady population of
triangles with all three vertices outside the same clip plane. Representative
captures saw roughly 129 to 193 trivially rejected triangles per frame. Many of
those triangles still resolved effective state, prepared textures, reused or
changed render state, and occasionally flushed an existing batch before being
discarded.

Every observed reject-triggered flush came from a texture or material change,
which makes this a good PSPGL experiment: invisible texture changes should not
break a visible batch.

## Processing Order

The old single-triangle path validated indices, loaded vertices, computed clip
codes, recorded geometry statistics, then applied material, texture, transform,
depth, and fog batch state before finally rejecting `sharedClipCode != 0`.

With `SF64_PSP_EARLY_TRIVIAL_REJECT=1`, the path now rejects after:

* validating all three indices;
* loading the cached vertices;
* determining projected versus pretransformed mode;
* computing shared and combined clip codes;
* recording geometry-only triangle statistics;
* recording trivial-reject diagnostics that describe the batch before state
  would have been applied;
* preserving depth and fog diagnostic statistics with side-effect-free state
  calculations.

The rejected triangle does not apply effective state, prepare a texture, look up
or upload texture data, mutate batch transform/texture/depth/fog state, flush a
batch, construct vertices, emit vertices, or enter clipping.

## State Semantics

The optimization is safe because a valid triangle with nonzero shared clip code
cannot produce output geometry: all vertices are outside the same clip plane.
Material and texture state for that triangle therefore has no visible result.

State changes are deferred, not deleted globally. If an invisible rejected
texture-B triangle appears between visible texture-A triangles, it no longer
flushes or mutates the A batch. If the next visible triangle needs texture B,
the renderer resolves and applies B at that later visible triangle in normal
display-list order.

## TRI2

The paired TRI2 direct fast path is unchanged. Diagnostics still classify the
ordered TRI2 outcome matrix first, the paired direct path is attempted next, and
fallback still calls the single-triangle path twice in first-then-second order.
This experiment does not add a paired reject path or reorder triangles.

## Batch-State Cache

With `SF64_PSP_BATCH_STATE_CACHE=1`, early rejection happens before
`psp_gfx_dl_apply_effective_batch_state()`, so effective material, depth, and
fog caches are not resolved for rejected triangles.

With `SF64_PSP_BATCH_STATE_CACHE=0`, texture preparation and batch transform,
texture, depth, and fog mutation are behind the early-reject decision when the
experiment is enabled. With the experiment disabled, the legacy cache-disabled
ordering remains the control path.

## Preserved Statistics

Early-rejected triangles preserve geometry/output counters where they describe
the input triangle:

* projected or pretransformed triangle count;
* shared clip, behind-eye, eye-plane-crossing, and degenerate counts;
* clip-rejected triangle count;
* total triangle count;
* profiler triangle result `PspProfiler_CountTriangleResult(0, 1, 0, 0, 0)`;
* depth-test, depth-write, fog-triangle, and fog depth-range diagnostics.

They are not counted as direct, fallback-emitted, clipped, generated, submitted,
or accepted.

## Expected Counter Differences

The candidate build may reduce effective-state resolves and reuses, texture
prepare calls, texture-cache lookups, uploads, texture-change flushes, draw
calls, submitted vertices, and VBO bytes.

For the same workload, structural counters should remain equivalent: display
list commands, TRI1/TRI2 commands, input triangles, invalid triangles,
trivially rejected triangles, partially clipped triangles, generated clipped
triangles, accepted triangles, G_VTX commands, vertices loaded, matrices,
lighting evaluations, component ownership, and mixed-component diagnostics.
Submitted vertices, draw calls, and flushes should not increase.

## A/B Procedure

Build a control and candidate with identical options except
`SF64_PSP_EARLY_TRIVIAL_REJECT`:

```sh
make USE_LOCAL_PSPGL=1 SF64_PSP_EARLY_TRIVIAL_REJECT=0 BUILD_DIR=build/psp-early-reject-default psp
make USE_LOCAL_PSPGL=1 SF64_PSP_EARLY_TRIVIAL_REJECT=1 BUILD_DIR=build/psp-early-reject-candidate psp
```

For structural diagnostics, enable phase/component/trivial-reject profiling and
capture the same route on real PSP hardware:

```sh
make USE_LOCAL_PSPGL=1 SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_COMPONENTS=1 SF64_PSP_PROFILE_TRIVIAL_REJECTS=1 \
     SF64_PSP_TRI2_PAIR_FASTPATH=1 SF64_PSP_TRI2_PAIR_VALIDATE=0 \
     SF64_PSP_PROFILE_CAPTURE_FRAMES=300 PSP_FPS_OVERLAY=0 \
     SF64_PSP_EARLY_TRIVIAL_REJECT=1 BUILD_DIR=build/psp-early-reject-profile-candidate psp
```

Use a matching `SF64_PSP_EARLY_TRIVIAL_REJECT=0` build as the control.

For timing, compare profiling-disabled control and candidate builds on the same
title-screen and Corneria route. The title screen is primarily a regression
check; Corneria is the useful performance scene.

## Correctness Procedure

Check the title screen and Corneria first. Verify that visible texture changes
still occur on the first visible triangle that needs them, that invisible
geometry does not cause batch breaks, and that no new clipping artifacts appear
near the eye plane or frustum edges.

## Adoption Criteria

Keep the option disabled unless real PSP captures show stable correctness and a
measurable gain in the Corneria scenes that contain rejected geometry. A good
candidate should reduce state and texture work for rejected triangles without
increasing draw calls, flushes, submitted vertices, or structural mismatch risk.
