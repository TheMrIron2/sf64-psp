# PSP Hardware-Counter Profiling

Restores the Allegrex hardware-counter capture described in
`docs/PSP_Hardware_Audit.md`, Primary Finding 1, as an independent build mode.
This is the measurement foundation the audit's remaining sequence depends on:
the counter gate in `docs/psp_graphics_optimisation_handoff.md` is not waivable,
and two candidates have already been reverted for lack of it.

The earlier capture (`1dff247c`, removed in `d92ba666` and `53938305`) existed
only to serve the no-projection A/B. This one is scene-tagged, scope-decomposed
and carries full build metadata, so captures are comparable across builds.

## Build

```bash
make -j8 psp-profile-hw-counters
```

Output lands in `build/psp-profile-hw-counters/`, alongside the loadable
`starfox64.psp.prx`,
`profile_build_metadata.txt`, `PROFILE_BUILD_COMMANDS.txt` and `SHA256SUMS`.

The target pins the audit's suggested configuration:

```make
PROFILE_HW_COUNTERS=1
PROFILE_PHASES=0
PROFILE_COMPONENTS=0
PSP_FPS_OVERLAY=0
PSP_RENDERER_DIAGNOSTICS=0
PSP_LOG=0
```

Phase profiling stays off deliberately. Its timer calls sit inside the same code
the counters are measuring, and the point of this build is to measure the
release renderer.

`PSP_PROFILE_MODE` remains `release`, so the capture links the same `n64psp`
archives as a release build. The metadata records the mode as
`release+hw-counters`.

| Variable | Default | Meaning |
| --- | --- | --- |
| `PROFILE_HW_COUNTERS` | `0` | Compiles the capture in. Everything below is inert without it. |
| `PROFILE_HW_COUNTER_FRAMES` | `300` | Frames recorded before the capture auto-saves. |
| `PROFILE_HW_COUNTER_WARMUP_FRAMES` | `120` | Frames discarded after the start press, matching the handoff's warm-up rule. |
| `PROFILE_HW_COUNTER_SCOPES` | `0` | Adds the nested texture, vertex, submit, triangle, clipping and batch scopes. The frequent samples perturb the frame; use them to locate a cost, not to quote release timing. |

With `PROFILE_HW_COUNTERS=0` every hook is a macro that expands to nothing and
`hw_counter_profile.c` is not built. Verified: `make psp` produces a
byte-identical `EBOOT.PBP` with the capture present in the tree.

## On hardware

For a standalone launch, copy `build/psp-profile-hw-counters/EBOOT.PBP` to the
usual `ms0:/PSP/GAME/<folder>/`. PSPLINK loads PRX files, not packaged EBOOTs;
use `build/psp-profile-hw-counters/starfox64.psp.prx` there.

Controls, all held with `Select`:

| Combo | Effect |
| --- | --- |
| `Select` + `Left` / `Right` | Cycle the scene tag: `title`, `corneria`, `light`, `other`. Only while idle. |
| `Select` + `L` | Start a capture: warm-up, then record. |
| `Select` + `R` | Stop early. During warm-up this aborts and writes nothing. |
| `Select` + `Start` | Exit. A capture in progress is written out. |

The status line on the debug screen reads:

```text
HW <scene> <CTRS|TIME> <READY|WARM nnn|REC nnn/NNN|SAVED nnn|ERROR>
```

`CTRS` means the firmware profiler is exposed and counters are being recorded.
`TIME` means counters are unavailable and the capture is wall-clock only; see
below. The capture auto-saves at `PROFILE_HW_COUNTER_FRAMES` and increments the
slot, so repeated runs never overwrite each other.

### Counter availability

Counters come from `sceKernelReferThreadProfiler()`, with
`sceKernelReferGlobalProfiler()` as a fallback. Some firmware exposes them to a
standalone EBOOT, so the on-screen `CTRS` status is authoritative and PSPLINK is
not required in that case.

When using PSPLINK:

```text
profmode t
reset
```

then relaunch the PRX. **`profmode` is not persistent** — it is a shell setting,
not an INI one, so it must be re-issued after every PSP boot, before `reset`.
Both refer calls return NULL when the counters are unavailable, which is safe.

Never call `pspDebugProfilerEnable`, `Disable`, `Clear` or `GetRegs` from this
EBOOT. They write the profiler MMIO at `0xBC400000` directly and fault in user
mode; that is what crashed the first diagnostic build.

A `TIME` capture still runs, still writes a CSV, and still records per-scope wall
time and the full build metadata, which is enough for scene and scope
decomposition. It **does not** satisfy the counter gate in the handoff's
acceptance rules. Only a `CTRS` capture does.

## Capture scopes

Four scopes are recorded by default, sampled at coarse boundaries only:

| Scope | Covers |
| --- | --- |
| `task` | The whole graphics task, the same window the FPS overlay reports as GFX time, so counter captures line up with the A/B frame-time numbers. |
| `frontend` | Frame setup and display-list interpretation, up to but excluding the final PSPGL flush. |
| `flush` | The end-of-task `PspGfxPspgl_Flush()`. Measured at ~12 us per frame, i.e. near zero — see below. |
| `present` | `PspGfx_EndFrame()`, which is `eglSwapBuffers`, outside `task`. |

`frontend` and `flush` partition `task`, but not usefully: batches are submitted
as they fill, from inside the display-list interpreter, so by the time the
end-of-task flush runs there is almost nothing left to do. **Nearly all PSPGL
cost is inside `frontend`, not `flush`.** Use the inner scopes to split it.

`PROFILE_HW_COUNTER_SCOPES=1` adds six nested scopes, all counted inside
`frontend` and therefore not additive with it:

| Scope | Covers |
| --- | --- |
| `texture` | Texture cache lookup, conversion and upload — Primary Finding 2's path. |
| `vertex` | `G_VTX`: transform, lighting and attribute assembly. |
| `submit` | `PspGfxPspgl_DrawColoredTriangles` per batch flush — the real PSPGL submission cost. |
| `triangle` | Complete `G_TRI1` and `G_TRI2` handling, including nested state, clipping, batch and submission work. |
| `clipping` | Clip-code expansion and polygon clipping, ending before final fan vertices are emitted. |
| `batch` | Conversion from software or clipped vertices into the final 24-byte GE vertex stream. |

`clipping` and `batch` are nested within `triangle`; `texture` and `submit` can
also occur there. Treat the scopes as a hierarchy, not columns to add together.
The triangle-exclusive remainder is obtained by subtracting its nested work.

### Retired post-stream submission split

A layout-controlled 17-phase diagnostic subdivided the accepted direct-stream
submission path. It found matrix resolution/upload was the largest remaining
fixed scope. A second split measured 0.113 ms per frame resolving the matrices
and 0.482 ms emitting their GE packets after subtracting matched counter-read
overhead.

Bulk matrix-value reservation failed its first title pair. The successful PSPGL
candidate instead stopped texture-object changes from dirtying an unchanged
effective texture-matrix adjustment. It improved task time by 1.971% at the
static title and 2.792% in the closely matched Corneria opening cutscene. The
user confirmed both were visually correct on hardware. The optimisation is now
unconditional; all subdivision hooks, selectors and dedicated targets have been
removed. Full measurements are in Finding 11 of `psp_counter_findings.md`.

Counters are per-thread, so they exclude other threads' work while the render
thread is descheduled. Wall time does not. A scope where elapsed time is large
but `cpuck` is small is a wait, not CPU cost — the audit's "low Allegrex time
but high total frame time" case.

## Standard capture scenes

Use all three from the audit, tagging each capture before starting it:

- **`title`** — the Star Fox team and Arwing screen. The project's most
  layout-sensitive workload and the fastest to reach. Do not optimise for it
  alone.
- **`corneria`** — a representative gameplay stretch with scenery, actors,
  effects and HUD. Fly the same route each time.
- **`light`** — a deliberately cheap scene, to expose the fixed floor of frame
  setup, PSPGL overhead and submission.

Procedure per capture: audio off, reach the scene, tag it, press `Select` + `L`,
hold the scene steady through warm-up and the recorded frames, and let it save
itself. The deterministic title needs one exact workload-matched pair when time
and independent counters agree. Gameplay comparisons must be normalised for
their recorded work.

## Output

```text
ms0:/PSP/GAME/SF64PROFILE/hw-NNN-<scene>.csv
```

Three roots are tried in order and the one used is reported on the status line:
`ms0:` (M), `ef0:` (E, PSP Go internal flash, since a Go has no `ms0:` without an
M2 card) and `host0:` (H, the PSPLINK host directory on the PC). Slots are
zero-padded and never silently overwritten. If all three fail the status reads
`ERR <step> <root> <code>`, naming the failing step and the raw `sceIo` return. The file has four
sections.

Metadata, as `key,value` rows: `scene`, `counter_source`
(`thread_profiler`, `global_profiler` or `none`), `frames`, `warmup_frames`,
`sf64_commit`, `sf64_tree`, `n64psp_commit`, `n64psp_tree`, `pspgl_commit`,
`pspgl_tree`, `pspgl_source`, `compiler`, `opt_flags`, `build_flags`, `cpu_mhz`,
`bus_mhz`, `ratio_scale`.

```text
[work]
metric,total,per_frame_x1000
```

Display-list commands, loaded vertices and submitted vertices for the capture.

```text
[scopes]
scope,samples,elapsed_us,us_per_frame_x1000,us_per_sample_x1000
```

```text
[counters]
scope,counter,total,per_frame_x1000,per_sample_x1000,per_command_x1000,per_loaded_vertex_x1000,per_submitted_vertex_x1000
```

One row per scope per counter, in the profiler register order: `systemck`,
`cpuck`, `internal_stall`, `memory_stall`, `copz_stall`, `vfpu_stall`, `sleep`,
`bus_access`, `uncached_load`, `uncached_store`, `cached_load`, `cached_store`,
`i_miss`, `d_miss`, `d_writeback`, `cop0_inst`, `fpu_inst`, `vfpu_inst`,
`local_bus`.

Every `_x1000` column is a fixed-point ratio: divide by `ratio_scale` (1000) for
the real per-frame, per-command or per-vertex figure. Integer division only —
there is no float formatting anywhere in the capture path. When counters are
unavailable the `[counters]` section is a single comment line instead of a
table; the timing sections stay valid.

Register deltas are accumulated as unsigned 32-bit differences, so a counter
wrapping inside one scope is handled, but a counter wrapping more than once
inside a single scope is not distinguishable. At 333 MHz `cpuck` wraps about
every 12.9 s, far longer than any scope here.

Read the audit's interpretation guide (`docs/PSP_Hardware_Audit.md`, Primary
Finding 1) for what each counter pattern implicates.
