# PSP Vertex Reuse Diagnostics

This profiling-only diagnostic measures exact, batch-local reuse of final PSPGL
vertex packets. It is enabled with:

```sh
make SF64_PSP_PROFILE_PHASES=1 SF64_PSP_PROFILE_VERTEX_REUSE=1 psp
```

`SF64_PSP_PROFILE_VERTEX_REUSE` defaults to `0`, accepts only `0` or `1`, and
requires `SF64_PSP_PROFILE_PHASES=1`.

## Scope

The diagnostic analyzes two named scopes:

* `all_pspgl_draws`: every non-empty call to
  `PspGfxPspgl_DrawColoredTriangles()`.
* `renderer_batches`: every populated display-list renderer batch in
  `psp_gfx_dl_flush_reason()` before the batch is reset.

These scopes are not disjoint. Renderer batches are submitted through PSPGL, so
they are analyzed once as `renderer_batches` and again as `all_pspgl_draws`.
Compare the scopes; do not add them together.

## Identity Rule

Reuse is exact and conservative. Two vertices count as reusable only when all
24 bytes of their final `PspGfxPspglColorVertex` packets match via `memcmp`.
The diagnostic does not use float tolerances, source vertex indices, positions,
UVs, colours, or canonicalized float values.

The hash table is only an accelerator. Hash matches are verified with `memcmp`,
and table overflow is reported in the output.

## Output

`profile-NNN.txt` includes `[vertex reuse: ...]` sections and invariants:

```text
all_pspgl_draws_analyzed == pspgl_draw_calls
all_pspgl_vertices_analyzed == pspgl_draw_vertices
renderer_batches_analyzed == renderer_batch_flushes
renderer_batch_vertices_analyzed == renderer_vertices_submitted
all_pspgl_table_overflows == 0
renderer_batch_table_overflows == 0
```

The long-form CSV is:

```text
profile-NNN-vertex-reuse.csv
```

It records summary totals, hash diagnostics, origin counts for renderer batches,
histograms, and component attribution when component profiling is enabled.

The byte-savings fields are theoretical only. They model replacing repeated
24-byte final packets inside the existing draw with unique packets plus 16-bit
or 32-bit indices. This task does not implement indexed drawing, change batch
boundaries, or alter PSPGL submission.
