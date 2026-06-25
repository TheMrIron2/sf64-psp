# PSP Profiling

This repository has two hardware-oriented PSP profiling modes.

## Builds

```bash
make psp                         # normal release path
make psp-profile-gprof           # build/psp-profile-gprof
make psp-profile-phases          # build/psp-profile-phases
make psp-profile-combined        # optional combined diagnostic build
make psp-profile-builds          # gprof + phases
make psp-profile-artifacts       # timestamped complete build bundle
```

`PROFILE_PSP=1` enables `-pg -g -fno-omit-frame-pointer -fno-optimize-sibling-calls` while keeping the normal `-O2` optimisation level. It defines `SF64_PSP_GPROF=1` and packages the stripped ELF into `EBOOT.PBP` while retaining the exact unstripped `starfox64.psp.elf` and map in the build directory.

`SF64_PSP_PROFILE_PHASES=1` enables the low-overhead phase profiler. It does not reference gprof symbols and can be built independently.

Authoritative measurements should use the separate gprof and phase EBOOTs. `psp-profile-combined` remains available as a diagnostic build, but gprof instrumentation and phase timers perturb each other.

Profile targets default `PSP_FPS_OVERLAY=0`, `PSP_LOG=0`, `PSP_TRACE=0`, `PSP_RENDERER_DIAGNOSTICS=0`, validation off, and queue tracing off. Pass an explicit make variable to override one of these for a diagnostic run.

Each profile variant builds n64psp into an isolated directory, such as `lib/n64psp/build-psp-gprof` or `lib/n64psp/build-psp-phases`. Gprof builds also pass `N64PSP_PROFILE_PSP=1`, so n64psp PSP objects are compiled with call-arc friendly flags instead of only receiving samples at exported symbols.

## Controls

Reach the target scene first, then use:

```text
SELECT + L       start or reset capture
SELECT + R       stop and dump capture
SELECT + START   existing game exit
```

The profiling chords are edge-triggered and consumed before PSP input is mapped to N64 controls.

The status line is:

```text
PROF OFF
PROF REC 000
PROF SAVED 000
PROF ERROR
```

## Output

Captures are written to:

```text
ms0:/PSP/GAME/SF64PROFILE/
```

Gprof builds write:

```text
gmon-000.out
gmon-001.out
```

Phase builds write:

```text
profile-000.csv
profile-000.txt
profile-001.csv
profile-001.txt
```

No file I/O is performed during an active phase-profile measurement window. Data is accumulated in fixed RAM structures and written after capture stops.

## Instrumented Boundaries

The phase profiler records inclusive wall-clock timings for:

```text
graphics task total
display-list traversal and dispatch
G_VTX processing total
triangle processing total
software clipping/subdivision
texture lookup/decode/preparation
texture decode/conversion
texture upload
batch construction
batch flush total
PSPGL state/setup
PSPGL vertex stream upload
PSPGL vertex stream upload small draws
PSPGL vertex stream upload large draws
PSPGL draw submission
PSPGL draw submission small draws
PSPGL draw submission large draws
glFlush queue flush
graphics finish/synchronisation
audio task dispatch
audio synthesis task creation
audio update work
game/update work
graphics task completion/backpressure wait
vblank or idle wait
```

Some categories are nested. For example, display-list traversal is inside graphics task total, clipping is inside triangle processing, and the PSPGL small/large upload and submission rows are inside their aggregate PSPGL rows. Do not add overlapping rows together.

Counters cover opcode counts, exact G_VTX batch sizes from 0 through 64, lit/unlit vertices, matrix changes, triangle acceptance/rejection/clipping, generated clipping vertices, texture cache/decode/upload events, batch flush reasons, draw calls, submitted vertices, `glFlush`, and swap/synchronisation calls.

## Hardware Test Protocol

Install each EBOOT in a separate Memory Stick folder, for example:

```text
ms0:/PSP/GAME/SF64GPROF/EBOOT.PBP
ms0:/PSP/GAME/SF64PHASE/EBOOT.PBP
```

For each scene below, use a separate capture so navigation does not contaminate the data:

```text
title screen
menu
training low-cost view
training high-cost view
```

For the gprof EBOOT:

1. Boot the gprof EBOOT.
2. Reach the scene.
3. Hold a repeatable menu, camera, or aircraft state.
4. Press `SELECT + L`.
5. Wait long enough to sample the scene.
6. Press `SELECT + R`.
7. Exit with `SELECT + START`.
8. Copy `ms0:/PSP/GAME/SF64PROFILE/gmon-NNN.out`.
9. Retain the matching `build/psp-profile-gprof/starfox64.psp.elf`, map, and SHA metadata.

Repeat the same scene sequence with the phase EBOOT. The phase capture stops automatically after `SF64_PSP_PROFILE_CAPTURE_FRAMES` graphics tasks, default 300, but `SELECT + R` may stop early.

Return these files for analysis:

```text
all gmon-NNN.out files
all profile-NNN.csv files
all profile-NNN.txt files
matching unstripped starfox64.psp.elf files
matching starfox64.psp.map files
PROFILE_BUILD_COMMANDS.txt
SHA256SUMS
```

Generate gprof reports on the host with:

```bash
make psp-profile-report \
    ELF=build/psp-profile-gprof/starfox64.psp.elf \
    GMON=/path/to/gmon-000.out \
    OUT=/path/to/gprof-000.txt
```

The generated report records the gprof executable and version, ELF SHA-256, gmon SHA-256, and build ID when available. It fails if the capture appears empty, contains no samples, or appears mostly unresolved.

Use `make psp-profile-artifacts` before hardware testing to copy complete gprof and phase builds into `artifacts/psp-profile-YYYYMMDDTHHMMSSZ/`. Raw Memory Stick captures should be copied into that bundle's `raw/` directory; generated reports belong in `reports/`.

## Interpretation

Do not infer bottlenecks from PPSSPP timings or library microbenchmarks. Rank future work only after real PSP captures using gprof self time, gprof inclusive time, phase wall-clock time, calls per frame, items per frame, G_VTX batch distribution, percentage of graphics/frame time, and maximum possible whole-frame gain.

Evaluate TnL, clipping, texture conversion/upload, display-list dispatch, batching, PSPGL submission, synchronisation, audio and game simulation as competing hypotheses. Do not constrain the ranking to VFPU work.
