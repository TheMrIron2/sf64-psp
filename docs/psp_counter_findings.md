# PSP Hardware-Counter Findings

First hardware-counter measurements of the SF64 PSP renderer, 2026-07-27.
Method and CSV schema in `docs/psp_hw_counter_profiling.md`. These supersede the
assumptions in `docs/PSP_Hardware_Audit.md` where they disagree.

Build `9506f34d` (n64psp `d50d7be`, system PSPGL), 333/166 MHz, audio off,
300 recorded frames after a 120-frame warm-up, `counter_source=thread_profiler`.

Three scenes: **title** (Star Fox team and Arwing), **corneria** (representative
gameplay), **light** (menu list, two small spinning Arwings, starfield).

## Summary

| | title | corneria | light |
| --- | ---: | ---: | ---: |
| graphics task | 25.30 ms | 21.59 ms | 4.07 ms |
| swap wait | 7.91 ms | 11.67 ms | 12.46 ms |
| frame total | 33.21 ms | 33.26 ms | 16.53 ms |
| effective rate | 30 fps | 30 fps | 60 fps |
| display-list commands | 3,272 | 3,861 | 592 |
| loaded vertices | 2,546 | 2,056 | 280 |
| submitted vertices | 6,499 | 2,713 | 384 |
| draw calls | 177 | 148 | 18 |

Task and swap figures are from the unscoped build for title and corneria, and
the scoped build for light. Draw counts are from the scoped build.

## Finding 1: everything is CPU-bound, and the GE never gates

CPU cycles account for **97.6–97.9%** of graphics-task wall time in all three
scenes. The task is not waiting on anything; it is executing.

The swap is the mirror image. `eglSwapBuffers` burns 0.5–3.1% CPU across its
window — 0.12 ms of work inside 7.9 ms at the title. It is idle, not busy.

Frame totals are quantised to the 60 Hz display: title and corneria land on
33.2 ms (two vblanks), the menu on 16.5 ms (one vblank). The proof is an
accident of the instrumentation: the scoped build costs 4.08 ms more per frame
at the title, and the frame total did not move at all.

```text
unscoped   task 25.30 + swap 7.91 = 33.21 ms
scoped     task 29.38 + swap 3.83 = 33.21 ms
```

The extra 4 ms came entirely out of idle swap time. So the swap is pure slack,
and the GE is not the limit in any measured scene — the title submits 2.4× the
vertices of corneria and still totals the same.

**Headroom against the 30 fps target: 8.0 ms/frame at the title (24%), 11.7 ms
at corneria (35%), 12.6 ms in the menu (76%).** Sound-off graphics is not short
of time at these scenes. That headroom is what audio synthesis will consume.

## Finding 2: over half of every CPU cycle is a stall

Share of graphics-task CPU cycles:

| | title | corneria | light |
| --- | ---: | ---: | ---: |
| **memory stall** | **36.9%** | **41.0%** | **38.2%** |
| coprocessor stall | 6.6% | 6.5% | 7.7% |
| internal stall | 5.9% | 6.3% | 6.0% |
| VFPU stall | 3.0% | 2.8% | 2.4% |
| **total stalled** | **55.2%** | **60.4%** | **54.3%** |

This is the central result. The renderer spends more of its time waiting on
memory than doing anything else, in every scene, regardless of load.

It also explains two failed experiments. The no-projection candidate removed a
reciprocal and three stores per vertex; the material-template work removed 40–60
instructions per state resolve. Both removed *arithmetic* from a loop that is
memory-bound, and both measured exactly neutral. That was not measurement noise
or instruction layout — it was the wrong axis. **Candidates must be judged on
bytes moved, cache misses and stalls, not on instructions removed.**

## Finding 3: PSPGL draw submission is the largest single cost, and it scales with draw calls

Splitting the frontend (share of frontend time):

| scope | title | corneria | light |
| --- | ---: | ---: | ---: |
| **submit** (PSPGL draw) | **36.3%** | **31.1%** | **21.9%** |
| vertex (`G_VTX` TnL) | 16.1% | 15.5% | 13.5% |
| texture | 3.9% | 6.9% | 2.6% |
| uninstrumented remainder | 43.7% | 46.5% | 62.0% |

Counter attribution at the title, as a share of the whole graphics task:

| counter | submit | vertex | texture | remainder |
| --- | ---: | ---: | ---: | ---: |
| CPU cycles | 36.8% | 15.9% | 3.7% | 43.6% |
| memory stalls | 37.5% | 10.9% | 6.7% | 44.9% |
| **uncached stores** | **87.8%** | 0.0% | 0.0% | 12.2% |
| bus accesses | 46.5% | 8.9% | 5.1% | 39.5% |
| cached loads | 44.0% | 8.1% | 1.5% | 46.4% |
| I-cache misses | 40.5% | 8.7% | 7.8% | 43.0% |
| D-cache misses | 31.3% | 15.9% | 3.3% | 49.5% |

Submission owns 88% of the game's uncached store traffic (94% at corneria) and
nearly half its bus activity.

**The cost is per draw call, not per vertex.** Fitting submission time against
draw count and vertex count across the three scenes:

```text
submit ≈ 50.2 us per draw call + 0.27 us per vertex
```

The fit predicts the menu scene, which was not used to derive it, within 14%.
At the title that is 8.9 ms of fixed per-draw cost against 1.8 ms of
vertex-proportional cost — **a 5:1 ratio**. Per draw, consistently across all
three scenes:

| per draw call | title | corneria | light |
| --- | ---: | ---: | ---: |
| elapsed | 60.2 us | 55.4 us | 49.0 us |
| CPU cycles | 19,900 | 18,150 | 16,300 |
| memory stall | 24.1 us | 27.3 us | 22.1 us |
| I-cache misses | 81 | 97 | 84 |

Roughly 3.7 us of each figure is measurement overhead; the real cost is ~46–56 us
per draw. Eighty-plus I-cache misses per draw is ~5 KB of instruction refill to
issue one draw, which points at PSPGL's state translation and command generation
rather than at the vertex copy.

**Implication: the lever is draw count and per-draw cost.** At the title, 177
draws per frame at ~50 us each. Halving either saves ~4.5 ms/frame — larger than
any other single opportunity measured.

## Finding 4: texture upload is not a bottleneck

Texture work is **3.9% of the frontend at the title, 6.9% at corneria, 2.6% in
the menu** — and lower still once measurement overhead is removed, since the
scope is entered ~200 times per frame. In absolute terms it is roughly 0.4 ms
per frame at the title and 1.1 ms at corneria.

This contradicts the audit's ordering. Primary Finding 2 and Work Package C
propose one-pass texture creation with a new PSPGL mapping extension — a
substantial piece of work whose entire target is ~1 ms/frame at best. Work
Package D addresses a path costing ten times that.

**Texture work should be deprioritised** until something changes the picture.

## Finding 5: the VFPU path has little left to give

Vertex TnL is 16% of the frontend. Within it, VFPU stalls are 18.7% of the
scope's cycles — and 99.97% of *all* VFPU stalls in the frame occur there, so
the kernel is exactly where it should be and nowhere else.

The ceiling is small: eliminating every VFPU stall at the title saves ~0.9 ms.
The audit's VFPU audit plan (Experiments 1–4) is chasing a sub-millisecond
target. Experiment 5 — running the kernel with its output pointing directly at
PSPGL stream memory — remains valuable, but for its data-movement effect, not
its arithmetic.

## Finding 6: the uninstrumented remainder holds the scalar float work

The 44% of the task not covered by any inner scope — display-list
interpretation, triangle assembly, clipping and the software-vertex to GE-vertex
conversion — contains:

- **78.9%** of scalar FPU instructions
- **85%** of coprocessor stalls
- **44.9%** of memory stalls
- **49.5%** of D-cache misses

Scalar float runs 2.4:1 against VFPU across the frame (134 FPU instructions per
loaded vertex), and four-fifths of it is outside the TnL kernel. This is
Finding 3's first write — the conversion from the large software vertex into the
final 24-byte batch — and on these numbers it is a bigger block than submission.

It is also the least understood part of the frame, because it is defined by
subtraction. **It needs its own scopes before anything is built against it.**

## Finding 7: the menu has an uninstrumented draw path

In the menu scene, only 31.6% of uncached stores occur inside the batch
submission scope; the other 68% are elsewhere in the frontend. The most likely
source is the starfield, which does not route through `psp_gfx_dl_flush_batch`
and so is invisible to the `submit` scope. Harmless for performance — the menu
has 76% headroom — but it means menu-scene numbers under-report submission.

## Finding 8: draw calls are texture-bind breaks, and there are no texture uploads

A phase capture on the same two scenes (`profile-000` title, `profile-001`
corneria) attributes every batch break. The counts match the counter captures
exactly — 54,900 `G_VTX` commands, 57,900 texture lookups, 53,100 batch flushes
at the title in both builds — so the two instruments agree on the same workload.

Batch flushes per frame, by cause:

| cause | title | corneria |
| --- | ---: | ---: |
| **texture change** | **174.0 (98.3%)** | **121.0 (80.7%)** |
| blend or render-state change | 2.0 (1.1%) | 27.0 (18.0%) |
| projection or transform change | 0.0 | 0.9 (0.6%) |
| end of task | 1.0 | 1.0 |
| **buffer full** | **0.0** | **0.0** |
| clipping path | 0.0 | 0.0 |

At the title, texture binding causes 98% of all draw calls. Nothing else varies:
across 53,100 flushes the batch-state transition counters record `texture_env`,
`alpha_test`, `blend` and `premultiplied` changes of **zero**. Only `texture_id`
(52,200) and wrap mode (15,000, a subset of the same flushes) differ.

Draws are correspondingly tiny — 35.5 vertices, 11.8 triangles per draw at the
title; 17.6 vertices, 6.6 triangles at corneria — against ~50 us of per-draw
cost.

**There are zero texture uploads at the title.** 193 lookups per frame, 100%
cache hits, 0 misses, 0 bytes uploaded across all 300 frames; 94 textures sit
resident (76 RGBA16, 7 CI8, 11 converted). Corneria uploads 0.92 textures and
2.8 KB per frame.

This retires Primary Finding 2 and Work Package C outright. A one-pass texture
upload path optimises something that happens **zero times per frame** in one
scene and once per frame in the other. The texture cost measured in Finding 4 is
entirely cache lookup and bind, not conversion or upload.

It also **disproves the `glMapBuffer` hypothesis** for the per-draw cost: 98.9%
of title draws and 100% of corneria draws take the small-arena path, which maps
once and holds the mapping. Only 600 large draws in 300 frames map per draw. The
~50 us is not buffer mapping.

Corneria does show texture *cache* pressure, unlike the title: the RGBA16 cache
runs full at 96/96 with 102 unique keys and 63 evictions, and the converted
cache at 64/64 with 117 unique keys and 209 evictions, with essentially every
eviction followed by reuse. Capacity is a handful of entries short of the
working set — a constant worth raising regardless of anything else here.

Two supporting numbers: clipping is negligible at the title (0.8 partially
clipped triangles per frame) but real at corneria (56 per frame, plus 151
trivially rejected), and the TRI2 pair fast path hits 99.9% at the title.

Caveat: the phase build runs the title at 83 ms/frame (12 fps) against 25.3 ms
release, a 3.3x slowdown. Its counts are exact and build-independent — they
match the counter build — but none of its timings are usable.

## What this changes

1. ~~**Merge draws across texture binds.**~~ **DONE, 2.25 ms/frame.** 174 of 177
   draws per frame at the title existed only because the bound texture changed.
   Shipped as the open batch pool; see Finding 9.
2. **Raise the texture cache capacities.** Corneria's RGBA16 cache holds 96
   against 102 unique keys and the converted cache 64 against 117, with
   near-total reuse after eviction. Cheap, and it removes the only real upload
   traffic measured anywhere.
3. **Instrument the 44% remainder.** It is the biggest single block in the frame
   and is currently defined only by subtraction. Scopes for triangle assembly
   and batch construction come before any Work Package D design work.
4. **Work Package D on data-movement grounds.** Writing final vertices straight
   into PSPGL stream memory removes a full cached write and a full cached read
   per vertex. Also worth testing: staging in cached memory with one writeback
   per draw, rather than storing through an uncached mapping.
5. **Work Package C is retired, not deprioritised.** Zero texture uploads per
   frame at the title, one at corneria. The VFPU micro-experiments stay
   deprioritised at a sub-millisecond ceiling.
6. **Weigh audio against all of it.** Every measured scene already hits its
   vsync target with 24–76% headroom. If the goal is 30 fps sound-*on*, moving
   audio synthesis to the Media Engine may matter more than any renderer change
   on this list.

## Finding 9: merging draws across texture binds is worth 3–4.4 ms/frame

Measured on hardware, 5,220 tasks from boot through the title, menus and most of
Corneria. The report is cumulative, so the windows below are differenced.

| window | draws/frame | merged | saved | barriers |
| --- | ---: | ---: | ---: | ---: |
| **title** (258 tasks) | **177.3** | **89.3** | **49.6%** | 25.0% |
| menus and map | 116.9–128.8 | 94.6–120.0 | 0.0–19.9% | 45–95% |
| transitions, sparse scenes | 47.8–87.6 | 33.4–72.1 | 9.0–32.0% | 14–51% |
| **Corneria flight** (1,200 tasks) | **149.2** | **85.7** | **42.5%** | ~24% |

**The title halves: 177 → 89 draws per frame.** At the counter-derived ~50 us
per draw that is **4.4 ms/frame**, on a 25.3 ms task with an 8 ms budget to
spare. Corneria flight removes 63.5 draws per frame, **3.2 ms/frame**.

**Merge potential is governed entirely by the barrier fraction.** The two
columns move in exact opposition. Where blending dominates, nothing merges: the
menu and map window (tasks 600–900) is 95% barriers and saved *exactly* zero
draws — 36,010 flushes, 36,010 potential. Where opaque geometry dominates,
roughly half the draws collapse. This is the technique's shape: it pays in the
scenes that cost, and does nothing in the scenes that are already free.

Run lengths across the whole capture (16,781 runs): 1:1408, 2-3:914, 4-7:4922,
8-15:1090, 16-31:3625, 32-63:4375, 64-127:117, 128+:330. The title contributes
258 runs in the 128+ bucket — one run of 133 mergeable batches per frame, plus
44 barriers, and no other break. `largestGroup` peaked at 22, so at most 22
batches ever share one material. `overflowTasks` stayed 0, so the 512-batch cap
never biased the estimate low.

### What to build

Not full-frame deferral. Sorting a whole task means buffering every batch before
emitting — 156 KB of vertices per title frame — and restructuring the
interpreter into a deferred command queue.

The data points at something cheaper: **an open-batch pool**. Instead of one
current batch, keep a set of open batches keyed by material; a primitive joins
its material's open batch if one exists, otherwise opens a new one. A barrier
flushes every open batch in submission order. With 45 distinct materials per
title frame and a largest group of 22, a pool of ~48 captures nearly all of the
measured potential with bounded memory and no change to the interpreter's
streaming structure.

### Implementation result: ACCEPTED

Shipped unconditionally, 64 slots x 192 vertices. The accepted release binary is
byte-identical to the candidate that was measured.

**Title frame time 24.2-24.3 ms -> 22.0 ms, a 2.25 ms improvement** — 22x the
0.1 ms acceptance bar and 7x the noise band that reverted the two earlier
candidates. Counters at the title, per frame:

| counter | before | after | |
| --- | ---: | ---: | --- |
| `vfpu_inst` | 142,610 | 142,610 | identical, vertex work untouched |
| `uncached_store` | 181,797 | 180,314 | -0.8%, same vertex bytes written |
| **`i_miss`** | 28,898 | **16,741** | **-42%**, the predicted mechanism |
| `memory_stall` | 3.02 M | 2.13 M | -30% |
| `bus_access` | 3.64 M | 2.81 M | -23% |
| `cpuck` | 8.21 M | 7.06 M | -14% (24.6 -> 21.2 ms) |

The two unchanged counters are what make this conclusive. `vfpu_inst` matching
to the digit proves the vertex workload is untouched, and flat `uncached_store`
proves no data movement was removed. The entire saving is fewer draws, and the
42% drop in instruction-cache misses is the ~85 misses per draw the counters
predicted, multiplied by 88 fewer draws.

`task` + `present` is unchanged at 33.2 ms, so the saving became headroom rather
than framerate: still 30 fps, now with 11.5 ms idle in the swap instead of 7.9.

The per-draw model predicted ~4.4 ms and delivered 2.25 ms, so it was optimistic
by about half. Direction and mechanism are confirmed; treat the 50 us per draw
as an upper bound in future estimates.

**Corneria confirms it independently**, on a section 1-3% *heavier* than the
baseline capture (more commands, loaded and submitted vertices):

| corneria, per frame | before | after | |
| --- | ---: | ---: | --- |
| task | 21.59 ms | 19.39 ms | **-2.19 ms** |
| cpu cycles | 7.00 M | 6.29 M | -10.2% |
| memory stall | 2.87 M | 2.13 M | -25.7% |
| `i_miss` | 30,865 | 22,653 | -26.6% |
| `bus_access` | 3.03 M | 2.35 M | -22.5% |

Normalised for the heavier workload the controls are tighter still:
**`vfpu_inst` per loaded vertex 56.58 -> 56.50 (-0.13%)** and **`uncached_store`
per submitted vertex 27.48 -> 27.38 (-0.4%)**, while cycles per submitted vertex
fall 11.8% and I-cache misses per submitted vertex fall 27.9%. Arithmetic and
data movement per unit of work are unchanged to within a fraction of a percent;
only the draw count moved.

Stalls fall from 58.8% to 54.4% of cycles at corneria, memory alone from 41.0%
to 33.9%. The task remains 97.4% CPU-bound, so the shape of the problem is
unchanged — there is simply less of it.

**The `light` menu scene is not comparable**: its pre-pool capture was taken on
the scoped build, which inflates the frame, and the post-pool capture is
unscoped. The merge analysis predicted ~0% for menu scenes because they are ~95%
blend barriers, and nothing here contradicts that. Closing the gap would need a
fresh unscoped capture of the pre-pool binary; the scene has 76% headroom, so it
is not worth the run.

Caveat for the record: the two counter captures carry different `sf64_commit`
(`9506f34d` vs `32a9885f`), so the counter magnitudes are directional rather
than a clean paired control. The 2.25 ms frame-time delta is same-tree and is
the quotable result.

#### The bug that nearly shipped

The first hardware attempt rendered 3D correctly but lost most 2D: the drain
path zeroed the standalone batch instead of emitting it, so every unpoolable
primitive discarded whatever was pending. `drawv` caught it immediately — the
title read 6,213 against a baseline 6,495, and the all-blended menu screens read
0. Three sibling defects came out of the same review: parking a slot left
`batchCount` stale, entering a slot orphaned pending direct geometry, and
draining on every unpoolable primitive destroyed 2D batching.

After the fix, on eight frames matched by identical `(cmds, vtx, tri)` across
title, menu, map and Corneria, `drawv` is conserved exactly. Visuals confirmed
correct on hardware.

Delivered merge rate at the title: **45.0 pool draws per frame against the 45.3
distinct materials the model predicted**, with 103 appends per frame that would
each have been a draw before. Peak slots in use was 45 of 48, so the default is
close to its limit — 64 slots is the safer setting.

**Frame time is not yet measured.** The predicted saving is ~4.4 ms/frame at the
title, far above the 0.3 ms noise band, but the two previously reverted
candidates also removed real work and measured neutral. This is unproven until
it clears the acceptance rules: three paired 300-frame clean-build runs and a
counter capture showing the cost actually fell.

### Risks

- **Coplanar decals.** The estimate assumes free permutation inside a run, and
  SF64 has ZB decals biased toward the camera. Reordering those can change which
  surface wins. Needs visual verification, and possibly keeping decal batches
  pinned to their neighbours.
- The 50 us/draw figure comes from the counter fit (50.2 us fixed + 0.27 us per
  vertex). Merging removes draws, not vertices, so only the fixed part is
  recovered — which is what the estimates above use.

## Finding 10: the post-pool remainder is now the largest block

A scoped post-pool capture on `adb38632` re-derived Finding 3 after the title
fell from 177 to 108 sampled submissions per frame. Shares of frontend CPU
cycles:

| scope | title, standalone | title, PSPLINK | light, PSPLINK |
| --- | ---: | ---: | ---: |
| submit | 30.4% | 30.6% | 20.9% |
| vertex | 15.8% | 15.9% | 13.7% |
| texture | 4.4% | 4.4% | 2.9% |
| **remainder** | **49.5%** | **49.1%** | **62.5%** |

Submission is still expensive, but it is no longer the largest title block.
The next measurement should split the remainder into triangle handling,
clipping and final batch construction before another submission optimisation.

The standalone and PSPLINK title captures saw identical commands and loaded
vertices. Standalone CPU cycles were 0.85% higher, I-cache misses differed by
less than 0.01%, and every scope share was within 0.4 percentage points. A
standalone capture that reports `counter_source=thread_profiler` is suitable for
future counter work and avoids PSPLINK setup.

Corneria produced no capture. Both standalone and PSPLINK attempts crashed
during warm-up; the PSPLINK exception address `0x001DFB10` resolves to
`sceGeListEnQueue` in the scoped ELF. Warm-up does not arm scopes or sample
counters, so this is a separate GE submission failure rather than evidence of a
counter-read fault. The regular release EBOOT reproduced at the same symbol,
excluding the counter build.

The exception's command-buffer address resolves to PSPGL list 15. Its apparent
stall address is at the end of a full 512-word list. This is not simple GE queue
exhaustion: PSP firmware provides 64 list records and PSPGL permits at most 16
outstanding lists.

`make psp-dlist-diagnostics` builds a local-PSPGL PRX that validates each list
and import stub immediately before enqueue. It completed Corneria without a
failure log or PSPLINK exception. That rules out a consistently malformed list,
but does not prove a fix because the guards change timing and layout.
`make psp-local-pspgl` is the uninstrumented control. Its enqueue machine code is
equivalent to the release path and it reproduced the crash, excluding
local-library linkage. Its command buffer resolves to ring slot 8 rather than
the earlier slot 15, but both failures use the same 511-word rollover shape.
The failure is therefore not tied to one list object.

`make psp-dlist-layout-control` retains the full diagnostic executable layout
but disables its runtime work. Its `.text`, symbols and section sizes match the
full diagnostic build; one initialised data byte selects the mode. It reproduced
the crash, excluding layout. The live import stub still contained `jr $ra`
followed by a valid syscall instruction after the exception, excluding stub
corruption. `make psp-dlist-snapshot` is the next control: it records only list
identity, length and addresses without scanning the ring, tail or import stub.
The three modes have byte-identical `.text` and symbol addresses.

The snapshot build also crashed. Its last coherent identity fields describe
submission 17933, ring slot 12, a free 511-word list from `0x49A59900` through
`0x49A5A0F8`. Minimal writes therefore do not mask the fault, and the same
full-list rollover now appears on slots 8, 12 and 15. The phase and result words
did not describe the faulting call coherently, so they cannot be used as an
enqueue return value.

The release regression starts with the open-batch pool. The earlier
`build/psp-pool64` PRX is byte-identical to today's regular `build/psp` PRX, so
making the already-tested candidate unconditional did not introduce a separate
code change. The pool changes the GE command stream or its timing; enqueue is
where that problem surfaces.

The layout-identical 64-slot pool pair settled causality. The enabled control
crashed at `sceGeListEnQueue`; the bypass completed Corneria. Their PRXs differ
by one initialised selector byte while retaining identical code, symbols, pool
allocation and PSPGL layout. The failure therefore requires pooled rendering,
not its static allocation or the change that made it default.

Transient queue pressure is excluded. The post-queue control reproduced the
failure and the pre-queue candidate also crashed after limiting the transient
outstanding peak from 16 lists to 15. The control surfaced as an interrupt in
the normal matrix-command dispatcher; an interrupt PC and `BadVAddr` do not
identify an access by that instruction. The candidate failed in
`sceGeListEnQueue` like the earlier cases.

The pool audit found a concrete texture-lifetime defect. On a texture-cache
miss, the old path flushes only the selected batch before creating the texture.
Creation may evict and delete a texture still referenced by a different parked
pool slot. The slot is later submitted with a stale PSPGL texture reference.
Texture-enable changes can likewise leave parked batches behind.

The layout-identical texture-barrier pair confirmed the cause. The unsafe
control crashed in `sceGeListEnQueue`; the safe candidate survived the Corneria
test. Their packaged PRXs differed by one selector byte. Texture changes are now
unconditional pool-wide barriers, so every parked reference is consumed before
cache eviction. The temporary selectors and PSPGL queue diagnostics were
removed after the result.

The scoped counter build now records `triangle`, `clipping` and `batch` in
addition to the original inner scopes. `triangle` covers full TRI command
handling; `clipping` stops before fan emission; `batch` covers conversion into
the final 24-byte GE vertices. A fresh title and Corneria capture can now split
the post-pool remainder rather than infer it by subtraction alone.

The clean release and scoped build subsequently completed title, menu and
Corneria captures without a crash, verifying the unconditional texture barrier
on hardware. The new 300-frame scoped captures split the frontend as follows.
Scopes are nested, so the rows are not additive:

| scope, frontend CPU cycles | title | corneria | light |
| --- | ---: | ---: | ---: |
| triangle, inclusive | 51.2% | 43.0% | 40.6% |
| batch, inside triangle | 16.5% | 9.8% | 13.7% |
| clipping, inside triangle | 0.1% | 5.2% | 0.0% |
| submit | 21.9% | 21.1% | 16.0% |
| vertex | 11.8% | 13.1% | 10.8% |
| texture | 2.5% | 6.3% | 1.9% |

The event attribution is more useful than the perturbed cycle shares. Batch
construction owns 62.2% of scalar FPU instructions and 69.2% of coprocessor
stalls at the title; the corresponding Corneria shares are 31.2% and 32.3%.
Submission owns 87.7% and 92.0% of uncached stores. Batch construction performs
the scalar conversion into cached 24-byte vertices, then submission reads them
and writes the PSPGL stream through an uncached mapping. This confirms Primary
Finding 3's two-stage data-movement target.

Clipping is not a title optimisation: 1.31 samples per frame and 0.1% of cycles.
It is real but secondary in Corneria at 64 samples and 5.2% per frame. The next
candidate should reserve PSPGL stream space for pooled batches and construct the
final vertices there, with the current cached staging path retained as the
control and fallback.

### Implementation result: ACCEPTED

The direct-stream path is unconditional. A failed PSPGL reservation retains the
old cached staging and copy path as a runtime fallback. The promoted release is
byte-identical to the hardware-tested candidate:

```text
PRX SHA-256    35618c605ee45324f2e0eacc1548998d445b5d9d83984744825cdc5fc68bc5c8
EBOOT SHA-256  f14add8b415c37b23e884179201d52adf24e8cacf19c9563747943c9ed768ee7
```

The first direct-stream smoke test measured 18.3-18.4 ms at the title against
22.0-22.2 ms for the control, but showed corrupt triangles on the title and
premature-looking edge geometry in Corneria. This was reserved-range reuse, not
depth or clipping state: the TRI2 capacity preflush submitted a full slot and
continued writing at offset zero. Cached staging was safe because PSPGL copied
the slot; the direct path overwrote vertices before the GE consumed them. TRI2
now rotates to a fresh pool slot like the other capacity paths. The corrected
path fixed the title and Corneria geometry on hardware and averages 18.5 ms at
the title, against 22.0-22.2 ms for the control: ~3.6 ms, or 16%.

The first matched unscoped counter pair confirmed the gain in four scenes:

| scene | control task | direct task | change |
| --- | ---: | ---: | ---: |
| title | 22.355 ms | 18.501 ms | -3.855 ms (-17.2%) |
| light (menu) | 3.448 ms | 3.228 ms | -0.219 ms (-6.4%) |
| other (training) | 7.917 ms | 7.529 ms | -0.389 ms (-4.9%) |
| corneria | 19.146 ms | 17.889 ms | -1.258 ms (-6.6%) |

Title and menu work matched; title submitted vertices differed by only 0.02%.
Training and Corneria were slightly different gameplay windows, with 0.4% and
1.4% more commands in the candidate. Normalised per submitted vertex, CPU cycles
fell by 17.4% at title and 5.9-7.5% in gameplay. Uncached stores fell by 62.0%
at title, 35.1% in training and 61.5% in Corneria; cached loads, D-cache misses
and writebacks also fell in every scene. Title FPU work changed by +0.2% and
VFPU work was identical. The only consistent regression was I-cache misses
(+10.5% to +35.9%), which is outweighed by the data-side savings.

This confirms that direct construction removes the predicted second read and
stream copy rather than reducing geometry work. On 2026-08-05 the user accepted
the result as an explicit exception to the usual three-title-pair rule: the set
contains one matched pair in each of four scenes, all positive, with exact title
and menu work plus the expected counter movement.

### Finding 3 follow-up decision: CONCLUDED

The post-direct-stream diagnostic found exact duplicate packets in 60.6% of
title direct vertices and 31.8% at mid-Corneria. A stable source-load identity
found 96.6% and 83.7% of those duplicates with zero mismatches. Hybrid U16
storage would reduce direct vertex bytes by 52.4% and 24.1% respectively.

That transfer model does not clear indexed rendering. Sony's July 2005 graphics
seminar shows materially higher transfer cost for 24-byte indexed vertices and
recommends compact non-indexed data in eDRAM; this port's earlier indexed path
also regressed. Two non-indexed attempts tested whether the conversion work
could instead be reused:

| title candidate | task time | task cycles | batch cycles | batch FPU | batch memory stalls | batch D-misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| UV conversion cache | +4.51% | +4.57% | +17.26% | -28.24% | +29.22% | +88.08% |
| Per-command UV hoist | +1.97% | +2.13% | +7.67% | -7.85% | +56.09% | +69.14% |

Both pairs used thread hardware counters and layout-identical executables. Work
changed by +0.0018% for the cache and -0.0023% for the hoist. The cache's
arithmetic saving was overwhelmed by its data footprint. The hoist added six
stores per triangle command: 328,800 commands predicted about 1.97 million
stores, matching the measured 1,970,705 increase and a 201.74% rise in batch
D-cache writebacks. Neither candidate warranted a Corneria run.

Finding 3 ends at accepted Stage A. The audit's fused VFPU final-vertex Stage B
is retired: after direct streaming removed the extra copy, the remaining path
is memory-bound and both attempts to remove scalar UV work increased stalls and
task time. Indexed reuse was a separate alternative and is also rejected. The
packet diagnostic, source identities, experiment selectors and dedicated build
targets have been removed; detailed captures are summarised in
`docs/psp_vertex_reuse_diagnostics.md`.

## Finding 11: GE matrix packet emission is the next PSPGL submission target

The accepted direct-stream renderer was re-profiled at the title with a
layout-controlled submission build. Its volatile selector changes only one
initialised byte; control and phase 4 were verified with identical `.text` and
`.rodata`. Every fine scope had exactly 32,400 samples: 108 submitted draws per
frame. The captures matched at 3,272 display-list commands and 2,546 loaded
vertices per frame; submitted vertices varied by less than 0.002%.

Phase 9 measured the counter-read floor at 0.221533 ms and 67,147.813 CPU cycles
per frame. Subtracting that floor separately from phases 10-13 gives:

| PSPGL GE-state phase | ms/frame | CPU cycles/frame | memory stalls/frame | uncached-store units/frame |
| --- | ---: | ---: | ---: | ---: |
| preparation | 0.105713 | 28,274 | 15,810 | 0.863 |
| **matrix resolution/upload** | **0.708340** | **219,840** | **100,355** | **1,486.133** |
| dirty-register emission | 0.237457 | 73,424 | 13,076 | 346.313 |
| CLUT/vertex pointers | 0.103117 | 29,807 | 14,510 | 216.307 |
| total | 1.154627 | 351,345 | 143,751 | 2,049.616 |

Matrix resolution and upload therefore owns 61.3% of corrected time, 62.6% of
CPU cycles, 69.8% of memory stalls and 72.5% of uncached-store units in this
fixed per-draw subdivision. Its corrected cost is 6.56 us and about 2,036 CPU
cycles per submitted draw. Counter units remain uncalibrated, so the store
number proves real command-list traffic but is not interpreted as a command or
byte count.

The first candidate belonged in generic PSPGL, not SF64 display-list code.
`flush_matrix()` currently emits each 13- or 17-word GE matrix packet through a
separate `__pspgl_dlist_enqueue_cmd()` call, repeating list lookup, capacity
checking and bookkeeping for every word. A bulk reserve/emit experiment tested
whether that overhead explained the measured matrix cost.

### Matrix packet candidate: REJECTED

The profiling-only candidate emitted the trigger normally, then bulk-reserved
its 12 or 16 matrix values, splitting only at the original display-list
rollover. Both executables contained both paths and differed by one volatile
selector byte. Their title work matched exactly at 3,272 commands, 2,546 loaded
vertices and 6,499.05 submitted vertices per frame.

| title task metric | control | bulk packet | change |
| --- | ---: | ---: | ---: |
| time | 19.500606 ms | 19.538886 ms | +0.196% |
| CPU cycles | 6,312,237 | 6,319,077 | +0.108% |
| memory stalls | 2,438,795 | 2,473,614 | +1.428% |
| bus accesses | 2,574,958 | 2,604,221 | +1.136% |
| I-cache misses | 27,295 | 27,760 | +1.703% |
| D-cache misses | 7,080 | 7,144 | +0.907% |

Cached loads fell 1.61% and cached stores fell 2.32%, but the stall and miss
increase outweighed them. The candidate failed its first-pair gate, so no repeat
or Corneria run is justified. The bulk API, selector, build targets and build
trees have been removed. Matrix resolution/upload remains the measured scope,
but per-word enqueue overhead is not its useful optimisation target.

A profiling-only follow-up subdivided each dirty matrix upload into a
back-to-back counter-read control, stack resolution and optional VFPU adjustment,
and the GE trigger plus matrix-value writes. All three recorded 33,000 samples,
or 110 matrix uploads per frame against 108 draws. Work matched at 3,272 commands
and 2,546 loaded vertices per frame; submitted vertices varied by 0.001%.

After subtracting the per-upload control separately, the split is:

| matrix upload phase | ms/frame | CPU cycles/frame | memory stalls/frame | uncached-store units/frame |
| --- | ---: | ---: | ---: | ---: |
| resolution and adjustment | 0.113216 | 36,228 | 27,783 | 0.247 |
| **GE word emission** | **0.481813** | **151,254** | **69,543** | **1,484.337** |

Emission owns 81.0% of corrected time, 80.7% of cycles, 71.4% of memory stalls
and effectively all uncached stores. Software matrix resolution is not the next
target. The useful route is to prevent unnecessary packets.

PSPGL dirties the texture matrix on every texture-object change, although its
backend adjustment depends only on texture-coordinate type and whether the
texture is flipped. SF64 uses float texture coordinates, and the title's upload
rate tracks its draw rate. The generic PSPGL candidate caches the effective
texture-matrix adjustment and dirties the matrix only when that value changes.
Both A/B builds contain both paths and select them with one volatile initialised
byte.

The first 300-frame title pair had exact work at 3,272 commands, 2,546 loaded
vertices and 6,499.020 submitted vertices per frame:

| title task metric | control | cached adjustment | change |
| --- | ---: | ---: | ---: |
| time | 19.581966 ms | 19.196013 ms | -1.971% |
| CPU cycles | 6,355,668 | 6,205,975 | -2.355% |
| internal stalls | 411,269 | 399,048 | -2.972% |
| memory stalls | 2,482,069 | 2,420,881 | -2.465% |
| bus accesses | 2,625,116 | 2,545,423 | -3.036% |
| uncached stores | 68,442 | 67,027 | -2.068% |
| I-cache misses | 26,587 | 25,971 | -2.317% |
| D-cache misses | 8,538 | 8,324 | -2.509% |

The candidate saves 0.386 ms and 150k CPU cycles per frame without a stall or
miss regression. Its 1,415-unit uncached-store reduction is 95.3% of the GE
emission previously attributed to redundant matrix uploads. `task + present`
changed by only 0.000057 ms per frame, so the task saving reappeared as idle
wait. This passes the title gate. The title workload is static, and its exact
work match plus the agreement across independent counters makes repeat title
captures redundant.

The Corneria opening cutscene provided a faster, closely matched gameplay
check. Commands differed by +0.015% and loaded vertices by +0.003%; submitted
vertices were 0.968% lower in the candidate. Per-command results remove that
small workload difference:

| Corneria task metric | absolute change | per-command change |
| --- | ---: | ---: |
| time | -0.454 ms (-2.792%) | -2.806% |
| CPU cycles | -129,845 (-2.448%) | -2.462% |
| internal stalls | -1.817% | -1.832% |
| memory stalls | -5.022% | -5.036% |
| bus accesses | -5.231% | -5.245% |
| uncached stores | -4.800% | -4.814% |
| I-cache misses | -3.128% | -3.142% |
| D-cache misses | -1.429% | -1.444% |

FPU instructions changed by -0.043% per command and VFPU instructions by
-0.013%, so geometry arithmetic was effectively unchanged. The candidate clears
the performance gate in both title and gameplay. The user confirmed the title
and Corneria cutscene were visually correct on hardware. The texture-matrix
cache is accepted and unconditional; its selector and profiling targets have
been removed. The promoted normal release was then visually clean on hardware
and showed an approximate 0.2 ms title GFX improvement. This release observation
is consistent with the counter result but is not a formal matched capture.

## Appendix: how the merge potential was measured

`PSP_MERGE_ANALYSIS` was a temporary diagnostic, removed once it had answered
its question. It recorded every batch flush's full material
key, partitions each task into runs that may safely be reordered, counts the
distinct keys per run, and reports the draw count that would remain.

A batch is treated as a **reorder barrier** when it blends, or has depth test or
depth write off — those cannot move relative to their neighbours. Barriers stay
in place and remain their own draw. Everything between two barriers is assumed
freely permutable, so the reported figure is an **upper bound**.

The key covers every argument `psp_gfx_dl_flush_batch` passes to
`PspGfxPspgl_DrawColoredTriangles`: texture id, texture env and env colour, wrap
S/T, alpha test, blend, premultiply, depth test/write, fog and its colour and
range, projection serial, pretransformed and point filter. Two batches merge
only if all of it matches.

It reported through the log every 300 tasks:

```text
[pspgl-merge] task= tasks= flushes= potential= savedPermille= drawsPerTaskX1000=
              potentialPerTaskX1000= barriers= runs= maxRun= largestGroup=
              vtxPerDrawX1000= vtxPerPotentialDrawX1000= overflowTasks=
[pspgl-merge-runs] len:count buckets, powers of two
```

`savedPermille` was the answer. `largestGroup` shows the best
case single merge, and the run histogram shows whether reorderable runs are long
enough to be worth sorting. `PSP_MERGE_ANALYSIS=1` forces `PSP_LOG=1`, so output
lands in `sf64_psp.log` on the first writable root.

Two limits on the number. Coplanar decals depend on draw order even with depth
write on, so a real implementation would need to keep those together — the
estimate does not model that and will read slightly high. And batches past 512
per task count as unmergeable, which biases the estimate low; `overflowTasks`
reports whether that happened at all.

The analysis lived in `src/psp/gfx/gfx_psp_merge.c`, separated from the renderer
so it could be unit-tested on the host; nine cases covering identical keys,
distinct keys, alternating pairs, leading/trailing/interior barriers, all
barriers, the empty task and cap overflow all passed. Recoverable from git if a
future batching question needs it.

## Caveats

- **Counter units are uncalibrated.** `uncached_store`, `cached_load` and
  `bus_access` may count events or bytes; the data fits bytes slightly better
  (submission shows 24.5–25.1 uncached stores per submitted vertex against a
  24-byte vertex, plus ~20 per draw call, consistent across all three scenes).
  Ratios and shares are safe; absolute bandwidth figures are not, and none are
  quoted here.
- **The scoped build perturbs the frame.** The original three inner scopes added
  ~4 ms at the title. The triangle and batch scopes add about 2,200 high-frequency
  samples per frame and raise the measured task to 37 ms. Read attribution, not
  absolute milliseconds, from these captures.
- **One capture per scene.** These are diagnostics, not A/B results. Candidate
  captures must be layout-controlled and workload-matched. One exact pair can
  settle the deterministic title when time and independent counters agree;
  variable gameplay still requires a matched cross-scene validation.
- **Corneria sections differ slightly** between the scoped and unscoped runs
  (3,861 vs 4,036 commands per frame), so those two captures are not directly
  comparable frame-for-frame.
- Counters are per-thread, so they exclude other threads' work while the render
  thread is descheduled — which is what makes the swap-wait reading meaningful.
