# PSP Profile Output Schema

PSP profiling captures are written under:

```text
ms0:/PSP/GAME/SF64PROFILE/
```

Capture slots are zero-padded decimal numbers and are never silently overwritten.

## Gprof

`gmon-NNN.out` is the raw PSP gprof capture. It must be paired with the exact unstripped ELF from the same build directory, normally:

```text
build/psp-profile-gprof/starfox64.psp.elf
```

Generate a host report with:

```bash
make psp-profile-report \
    ELF=build/psp-profile-gprof/starfox64.psp.elf \
    GMON=/path/to/gmon-000.out \
    OUT=/path/to/gprof-000.txt
```

The report contains gprof's flat profile, call graph and function index.
The header also records the gprof executable and version, ELF SHA-256, gmon SHA-256, and build ID when the matching build metadata is present.

## Phase CSV

`profile-NNN.csv` has one row per timed phase:

```text
phase,inclusive_or_exclusive,calls,total_us_raw,total_us_adjusted,us_per_frame_adjusted,us_per_call_adjusted,percent_of_capture_adjusted,items,us_per_item_raw
```

All times are wall-clock microseconds from `sceKernelGetSystemTimeWide()`. Rows are inclusive. Nested rows, such as display-list traversal inside graphics task total, must not be summed as independent frame time.

Adjusted columns subtract the measured minimum timer read-pair overhead once per completed phase call. They do not subtract nested phase time. Use raw columns when validating profiler overhead; use adjusted columns for first-pass phase comparisons.

`items` is populated only where a natural denominator exists, such as vertices, triangles, or upload bytes. PSPGL draw submission rows use submitted vertices; PSPGL vertex stream upload rows use uploaded bytes. Otherwise it is zero.

## Phase Text

`profile-NNN.txt` starts with metadata:

```text
SF64 git SHA
n64psp submodule SHA
Perfect Dark reference SHA
compiler
optimisation flags
PROFILE_PSP
SF64_PSP_PROFILE_PHASES
CPU clock
bus clock
capture slot
requested frame count
actual frame count
timer overhead us
timer overhead samples
phase totals note
forced active phase ends on stop
```

Machine-readable sections follow:

```text
[opcode counts]
opcode,count

[G_VTX batch histogram]
count,commands

[flush reasons]
reason,count

[texture statistics]
cache_hits
cache_misses
decodes_or_conversions
uploads
bytes_uploaded

[clipping statistics]
input_triangles
trivially_accepted
trivially_rejected
partially_clipped
generated_vertices
output_triangles

[TnL statistics]
display_list_tasks
gvtx_commands
vertices_loaded
modelview_matrix_commands
projection_matrix_commands
matrix_compositions
lit_vertices
unlit_vertices
normal_transforms
normalisations
lighting_evaluations
clip_code_calculations
perspective_divides
tri1_commands
tri2_commands
batch_flushes
vertices_submitted
draw_calls
glFlush_calls
sync_calls
```
