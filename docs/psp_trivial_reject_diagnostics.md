# PSP Trivial Reject Diagnostics

This diagnostic measures how much current PSPGL renderer work is spent on
triangles that are already known to be trivially outside the frustum.

It is enabled with:

```sh
make SF64_PSP_PROFILE_PHASES=1 SF64_PSP_PROFILE_TRIVIAL_REJECTS=1 psp
```

`SF64_PSP_PROFILE_TRIVIAL_REJECTS` defaults to `0` and requires
`SF64_PSP_PROFILE_PHASES=1`. When disabled, the TRI1/TRI2 hot path has no
trivial-reject diagnostic classification or counter calls, `PspGfxDlContext`
has no diagnostic scope field, and the extra output strings are not compiled
into the ELF.

## Motivation

Real PSP validation of the paired TRI2 direct path showed that the title screen
is almost entirely direct-renderable: about 99.9% of title TRI2 commands take
the paired path. Corneria is different. Across 900 sampled gameplay frames,
there were 324,273 TRI2 pair attempts, 229,028 paired-path hits, and 95,245
fallbacks. The observed fallbacks were clipped or rejected, with zero invalid
vertex fallbacks, transform mismatches, direct-ineligible fallbacks, or
validation mismatches.

Dense Corneria samples include roughly 180-205 trivially rejected triangles per
frame. That makes Corneria the useful test scene for this diagnostic. The title
screen is useful for validating the paired fast path, but it is not a good test
for early trivial rejection because it has very few rejected triangles.

## Current Order

The single-triangle path validates indices, reads cached vertices, computes
clip codes, records triangle statistics, applies effective batch/material
state, and only then reaches the final `sharedClipCode != 0` reject branch.

This diagnostic does not move that branch. It observes the current work done
between detecting `sharedClipCode != 0` and reaching the existing reject.

## TRI2 Matrix

Before the paired TRI2 fast path is attempted, both triangles are classified
from existing cached vertex data:

* `DIRECT`: all indices valid and combined clip code is zero.
* `TRIVIAL_REJECT`: all indices valid and shared clip code is nonzero.
* `PARTIAL_CLIP`: all indices valid, shared clip code is zero, and combined
  clip code is nonzero.
* `INVALID`: at least one referenced vertex is invalid.

The profiler records the ordered 4x4 matrix:

```text
first triangle outcome x second triangle outcome
```

So `direct_trivial_reject` and `trivial_reject_direct` are distinct. The sum of
all matrix cells must equal the TRI2 command count observed by the diagnostic.

## Cost Counters

For each valid triangle with `sharedClipCode != 0`, the diagnostic scopes only
the existing state/texture work that happens before the final reject. It counts:

* effective-state calls, resolves, and reuses;
* whether the batch was empty before state application and the number of
  vertices already in the batch;
* texture preparation calls, cache hits, misses, decodes, uploads, and uploaded
  bytes caused while scoped;
* flushes and flushed vertices caused while scoped, split by existing flush
  reason;
* actual state-transition fields detected by the existing transition
  comparisons;
* the resolved render-state classification: textured, untextured, alpha test,
  blend, depth test, depth write, and fog.

Scope counters record begins, ends, invalid nesting, and leaks. A leaked scope
is diagnosed and cleared at task end rather than carried into the next task.

## Output

`profile-NNN.txt` includes build metadata, summary sections, and invariants.
The long-form file:

```text
profile-NNN-trivial-rejects.csv
```

uses:

```text
component_id,component_name,category,name,count
```

Categories are `tri2_outcome_matrix`, `trivial_reject_cost`,
`trivial_reject_flush_reason`, `trivial_reject_state_transition`, and
`trivial_reject_render_state`. Every defined name is emitted, including zero
counts. With component profiling enabled, entries are attributed to the current
command-stream component; gameplay without markers normally appears as
`unattributed`.

## Invariants

Useful checks after capture:

```text
sum(TRI2 outcome matrix) == tri2_commands
effective_state_calls == effective_state_resolves + effective_state_reuses
batch_empty_before_state + batch_nonempty_before_state == trivial_reject_triangles
sum(trivial reject flush reasons) == trivial_reject_flushes
scope_begins == scope_ends
scope_invalid_nesting == 0
scope_leaks == 0
```

## Capture Procedure

Build a phase profiling EBOOT with component profiling if desired:

```sh
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_COMPONENTS=1 \
     SF64_PSP_PROFILE_TRIVIAL_REJECTS=1 \
     SF64_PSP_TRI2_PAIR_FASTPATH=1 \
     SF64_PSP_TRI2_PAIR_VALIDATE=0 \
     PSP_FPS_OVERLAY=0 \
     BUILD_DIR=build/psp-trivial-reject-on \
     psp
```

On hardware, start capture with `SELECT + L`, run the target scene, then stop
with `SELECT + R`. Compare the diagnostic-on build against a matching
`SF64_PSP_PROFILE_TRIVIAL_REJECTS=0` build to keep profiler overhead in view.

## Decision Criteria

A later early-reject experiment is worth trying only if real PSP captures show
that trivially rejected triangles regularly cause meaningful state work:
effective-state resolves or reuses, texture preparation, batch flushes, or
state transitions. If rejected triangles mostly see empty batches, reused state,
and no texture or flush work, early rejection is unlikely to be a useful PSPGL
optimization target.
