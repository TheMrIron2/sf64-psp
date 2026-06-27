# PSP Render Component Profiling

This diagnostic attributes PSP renderer work to regions in the generated N64
display list. It is intended for sustained title-screen analysis first, and is
generic enough for later scene investigations.

## Build Option

Component profiling is disabled by default:

```make
SF64_PSP_PROFILE_COMPONENTS ?= 0
```

It requires `SF64_PSP_PROFILE_PHASES=1` and does not require frame tracing. The
Makefile records the option in profile metadata and in the reconstructed build
command. Flipping the option changes compiler definitions, so affected objects
rebuild through the existing compile-flags stamp.

Control build:

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PSPGL_DLIST_SIZE_WORDS=512 \
     SF64_PSP_PSPGL_PROFILE=0 \
     SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_FRAME_TRACE=0 \
     SF64_PSP_PROFILE_COMPONENTS=0 \
     PSP_FPS_OVERLAY=0 \
     BUILD_DIR=build/psp-title-components-off \
     psp
```

Hardware capture build:

```bash
make USE_LOCAL_PSPGL=1 \
     SF64_PSP_PSPGL_DLIST_SIZE_WORDS=512 \
     SF64_PSP_PSPGL_PROFILE=0 \
     SF64_PSP_PROFILE_PHASES=1 \
     SF64_PSP_PROFILE_FRAME_TRACE=0 \
     SF64_PSP_PROFILE_COMPONENTS=1 \
     SF64_PSP_PROFILE_CAPTURE_FRAMES=300 \
     PSP_FPS_OVERLAY=0 \
     BUILD_DIR=build/psp-title-components \
     psp
```

## Marker Encoding

Markers are emitted as existing `G_NOOP` commands with a reserved tag payload:

```text
w0 opcode: G_NOOP
w1 tag:    0x50524300 | component_id
```

`0x50524300` is the `PRC` signature in the high 24 bits. The low byte is the
bounded component ID. The PSP display-list interpreter recognizes this exact
signature before ordinary no-op handling, changes the active component, and
continues traversal. Nonmatching `G_NOOP` and `G_SPNOOP` commands keep their
existing behaviour.

When disabled, `PSP_PROFILE_DL_COMPONENT(pkt_expr, component)` expands to a
no-op and does not evaluate `pkt_expr`, so calls such as
`PSP_PROFILE_DL_COMPONENT(gMasterDisp++, ...)` do not advance the display-list
pointer. When enabled, the packet expression is evaluated once and exactly one
tagged no-op command is emitted.

## Interpreter Semantics

The main consumer is the PSP display-list traversal in
`psp_gfx_dl_run_internal()`. Marker commands are consumed before
`PspProfiler_CountOpcode()`, before command-count accounting, and before normal
no-op handling, so markers are not treated as rendered commands and never reach
PSPGL or the GE.

Nested display-list calls do not push or restore component state. A marker
opens a command-stream region, and that component remains active through nested
model, limb, and material display lists until another marker changes it.

Task lifecycle is owned by the outer PSPGL render task in
`PspRenderer_RenderGfxTask()`:

```text
task begin -> UNATTRIBUTED, start component timer
frame setup, starfield, and pre-display-list renderer work -> UNATTRIBUTED
marker     -> close previous timed region, switch component
final PSPGL flush and finish/sync -> the active stream component, normally UNATTRIBUTED after title markers
task end   -> close current region, reset to UNATTRIBUTED
```

Invalid component IDs are counted and rejected before indexing the fixed arrays.

## Deferred Batch Ownership

Command interpretation, TnL work, clipping, texture preparation, and state
resolution follow the active command-stream marker. Deferred batch submission is
different: vertices can be appended under one component and flushed later after
another marker has become active.

Component builds therefore track a per-batch ownership mask. Every path that
appends vertices to the renderer batch ORs in the current command-stream
component:

```text
direct triangle output
general/textured triangle output
clipping-generated output
texture rectangles
```

The ownership mask is cleared whenever the batch is emptied. A batch flush is
attributed as follows:

```text
no owner bits      -> UNATTRIBUTED
one owner bit      -> that component
multiple owner bits -> MIXED_BATCH
```

The renderer does not flush at marker boundaries. Mixed batches are not guessed
or proportionally divided; they are reported as `mixed_batch`, with
participation counters for the components that contributed vertices.

During the actual call to `PspGfxPspgl_DrawColoredTriangles()` and its nested
PSPGL phases, the profiler temporarily scopes attribution to the resolved batch
owner. The scope closes the command-stream component region, opens the batch
owner region for the submission work, then restores the previous
command-stream component from the same timer boundary. Invalid nested scope use
is diagnosed and ignored.

Batch-state transitions and texture-flush-source counters describe why the
incoming state forced a flush, so they remain attributed to the current
command-stream component. The actual batch-flush phase, draw submission, VBO
work, submitted vertices, and owned batch-flush counters follow batch
ownership.

## Components

The current component IDs are:

```text
UNATTRIBUTED
MIXED_BATCH
TITLE_COMMON
TITLE_FOX
TITLE_FALCO
TITLE_SLIPPY
TITLE_PEPPY
TITLE_ARWING
TITLE_STARFOX_LOGO
TITLE_N64_LOGO
TITLE_PRESS_START
TITLE_COPYRIGHT
TITLE_OTHER_2D
```

Each has a stable lowercase name in CSV output.

## Title Markers

`Title_Screen_Draw()` marks common setup before title lights, marks each team
member immediately before `Title_Team_Draw()` in the existing order
Fox, Falco, Slippy, Peppy, then marks `TITLE_ARWING` before the second
light/ambient setup and `Title_Arwing_Draw()`.

`Title_Draw()` marks high-level 2D title elements before their setup and draw
work: Star Fox logo, copyright symbol/text, Press Start, and the N64 logo. It
emits `UNATTRIBUTED` after the final title element so task-final flush and sync
work are not reported as N64-logo cost.

## Phase Attribution

For component-local phases, the profiler captures the active component at phase
begin and attributes duration, calls, and items to that component at phase end.
If a phase crosses a marker boundary, `component_phase_crossings` increments and
the phase still belongs to the component captured at begin.

Whole-task and unrelated phases are excluded from component-local phase rows:
graphics task total, display-list traversal, game update, audio phases,
graphics backpressure, and vblank wait. The component-region timer is the
breakdown for display-list traversal time.

Supported component-local phases include G_VTX processing, triangle processing,
clipping, texture prepare/decode/upload, batch construction/flush, PSPGL state
setup, vertex-stream upload, draw submission, `glFlush`, and finish/sync when
they occur inside an active graphics task. Deferred batch submission phases use
the scoped batch owner rather than the current command-stream marker.

Raw timing is preserved. Adjusted timing subtracts the existing timer-overhead
calibration and clamps underflow to zero.

## Outputs

Existing aggregate files are unchanged. When component profiling is enabled,
capture stop also writes:

```text
profile-NNN-components.csv
profile-NNN-component-phases.csv
profile-NNN-component-categories.csv
```

`profile-NNN-components.csv` has one row per component, including zero-use
components. It includes region counts, raw and adjusted time, per-frame time,
percent of component-task time, marker entries, display-list command counts,
nested display-list calls, geometry/TnL counters, clipping counters, state
resolve counters, batch flushes, batch flushes owned by this component,
mixed-batch participations, owned batch vertices, draw/VBO/upload counters,
texture counters, texture wrap/cache-parameter counters, `glFlush` calls, and
sync calls.

`profile-NNN-component-phases.csv` is long-form:

```text
component_id,component_name,phase,calls,total_us_raw,total_us_adjusted,items
```

`profile-NNN-component-categories.csv` is long-form:

```text
component_id,component_name,category,name,count
```

Categories are `flush_reason`, `batch_state_transition`, and
`texture_flush_source`.

`profile-NNN.txt` records component profiling enablement, component count,
marker commands seen, invalid marker IDs, component switches, task starts/ends,
unexpected outside-task state, phase crossings, single-owner batch flushes,
mixed-owner batch flushes, empty-owner batch flushes, maximum components in one
batch, mixed-batch vertices, scope begin/end counts, invalid scope nesting,
raw/adjusted component task time, sum of component raw/adjusted time, and
static component storage.

## Invariants

Including `UNATTRIBUTED`, component counters should sum to the equivalent
graphics-task-owned aggregate counters. Counters that can legitimately occur
outside a component-active graphics task are not forced into a component.

Per-component flush reasons, batch-state transitions, and texture-flush sources
should sum to the aggregate category totals for graphics-task-owned events.

Component timing raw sums should match total component task time. Adjusted
component task time is the sum of component-adjusted region times, so it should
match `sum of component adjusted time` exactly.

## Hardware Capture Procedure

1. Launch the game.
2. Allow the title screen to reach its stable state.
3. Wait about 8 to 10 seconds so initial texture uploads and light transition
   are over.
4. Start one 300-frame capture.
5. Do not press buttons or move the stick.
6. Keep `profile-NNN.txt`, `profile-NNN.csv`, and all component CSV files
   together.
7. Repeat only if component totals or timing look unstable.

Absolute component timing is instrumented. Use component shares, counts, and
relative costs within the same capture as the primary evidence.
