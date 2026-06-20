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

## Phase CSV

`profile-NNN.csv` has one row per timed phase:

```text
phase,inclusive_or_exclusive,calls,total_us,us_per_frame,us_per_call,percent_of_capture,items,us_per_item
```

All times are wall-clock microseconds from `sceKernelGetSystemTimeWide()`. Rows are inclusive. Nested rows, such as display-list traversal inside graphics task total, must not be summed as independent frame time.

`items` is populated only where a natural denominator exists, such as vertices or triangles. Otherwise it is zero.

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
