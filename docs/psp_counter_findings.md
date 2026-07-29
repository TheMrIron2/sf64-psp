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
- **The scoped build perturbs the frame** by ~4 ms at the title, ~3.7 us per
  sample across ~1,100 samples per frame, landing inside the inner scopes. Read
  proportions, not absolute milliseconds, from scoped captures.
- **One capture per scene.** These are diagnostics, not A/B results. Any
  optimisation candidate still needs the handoff's three paired 300-frame runs
  on a clean build.
- **Corneria sections differ slightly** between the scoped and unscoped runs
  (3,861 vs 4,036 commands per frame), so those two captures are not directly
  comparable frame-for-frame.
- Counters are per-thread, so they exclude other threads' work while the render
  thread is descheduled — which is what makes the swap-wait reading meaningful.
