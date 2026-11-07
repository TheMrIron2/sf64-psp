Rules for optimising PSP code for Star Fox 64.

## Optional scratchpad execution

Scratchpad execution is disabled by default while instability and build
complications remain unresolved. Use `make SCRATCHPAD=1 psp` to enable the
preserved implementation, or `make SCRATCHPAD=0 psp` to disable it explicitly.
It remains limited to full GU/unified builds with the production pinned layout;
profiling and diagnostic configurations that disable that layout still use RAM.
Disabled builds omit scratchpad sources, assembly rewriting, linker wrappers,
image generation and verification. The compile flags stamp rebuilds affected
objects when switching settings in the same build directory.

## OOT gap audit — October 2026

The [current-head comparison](OOT_PSP_OPTIMISATION_GAP_AUDIT.md) recommends a
game-matrix census and a narrow scalar ME ADPCM MADD trial, not another texture
policy. Recent Corneria reservation work is small and allocation reuse leaves
decode/writeback traffic intact. Renderer VFPU support does not cover engine
matrices; the linked calculation stack is not 16-byte aligned, so charge staging
before reusing the aligned n64psp multiply. OOT keeps scalar ADPCM predictor sums
in integer HI/LO while current SF64 emits repeated MULT/MFLO; preserve exact
low-32-bit semantics and distinguish this from rejected VME ADPCM. Neither lead
has a new SF64 hardware result. SF64 already has independent audio production
and hardware SRC; OOT's deeper ME command construction is deferred.

The [matrix census](oot_matrix_census.md) counts source entries rather than
symbol calls: Allegrex splits Mult and inlines some operations into angle and
LookAt helpers. Count NEW copies separately from APPLY work, and keep partial
capture scopes visible. GAME_UPDATE includes DRAW_GENERATION; their wall times
and thread run-clock windows must not be added together. Census off/on retains
diagnostic call sites and cannot establish clean release savings.

The [ten-scene hardware review](matrix_census_hardware_review.md) confirms only
one fully aligned engine Mult per gameplay generation and strong NoTranslate
frequency in Corneria/Zoness. Titania's roughly 2,071 translated Vec3 calls per
update support investigating its duplicated 1,024-pixel wheel rotation first.
Keep both chain scrolls. GU's `texture upload` timer mostly counts texture binds,
not pixel uploads; converted-cache counts mix CI4/IA8/IA16. Approximate lighting
must compensate model scale and preserve the matrix used for positions.

The [Titania wheel A/B](titania_wheel_ab.md) now compares two passes with one in
a single GU executable. Keep both scrolls and latch selection before Game_Update.
Use its coarse update timer and eligibility count; the GFX timer excludes wheel
update work. Host equality and preserved GU reservations pass. On 14 October
the owner reports E=30–32, no visual discrepancies and a possible small gain;
manual timing windows cannot be matched usefully in the advancing level. Keep
the saving unquantified and do not demand more of the same manual comparison.
The owner approved promotion on 14 October. Production removes only the duplicate
call, preserving both scrolls and Macbeth; the A/B flag, controls and timers are
removed. The clean production regression passes 4,204 exact host comparisons.
Paused or boss-active windows were controls.

The next [texture residency census](texture_residency_census.md) records source
format and final PSM separately. Seed residency from warm cache allocations at
the first recorded task, include retired storage until freed, and do not count
mutable refresh as a new allocation. Track simultaneous aggregate peaks rather
than adding cohort peaks. The [six-sweep review](texture_census_hardware_review.md)
finds only slot-pressure evictions, no RAM fallback or allocation failure, and
IA8 decode averaging at most 4.43 microseconds per task. That motivated the
IA8 memory/sampling trial reviewed below. Old hardware upload-byte counters
assume four bytes per texel; use final-upload payloads.
Retained meshes also leave source command counts incomplete; use tagged times
with their coverage limit rather than source command fractions.

The 192-slot trial is closed: fewer evictions did not establish a clean hardware
performance benefit, so 128 remains production. The [IA8 4444 trial](codex/ia8_4444_trial.md)
keeps that capacity and halves final IA8 storage. Quantise after SF64 transfer,
environment blending and soft coverage; native four-bit inputs do not make those
outputs lossless. Host errors reach 8/255 RGB and 7/255 soft alpha. Preserve GE
flushes and distinguish the hardware texture cache from software residency.
The 21 October screen rejects promotion: 4444 mean task time is +0.32% at
Title, +1.61% at Aquas and +19.39% at Zoness. Bolse's −5.80% mean comes with
worse P95/P99 and sharply varying task times within the recording. There is only one
A/B pair per scene, so repeatability and causal format regressions remain
unproven. Visual acceptance is also unrecorded. Close the trial and keep 8888;
no further census or format heuristics are warranted for this trial. Smaller
allocations alone do not justify promotion.

The independent [whole-list GE probe](ge_execution_replay.md) reuses a completed
list at its original address before the next frame can reclaim its data.
Retained indices and inline vertices must both survive. Save initial GE state
and colour/depth contents, validate replay output, then restore completed state
before presentation. Texture-cache contents cannot be snapshotted; report the
explicit flush policy. Serial enqueue/wait time has a different overlap and bus
condition from normal DIRECT frontend/task time. Neither a short finish tail
nor a fast serial result alone establishes a hardware-lighting speedup.

The 22 October Aquas captures now pass exact colour/depth replay validation,
including retained indexed draws. Warm serial GE medians are 1.513 and 1.768 ms,
against preceding DIRECT frontend means of 17.630 and 21.535 ms. Empty-list
overhead is about 16 microseconds. This strongly supports a CPU-side bottleneck
in those views and makes a bounded retained-lighting trial reasonable; it does
not price that trial's saving under normal bus contention. Warmed Title never
reached replay: holding the main/render thread can prevent VI forwarding needed
to reach the probe's required task wait. Treat this as an observation-point
limitation, not insufficient warmup or a rendering mismatch. See the
[hardware review](ge_execution_replay.md#hardware-results--22-october).

Close GE replay with those two Aquas results and retain the isolated diagnostic.
The next [small audit](scratchpad_code_feasibility.md) concerns executable
scratchpad placement, independent of the failed data-cache experiment. Five
VFPU leaves total 1,840 bytes with no object relocations, making a bounded
correctness test plausible. Their instruction-stall cost remains unmeasured;
aggregate misses and fast serial GE execution do not prove an I-cache bottleneck.
Do not develop this and GE lighting simultaneously. Limit further optimisation
to these bounded experiments before returning to PSP UI and gameplay polish.

The [review and standalone code probe](scratchpad_code_feasibility.md#review-fact-check-and-standalone-experiment)
add an aligned RAM-copy control, a RAM-only power-lock check and warm versus
instruction-pressure workloads. Animal Forest does include state/draw code,
but source annotations do not prove instruction-cache eviction or a saving.
The probe builds and passes static checks. On 23 October the owner accepted
the RAM-only power-lock check after a passing second attempt; the first attempt
shut down and required a full boot, with cause unconfirmed. Scratchpad execution
then passed all 432 comparisons, code guards and restoration at stable
222/111 MHz, and ordinary suspend/resume after completion also passes.
Warm 32-vertex timing is effectively neutral; pressure totals
fall 0.12–0.34% against original code and include the executed pressure loop.
One capture without miss counters or whole-task timing establishes feasibility,
not an SF64 speedup. No VFPU cluster expansion is justified by this result alone.

The bounded [draw-state trial](scratchpad_state_trial.md) redirects four calls
after compilation while preserving all backend instruction sections and pins.
Preserve private callee register assumptions when adding diagnostic dispatch;
matching a C signature alone does not guarantee this. The shared 2,848-byte GU
state helper is the only copied function. All four hardware captures pass guards
and restoration at stable 333/166 MHz. Fixed recorded work in Aquas, Bolse and
Macbeth yields savings of 0.213 ms (1.09%), 0.045 ms (0.20%) and 0.306 ms
(1.62%) per device task. Scratchpad beats original code and the aligned RAM copy
in every balanced round of those three views. The changing-work first capture's
1.39% slower mean remains inconclusive. Loaded-vertex counts are disabled in
this build and submitted vertices omit retained mesh draws; equal coarse counts
do not prove identical commands or an I-cache cause. The additional scenes
supersede the earlier closure recommendation and pass the diagnostic timing
gate. Proceed with production integration for this helper only, resolving
ownership and safe suspend/resume before persistent scratchpad execution.
Validate the cleaned build before promotion. The owner subsequently authorised
one bounded [combination screen](scratchpad_combination_trial.md): all subsets
of state application, effective material resolution, texture preparation and
batch flush, totalling 5,212 code bytes in 5,888 guarded bytes. Compare every
addition against state alone; spare SRAM is not a reason to copy more code.
Viewport selection has a compiler jump table and is excluded. No general
relocation framework or further shortlist expansion is planned.
The isolated Bolse/Macbeth/Aquas screens now pass with fixed recorded work in
each. State + material + texture (4,692 bytes) saves a further 0.337/0.233/0.172 ms
against state alone, consistently across all nine scene/round comparisons.
The finalist confirmation also wins all nine comparisons, saving
0.287/0.219/0.270 ms (1.51/1.19/1.50%) in its own Bolse/Macbeth/Aquas views.
Flush helps Bolse by a further 24 microseconds but is effectively neutral in
Macbeth and slower by 9 microseconds in Aquas. It fails the cross-scene gate.
Select the 4,692-byte three-function set and close combination selection.
Title's incomplete sweep is excluded; absolute times are not pooled across
different views or binaries. Next resolve production ownership and safe resume,
then validate the cleaned build. Spare SRAM does not justify more candidates.

## Build-time preprocessing audit

The [asset audit](ASSET_PREPROCESSING_AUDIT.md) separates recurring conversion
from startup construction. Current 396-leaf retained storage requests 484,048
bytes including material programs; the 3 MiB limit is not a reserved arena.
Moving that storage to rodata does not by itself reduce total RAM or warm draw
work. Latest Corneria attribution assigns 96.5% of texture decode time to
invalidated keys, so do not price those as immutable texture opportunities.
The proposed first experiment replaces water animation's finite integer row
and column mappings with 48 bytes of tables, preserving dynamic pixels and
invalidation. Its 16 cosine and 32 sine calls survive PSP compilation and use
software double arithmetic. No hardware saving is established yet. Skeleton
index searches are another compact metadata candidate. Starfield generation
is deterministic, but Andross mutates star 109; a read-only global replacement
would be incorrect.

## Native asset investigation

The independent span-program review is in `NATIVE_MESH_REVIEW.md`. Actual asset
inspection finds 386 of 396 leaves establish all texture support inputs before
drawing. The RC4 prototype folds material runs at initialization and validates
these immutable requirements there; palettes and texture contents remain live.
Do not confuse retained topology with fully compiled state. Folding state must
preserve intermediate mirror invalidation and trailing state writes. The old
preflight omitted inherited mirror flags; its corrected guard has a host
regression check. Clean builds compile fine mesh timers out and retain coarse
capture plus work counters. The user reports RC4 saving about 0.6 ms against RC3
in both Title and Venom, with matching reported work counters after correcting
Venom D192 to D196. Treat this as a successful experiment; promotion and cleanup
are explicitly deferred. See `NATIVE_MESH_CONTRACT_REVIEW.md` for the next review.
Current projection writers keep render and fog projections identical, so the
retained box admission performs a duplicate projection test. A 4096-transition
actual-handler host probe passes; a same-binary hardware test is proposed before
removing that work. Do not infer inside containment from object-origin culling.

The approved RC5 now removes the repeated projection pass through a shared
n64psp primitive. Allegrex assembly confirms that the old pass survived
compilation and uses scalar FPU transforms despite the VFPU conventional TnL
path. Verify the actual selected primitive before labelling work as VFPU.
The complete writer review includes invalidation, viewport, snapshots and special
paths; 9382 handler/edge comparisons and all 396 asset tests pass. The physical
RC4/RC5 sweep is accepted below. See
`build/experiments/native-single-projection/README.md` for delivery and checks.

The full hardware sweep establishes broad RC5 as the production candidate:
Title 13.7–14.0 ms against conventional 15.3–15.5 and subset 14.1–14.2 ms;
Venom 20.4 ms against both 20.7 ms references. Freeze the tested executable and
source before removing controls. Cleanup changes compiler specialization and
layout even when semantics stay fixed; benchmark one clean candidate against
frozen RC5 and retain existing reservations. The 65 current Venom fallback
attempts are not 65 complete box tests: most failures in the older capture
stopped at the initial state guard. See `NATIVE_MESH_PROMOTION_PLAN.md`; cleanup is now approved and the selector-free candidate is built. All 396 leaves
match frozen RC5 host output/work, while compiler decisions changed five
unchanged AOT bodies. Retain the hardware gate despite preserved pins. See
`NATIVE_MESH_PRODUCTION_REPORT.md`; hardware validation of cleanup is pending.

The 10 October packed candidate compares the original thirteen leaves with
396 qualified leaves in one executable. Its cache is sized to actual data and
draw spans follow material changes rather than N64 vertex loads. Pack referenced
vertices densely; a first/last index range can otherwise include unused loads.
Keep cold packing outside the interpreter unit to limit compilation coupling.
The backdrop weld also needs a depth-free predicate: tiny 3D models can appear
flat enough to satisfy a geometric-only tolerance. See the candidate section
in asset_aot.md; hardware acceptance remains pending.

First same-build hardware reports are RC0/RC1/RC2 15.9/14.4/15.6 ms, with
Corneria seams correct. Broad execution saves 0.3 ms against conventional but
costs 1.2 ms over the small subset. Fixed code/pins and prebuilt capacity rule
out cross-build movement as that comparison's cause; accessed data and draw
traffic still differ. Price aggregate restoration, production and submission
before more asset selection. The comparison remains experimental.

The next candidate proves dead outputs from bounded actual caller continuations
before discarding them. Unknown calls and limits retain restoration. Discard
must release old projection references and invalidate stale flags. RC2/RC3
prices proof overhead and omitted reconstruction in the same binary.
The hardware comparison now reports RC2 15.6–15.8 ms and RC3 14.3–14.6 ms
with identical H70/F3/D140/V2608. Restoration falls from 1103 to 181 slots,
with 922–986 skipped. Broad coverage matches the subset timing. Outgoing-slot
reconstruction is therefore a major architectural cost in this view; the result
does not isolate arithmetic from working-set effects. Matched Venom saves only
0.2 ms (20.2→20.0), and broad discard remains 0.2 ms behind the subset (19.8).
The user reports visuals all OK. Skipped-slot totals alone cannot predict savings;
price admission, fallback and proof costs before promoting broad coverage.

The material-first RC4 experiment failed: Title RC3 15.1–15.3 ms versus
RC4 15.2–15.4 ms, Venom both 21.0 ms with matching counters. Stop reorders.
The new executable moves 793 common executable symbols outside the 200 pins,
including retained dispatch. Preserved pins do not isolate the whole layout;
linked-byte changes can be relocations and do not prove a cache cause. Measure
aggregate successful/failed admission, vertex preparation, caller proof and
CPU/GE boundaries before further code changes. Set a production stopping rule:
one diagnostic build, then one clean acceptance build; reject broad coverage
if it still loses to the saved accepted subset. Exact cache attribution is
separate research, not a prerequisite for that production decision.
The aggregate diagnostic buffers captures before writing, separates CPU frontend
from finish/sync, and marks nested intervals as inclusive. Timing-off probes and
coarse clocks still perturb the executable; use its same-binary off/on control.
The remaining GE wait is not total GE execution time. Diagnostic probes can change
conventional inlining enough to exceed pins; preserve the production plan for cleanup.
The completed diagnostic finds timing-off broad penalties of 1.131 ms Title
and 0.372 ms Venom. Venom rejects 54 draws on initial state and nine on bounds,
with no material failures. Fine probes add 1.1–3.3 ms depending on workload.
Reject broad promotion; finish with accepted coverage, packed packets and proven
discard, then remove diagnostics and selectors. That combined clean subset still
needs its hardware gate; restore the saved accepted reference if it loses.

The 9 October [architecture audit](asset_aot.md#architecture-audit-9-october-2026)
distinguishes generated executable AOT from retained geometry data. Geometry
in RAM does not directly displace I-cache, and allocated capacity is not the
accessed working set. The retained adapter still lights and streams each draw,
restores final CPU vertex slots, preserves load-driven draw splits and repeats
model-matrix submission. Price those costs before expanding coverage. The
survival guide repeats this project's evidence; it does not independently prove
cache causality. The remembered 14.8/16.0 ms pair also changed retained coverage.

The [AOT working document](asset_aot.md) now holds the coverage, hardware results
and next steps. Keep the accepted 168 leaves and Fast3D fallback. The broad
233-leaf expansion loses against baseline; compact runs, improved lookup and
several placement controls have not established a production win. Restoring
writable addresses was insufficient, and the cache mechanism remains unproven.
Pause placement trials and keep representation fixed.

Prioritise recurring essential models using measured fallback use and
interpretation cost. The ordinary Arwing skeleton, normal pilot heads and cockpit
glass are already native (15 leaves), as are the Blue Marine body/propeller/rudders.
Landmaster, space Arwings, Star Wolf, Bill/Katt and Great Fox main lists remain
supported direct-read candidates. Their exclusion is not caused by aliases.

Qualify read-only alias consumers through shared data-flow contracts. Skeletons
copy a list pointer into a local selector; a callback can replace that pointer
without mutating command bytes. Unknown callbacks/escapes and actual writers
still need review. Do not create per-actor manifests, audit all 634 held lists
or enable a broad set based only on command counts.
The missing-model review verifies eleven core lists with the current compiler.
The detailed Arwing is native, but its space detail fallbacks are not; the planet
fallback is accepted. Landmaster body/cannon form the clearest first two-leaf
test (237 commands per visible non-versus draw). No archived capture measures
these candidates. The source audit also misses plain `.c` includes such as
`fox_edata_info.c`; uncovered compiled users keep those aliases held. Repair
include traversal before qualifying read-only ObjectInfo consumers. Validate a small measured
selection through the existing compiler, report text/data growth and require a
hardware gain against 168 leaves. Further notes go into the same working document.
The focused GU 170-leaf test adds only Landmaster body/cannon through named
selection in the shared compiler: 8,084 bytes of text and no data. It also loses:
Title 15.5 to 16.5–16.7 ms, paused Titania start 17.0 to 17.2 ms. Title does not
execute the new lists, so this is further evidence of a common binary/dispatch
cost, not proof that native Landmaster execution is slower. Two additions are
enough to trigger it; small code growth does not guarantee an unaffected scene.
Keep 168 and do not subtract Title's loss from Titania to infer a native gain.
The cause remains unresolved; do not resume blind placement or broad conversion.
Object comparison now establishes whole-file inlining coupling: adding the two
lists changes 19 existing function instruction streams, including six Title
leaves growing by 876 bytes. GCC's dump shows a Title triangle-helper call
rejected at the unit-growth limit in 168 but inlined in 170. Identical generated
source is insufficient to control the executed code. Three controls restore
Title code, wider changed code, or lookup while preserving candidate module
offsets, data and load/file sizes. Hardware results must establish how much of
the loss comes from compilation versus dispatch or remaining placement effects.
The user reports 16.5/16.6/16.6 ms for Title Code/Shared Code/Dispatch: none
recovers 15.5 ms. The inlining coupling is real but does not explain the loss
on this evidence. Stop those controls; keep the cause unresolved.

OOT-PSP commit `035bc7d` suggests bounded retained GE meshes instead of generated
functions or interpreted native tables. Its unlit, fog-free terminal-suffix
contract does not cover SF64's lit Title models or lit/fogged Titania player.
Landmaster's final eligible shape is only 12 body and 2 cannon triangles because
material changes break the suffix. Evaluate genuine runtime coverage and
referenced-data immutability before implementing; command immutability alone
does not permit freezing vertices or textures. Preserve slot side effects,
shared lighting/clip semantics and buffer lifetime. Details remain in the AOT
working document. A bounded host prototype now retains whole supported leaves
across materials, including all 114/34 Landmaster triangles. Eighteen payloads
pass 147,456 scalar differential cases; existing lighting-only n64psp matches
fused TnL colours. The shared PSP builder is 1,348 bytes text plus 96 rodata.
These establish a starting point, not a GE or hardware win. Lit instances still
need fresh interleaved colours/UVs; the existing renderer already batches draws.
Pilot one already-native large Title limb, preserve final slots eagerly and
validate clip/cull/matrix state and GE lifetime before gameplay coverage.
The Peppy GU pilot is ready with RM0/1/2 and a single bounded heap cache.
It adds 8,920 text bytes, 96 rodata and 64 BSS against frozen 168; shader and
final-slot host checks pass but hardware gain is pending. Mechanically correct
Peppy, including neutral timing, must be followed by the roughly thirty-instance
Venom fighter stress case before deciding whether retained GE TnL is worthwhile.

## Current direction: GU frontend mechanics

The next phase investigates list publication and cache maintenance, not more
generic redundant-work removal. Installed libGU publishes the DIRECT stall at
GetMemory, DrawArray and Finish; a short final sync does not price those calls.
The five SF64 vertex writebacks target inline uncached list storage, unlike the
cached texture producers. The same-binary WB0 test saved 0.2 ms at Title and in gameplay on PSP, as
reported by the user. The five inline vertex writebacks are removed in production,
with the selector, controls and now-unused reserved-draw byte calculation deleted.
Texture writebacks remain necessary for cached producers. The cleaned binary
needs a final PSP check; no aspect-specific or audio-on result was supplied.
Two-list SEND also requires extending texture lifetime beyond the CPU frame;
alternating command arrays alone would permit premature texture reuse.
See `docs/codex/gu_frontend_review.md` for the lifecycle, census, source evidence,
experiment order, accepted hardware result and measurement limits.

Allocation-publication suppression is also accepted: the user reports a clean
production saving of 0.1–0.2 ms. Earlier Title/Corneria intro counter captures
(16:9, audio off) verified removal of all allocation publications, with no errors
or reported visual issues. Their 0.445/0.255 ms diagnostic differences included
AP1-only timer overhead and unequal workloads; those are not the accepted gain.
The production device overrides GetMemory with the SDK allocation path minus
its stall call. Draw/finish publication remains in the installed SDK, with the
same DIRECT scheduling and no extra frame of latency. All experiment controls,
flags, state and counters are removed. No Makefile option is added.

The draw-publication trial favours DP8. Clean paused 16:9 tests report Venom
23.7→23.0 ms and heavy Aquas 22.1→21.6–21.7 ms, with Title/Menu unchanged at
overlay resolution and no visual discrepancies. DP16 only adds 0.1 ms at Venom
and raises Menu from 2.7–2.8 to 3.1 ms. Menu counter build time falls 0.086 ms
but final sync rises 0.457 ms: fewer syscalls can lose useful overlap, even in a
light scene. A final short tail does not predict the effect of delaying draws.
The accepted fixed-DP8 implementation publishes the first draw immediately, then
every eight, with SDK Finish publishing the remainder and no cross-frame
latency. All trial controls/state/diagnostics are retired. The user accepts the
cleaned build, reports Title about 16.3 ms and commits it as `2c8eeb6b`. An older
Zoness water time is recalled as 4.8 ms versus current 5.5 ms; this is not a
controlled regression measurement and the user declines historical bisection.
Keep the accepted cadence. Same-frame SEND lost on PSP: Title 16.5→18.0 ms,
Menu +0.7 ms and Fortuna +1.6 ms. Its controls and instrumentation are removed.
This serial result does not price a two-list design: without measured build,
writeback, enqueue and wait costs, do not attribute the entire loss to one cause.
Defer cross-frame lifetime/latency machinery; price hot GU packing under DP8
before choosing a small direct GE primitive. Avoid scene policies or byte
thresholds without new hardware evidence. The restored clean baseline is
confirmed at 16.3 ms. GUPACKING hardware census (16:9, audio off, no visual
changes) finds small state/matrix packing costs and substantial timer overhead;
Corneria GT1 aggregate/calibration is excluded for a frame-boundary anomaly.
No broad direct GE rewrite is justified by these samples. One new source of
unused work is demonstrated: high-water CheckList calls survive when their
phase-profiler consumer is a no-op, about 104/54/129 per Title/Corneria/Venom
frame. Clean HQ1/HQ0 removal is neutral at Title and Venom on PSP. Retain the
accepted queries and allocation safety checks; census and HQ machinery are
retired. Fewer external calls alone did not yield a measured wall-time win.
Nested Clear/Draw/publication timings overlap, and empty calibration does not
remove all observer cost. See the frontend review for exact results.

## Work Harder, Not Smarter

Zoness exposed a recurring PSP optimisation lesson: **do not assume that avoiding arithmetic by caching or reusing larger intermediate results will be faster.** The level’s water renderer issues ~1,218 tiny `G_VTX` batches per task, averaging only 3.5 vertices each, and repeats much of the same geometry. A reuse experiment successfully cut VFPU instructions by ~39%, but task time still regressed by ~4.6 ms because the cached transformed representation was far larger than the compact source data, driving up D-cache misses, memory stalls, bus traffic, and writebacks.

In several other scenarios, indexing was shown to be slower, because the cost of bookkeeping was more expensive than allowing the PSP to perform compact calculations straight.
Indexed geometry was slower in the earlier SF64 tests. A retained mesh built
once changes the CPU bookkeeping cost, so those results do not establish its
net performance; compare retained indexed and nonindexed submission on hardware.
For indexed batching, extra bookkeeping/cache traffic beat the reduced vertex writes; for GE T&L, extra representations and state traffic beat reduced arithmetic; transformed water cut VFPU instructions by 39% and still lost because of memory and cache misses... 
The practical rule is therefore: **prefer compact data and cheap repeated computation over “smart” reuse unless the reuse path is demonstrably lighter on memory.** On Allegrex, the better optimisation target is often the fixed overhead around tiny batches—kernel setup, state reloads, avoidable stalls, redundant branches, or scheduling gaps—rather than storing more precomputed results.

A later source-display-list regrouping won where the transformed-water cache
lost. It kept compact source vertices and the existing vertex slots, preserved
triangle order, and removed repeated loads and tiny kernel calls. A matched
light Zoness A/B saved 1.20 ms/task (4.57%) despite D-cache misses rising 27.7%
and memory stalls rising 2.3%. This is evidence for eliminating source work,
not a general promise that larger batches win. The standard GU build also passed hardware checks
with no visual issues or performance regression. See `docs/codex/zoness_water_rebatch.md` for the result.

## Eliminate Work Before Caching It

When repeated work is found, the first question should not be “can this result be cached?” It should be **“why are we doing the work again?”**
This distinction has repeatedly mattered on PSP.

Material classification showed enormous repetition, but a one-entry last-result cache still regressed even with an 83% immediate hit rate. Raw combiner caching also regressed. Both added comparisons, key construction, branches, data accesses and/or code-layout pressure to avoid work that was already cheap.
The successful solution for material classification was instead to track its actual dependencies and only mark the classification dirty when those dependencies changed. Similarly, exact raw-light comparison avoids normalization, staging, dirty propagation and later work when an unchanged directional light is loaded again. The effective-state early return avoids entire chains of setters and pool lookups when there is no state change to apply.

The practical rule is:

**Prefer preventing unnecessary work over computing it and remembering the result.**

Triangle-only runs provide a bounded lifetime for sharing applied state, but
geometry still affects projection and fallback mirror wrapping. The accepted
GU change shares only projected TRI2 state with fixed wraps and the same
projection serial, and resets at generic emission or a state command. Host
comparisons pass. Hardware reported Title 16.3–16.6 ms, Map 8.7 ms and the
Zoness opening 12.3 ms against previous measurements of 16.4–16.6, 9.0 and
12.2 ms respectively. The user accepted this as a small win; the reference
was not remeasured. No transformed cache is added.

Before adding a cache, consider whether precise invalidation, an unchanged-state test, a no-op early return, or a more appropriate lifetime can make the operation disappear entirely. A high theoretical cache hit rate is not evidence of a performance win.

## Projected Lit Scheduling

The TnL scheduling change moves the input/view pointer advances into the two
padding slots after normal transfers, removing their old loop-tail copies.
Their current-vertex reads/writes are complete; arithmetic and VFPU register
order are unchanged. This removes two instructions per vertex. PSP hardware
passed all three 9,720-case comparisons, including exact old/new bits, and all
160 timing samples favoured the candidate. Summed kernel time fell 1.097%
across the benchmark, or 0.843% for seven lights. Both execution orders win,
but order affects the magnitude; these are not game workload weights.

The user reports effectively neutral game performance and Title at 16.3 ms,
possibly about 0.1 ms lower. Retain the measured kernel improvement without
claiming a whole-game gain. No new cache, dispatch path or production flag is
added.

The expanded smoke covers 9,720 batch, lighting, stride and clip cases. Host
ASan/UBSan validates the scalar test harness only. The PSP A/B checks baseline
and candidate against scalar, then exact old/new output bits before timings.
See `build/experiments/gu-tnl-schedule/` for binaries and commands.

After changing n64psp, check that the final game ELF actually contains the new
kernel. The nested library build can update an archive without relinking the
game on its first invocation. A second make relinked this candidate; the final
ELF kernel size is 744 bytes versus the baseline's 752. Comparing EBOOT hashes
caught the initially stale artifact before hardware testing.

## Static Size Is Not Dynamic Locality

Do not assume that smaller code or data structures will run faster on Allegrex.
Several apparently favourable size reductions lost on hardware:

`noinline(psp_gfx_dl_handle_vtx)` reduced the main display-list dispatcher by 18%, but Title and Map both became slower and I-cache misses increased. The extracted handler remained part of the hot working set and the new layout/call boundary was worse.
Reducing `PspGfxDlVertex` from 72 bytes to 64 bytes reduced context size and produced a convenient power-of-two stride, but nevertheless increased task time, memory stalls and I-cache misses. Compiling the texture implementation with `-Os` reduced its `.text` size by 16.9%, yet texture-creating Zoness workloads became approximately 4–5% slower. The smaller binary created more helpers and worse dynamic locality/call overhead in the path that actually executed.
Conversely, unchanged-light suppression made the dispatcher slightly larger and still won because it removed enough executed work.

Therefore:

**Optimise the dynamic working set, not the static byte count.**

Inspect generated dispatch before pricing a long source condition chain.
The GU dispatcher already compiles much of state dispatch into a jump table;
a consecutive-triangle loop removes remaining dispatch work. Its hardware
comparison reported Title 17.5 to 16.4–16.6 ms, Map 8.9 to 9.0 ms and the
static Zoness opening 12.4 to 12.2 ms. Title is the strongest result; the
smaller changes need caution at overlay resolution. The new baseline differs
from the preceding wrap comparison, so savings must not be summed across runs.

Inlining, outlining, structure packing, compiler size options and power-of-two strides are hypotheses only. Their effect on the actual instruction/data access sequence must be measured on hardware. Small perturbations in executable layout can themselves move PSP performance. For small frontend experiments, prefer a same-binary runtime A/B where possible to remove link-layout variance, then use matched scenes and repeated intervals to control workload noise. The October 2026 asset experiment checkpoint had overlapping title skeleton timings (13.5–13.6 ms A, 13.4–13.6 ms B), no observable title-logo timing change, and inconclusive moving-water timings. Same-binary A/B removes one confounder; it does not establish a speedup. If accepted, test the cleaned production binary again after the selector and experimental code are removed.

## Scratchpad Is Not Automatically Faster RAM

The Allegrex scratchpad is 16 KiB at `0x00010000`, and direct CPU access is usable. `PSP_THREAD_ATTR_SCRATCH_SRAM` should not be used; it is rejected as an illegal thread attribute on tested hardware. More importantly, fitting a structure into scratchpad does not imply that moving it there will improve performance.

The real renderer's 4.5 KiB vertex cache was tested in ordinary cached RAM, cached scratchpad and uncached scratchpad. Cached scratchpad was effectively neutral or slightly slower in matched Title and Zoness tests. Uncached scratchpad was roughly 18–19% slower. A synthetic benchmark did show scratchpad winning when another workload deliberately evicted the working set from the normal cache.

The lesson is:

**Scratchpad is useful for a demonstrated cache-displacement problem, not as a generic upgrade from RAM.**

Before moving data to scratchpad, prove that the target is suffering meaningful cache misses, that its lifetime and ownership are safe, and that the additional addressing/copy/coherency work is smaller than the misses eliminated. Prefer the cached alias for ordinary CPU working sets unless the producer/consumer topology specifically justifies uncached access. Mixing cached and uncached aliases requires explicit coherency handling.
The full research can be found at `docs/codex/scratchpad.md`.

## Memory Placement Includes the Producer and the Consumer

A reduction in stored bytes or a move to “faster” memory is not itself an optimisation. Measure the entire lifetime of the data.

Compact PSP texture formats reduced memory traffic substantially, but the first RGBA16-to-5551 implementation spent enough additional CPU time producing them to cancel most of the gameplay benefit. Replacing that producer with a small transformed-channel LUT allowed the bandwidth reduction to become a real win.

The next IA8 candidate also targets the producer: two sixteen-entry tables
replace repeated intensity/alpha work within an upload, keeping exact 8888
output and the existing cache policy. They occupy 128 temporary bytes, not a
persistent variant cache. Host comparisons pass. The 222 MHz PSP converter
benchmark reduced elapsed time by 39–68.5% with equal output in every case,
but static game timings did not establish a frame-time win. This shows why a
producer improvement must be weighted by actual uploads and checked against
the full renderer. See `build/experiments/gu-ia8-conversion/README.md`.

The user promoted this converter in `77b73396`: neutral non-upload scenes
were acceptable alongside the demonstrated upload-work reduction. This is
not evidence that the heavy gameplay tail or a 30/60 Hz target is resolved.

Likewise, EDRAM texture residency was measurably faster when compared against RAM at equal capacity, but making it production-safe required correct aliasing, cache writeback, lifetime handling and RAM spill. The final useful topology was not simply “use uncached EDRAM”: CPU production through the cached alias followed by an explicit writeback, with the GE consuming the uncached alias, proved more appropriate.

When comparing memory placement, keep capacity and policy constant unless the purpose of the experiment is explicitly to change them. A 1.2 MiB EDRAM cache versus a 2 MiB RAM cache is not a clean test of EDRAM versus RAM. Do not remove cache flushes, synchronization or lifetime barriers simply because they appear redundant. PSP hardware has disproved several generic coherency assumptions; effective texture-image changes, for example, require texture-cache invalidation even when texture memory itself has not just been modified.

The practical unit of optimisation is:

**producer cost + storage traffic + coherency + lifetime + consumer cost.**

A GU cache trial showed why slot pressure alone is not a reason to grow the cache.
Raising entries from 128 to 255 cut uploads from 4.20 to 2.60 per frame and
slot retirements from 3.19 to 1.41, but EDRAM allocation retirements rose from
21 to 1,830 across the two captures. Measured reservation time rose from 2.22
to 3.17 ms/task, D-cache misses rose, and task time did not improve. The level
runs had different work and the reservation timer included diagnostic overhead,
so these timings do not prove an isolated regression. The entry count was
restored to 128. Before changing capacity again, account for allocator work,
fragmentation, dynamic locality and clean task time alongside cache hits.

## Optimise the Critical Path, Not the Instruction Total

Check task pickup separately from buffer swaps. The GU path already swapped
immediately, but newly queued tasks waited for the main thread's next VI event.
A ready-task notification removes that avoidable scheduling gap in the GU
candidate without another framebuffer. Host models escape the 30 FPS step for
17–20 ms rendering; PSP validation is pending. Triple buffering would cost
272 KiB of the texture arena and cannot remove the task pickup delay by itself.
See `docs/codex/frame_interpolation.md` for the ownership limits and tests.

When multiple PSP processors are involved, reducing the total amount of computation is not necessarily the objective.

It can be profitable for the Allegrex and ME/VME to perform some duplicate work if that duplication allows a much larger body of work to execute concurrently and disappear from the Allegrex frame-critical path.
Conversely:

`move 5 ms to ME + wait 5 ms for ME`

has achieved essentially nothing. This applies to ME graphics experiments, VME audio, GE offload and DMA. Always measure the inclusive cost of an offload: staging, cache ownership, DMA where applicable, setup, synchronization, execution, result publication, extraction and any wait introduced on the critical processor. Avoid tiny accelerator invocations. Batch or fuse enough work to amortize setup and synchronization. Keep reusable accelerator state resident where possible. Do not move data merely to justify DMA.
Treat accelerator handoff as an explicit cache-ownership boundary. The GE consumes display lists through DMA and may run concurrently with the CPU. CPU-produced data handed to the GE must use an appropriate uncached mapping or be explicitly written back. Do not assume hardware cache coherency, and avoid unnecessary synchronization that prevents CPU/GE overlap.

Judge an offload primarily by how much **critical-path Allegrex time** disappears, not by how many Allegrex instructions were moved elsewhere.

## Tiny Batches Make Fixed Costs Important

Star Fox 64 frequently presents the PSP with many small pieces of work. In those workloads, fixed per-command, per-batch, per-call or per-state-transition costs can dominate the arithmetic performed for the vertices themselves. Historical PSPGL profiling measured approximately 50 microseconds of largely fixed submission cost per draw. Reducing draw count and removing a second vertex-stream copy produced multi-millisecond improvements while several arithmetic micro-optimisations produced nothing.

Zoness water is an even more extreme example: roughly 1,218 G_VTX batches per task averaging only about 3.5 vertices each. Any optimisation that introduces setup, lookup, copying or a function/accelerator invocation per batch starts from a severe disadvantage. Therefore, when batches are small, investigate command-dispatch overhead, redundant state traversal, batch setup, unnecessary copies, allocator/reservation overhead, synchronization and opportunities to process several commands with one setup.
Do not evaluate a kernel using throughput for a large ideal batch when the game actually feeds it batches of three or four elements.

A later 11,106-frame Zoness command-source capture found a fixed 2,280 ground commands per task: six calls to the 374-command rebatched water lists plus 36 setup commands. That is 42.6% of all interpreted commands, even in light scenes. Nine-command RCP setup lists add about 830 commands per task across the scene, or 15.5% of the total. These are concrete sources of repeated frontend work, but command share is not time share. The ground count never varies in this capture, so per-frame correlations cannot price its CPU cost; measure source time or a controlled A/B before changing it.

A paused display-mode comparison later exposed the source of much of that
fixed water work: widescreen drew six meshes, including a farther row, while
4:3 drew one. GFX was 21.2 versus 11.8 ms. Level initialization also chose mesh
dimensions by mode, while drawing used the current mode, making live switches
unsafe. The new candidate uses the original dimensions in all modes and three
side-by-side copies at the original depth. Check world coverage and mesh
lifetime when investigating aspect-ratio cost; pixel count alone is incomplete.
The user accepted the distant-row removal but reported stretched scrolling at
the sides. The extended outer cells span 700 units while their original S
range covers only a 100-unit cell. Matching position and UV values at a join
does not prove matching texture density. The follow-up recalculates S from X
once at initialization for both Zoness buffers, preserving interior UVs,
geometry and the reduced copy count. Retry initialization is idempotent;
Solar is unchanged. The user accepted the visual fix and reported widescreen
Zoness now close to 4:3 performance, without new exact timings. Details are in
`build/experiments/gu-zoness-water-uv/`.

Titania then measured 16.2 ms paused in 4:3 versus 25.7 ms wide. Its wide
terrain draws five complete copies of 26 rows. The next candidate removes
the outer two copies, reducing requested triangles from 3900 to 2340 while
keeping the three retained surfaces unchanged. This requires a PSP coverage
check; it does not establish the frame-time saving.

The user confirmed scrolling is fixed, but the three-copy build still shows
16.6/22.0 ms and 20.0/25.8 ms in paused 4:3/wide comparisons, with an early
gameplay comparison around 56/40 FPS. Unlike Zoness, Titania's copies have
identical Z positions and row counts; they add no distant row. Repeating the
whole depth of terrain at the sides still costs work. The follow-up retains
the central terrain and adds two side quads per row at the same Z positions.
This reduces wide terrain from 2340 to 884 requested triangles and 78 to 26
row matrices. Four compact edge vertices per row copy current edge heights
and normals after terrain updates, with S extended at the original density.
They affect rendering only; the central and collision meshes are unchanged.
Artificial side hills become extensions of the central edge profile rather
than complete mirrored terrain. The user accepted the fix and committed it in `f4cf6622`; no new exact
timings were supplied. Artifacts are in `build/experiments/gu-titania-skirts/`.

Titania also exposed a periodic interpolation fault in 4:3. The shared ground
transform resets backward by 220 units when the row ring advances, but the
terrain is not a repeating plane. Treat surviving rows as persistent objects:
record their full positions with row identities and discard history when a
row is recycled. A host test reproduces eleven baseline wrap jumps and passes
over 33,000 corrected terrain midpoint checks. The flat plane uses ordinary
texture-period wrapping instead. Retired PSP row matrices and excess list
capacity are removed. See `build/experiments/gu-titania-ground/README.md`.

A timed 10,463-frame follow-up measured the ground-marked display-list region at 12.77 ms/task on average, or 47.4% of attributed traversal time. In the lightest 1% of frames, its median was 13.07 ms and it occupied about 97% of traversal. The marked region includes the six water calls and adjacent setup, and the timer includes diagnostic overhead, so 12.77 ms is a priority signal rather than an isolated saving. Inspect per-tile visibility and cost before removing or reusing water work. The heavy tail has a different cause: in the slowest 1% of tasks, the effects region had a 29.29 ms median while ground remained near 13 ms. Profile effects by type after addressing the fixed water floor.

A clean 3,007-frame first-half Zoness capture measured the six water calls at 13.61 ms/task, almost all of the 13.79 ms ground region. Every tile processed 512 input triangles on every frame. Only 370 of 18,042 tile calls emitted zero triangles; their combined measured time was 0.112 ms/task on average, or 0.077 ms/task after the first 28 startup frames. Full-tile culling therefore has a small ceiling in this segment, before visibility-test cost and display-list state effects. After startup, the two side tiles at x=-1 and x=1 in the near row emitted triangle counts equal to just 5.3% and 2.6% of their input counts, yet consumed 1.69 and 1.09 ms/task. Investigate coarse sub-tile rejection if bounds can be proven conservative without undoing the rebatched list's win. The same capture attributed 62% of tagged effect time to multipurpose object ID 395; classify its draw states before specialising it. These are instrumented timings, not a production speedup estimate.

Zoness's source water vertices initially have Y=0, but `Play_UpdateDynaFloor` changes their heights every frame. `Play_InitLevel` can also rewrite the outer X/Z coordinates according to display mode. Partial-water bounds must use current heights and detect later X/Z changes; static asset bounds alone are unsafe. The existing rebatch has eight ordered geometry windows per tile, with no material commands between them, so a conservative per-window test can skip loads and triangles while preserving the final vertex-slot restoration. Test the visibility predicate and its memory cost on hardware before promoting it.

The first 1,004/1,002-frame partial-water A/B was invalid as a culling test: both captures skipped zero groups, and each water tile still interpreted all 382 commands. The custom marker used opcode `0x00`, but this project's active `G_NOOP` is `0xC0`, so the dispatcher did not recognize any marker. The `WC1` metadata alone did not prove that the path ran. Compare marker counts and actual skipped work before interpreting timing; the `WC1` run was 0.10 ms/task slower but tested no rejection.

With the marker corrected, a 500-frame `WC1` run saw and evaluated all 24,000 expected groups and skipped 6,277 groups, 176,269 triangle pairs and 253,376 source commands. After an opening transition, frames 42–499 consistently skipped 11 groups, 302 pairs and 436 commands per task. The water input-triangle totals exactly match the skipped-pair count. This proves the clip test removes work, but the run has no matched `WC0` capture or visual report, so it does not yet establish a hardware speedup or correctness.

A subsequent near-matched 503-frame `WC0` and 502-frame `WC1` pair did show a hardware win. In the stable portion, aligning the runs by one frame of emitted water geometry gives 452 pairs: task time fell 0.82 ms on average (about 3.7%), and measured water time fell 0.75 ms, improving in 451 of 452 pairs. The runs had nearly identical emitted water triangles, submitted vertices, texture uploads and non-water command counts. `WC1` consistently removed 436 water commands and 302 triangle pairs per stable frame. VFPU instructions fell while memory stalls and I-cache misses rose slightly, again showing why wall time decides. No visual issues were observed on hardware. The cull was then promoted into the standard renderer and the runtime switch removed; this cleaned build still needs a hardware performance check.

The generic triangle path already rejects shared-clip-code triangles before texture work. A 1,383-frame Zoness hardware capture found 17,926 generic actor face culls, including 14,077 that called deferred texture preparation before rejecting the triangle. Moving texture preparation and UV wrap resolution after the fully visible face-cull check reduced those calls from 11,907 to zero in a later 1,201-frame diagnostic A/B. Raw task time fell only 0.083 ms/frame (26.915 to 26.832 ms), while commands and loaded/submitted vertices also fell. Matching workload at seven- and eight-frame offsets gave inconsistent task changes (+0.001 and -0.076 ms/frame). The uninstrumented candidate was then performance-neutral on PSP hardware, with no visual issues reported. The ordering change and temporary cull diagnostics were removed. Avoiding these calls did not produce a measurable speedup in the tested scene.

The initial diagnostic capture counted zero opcode `0xBE` commands and reconciled all 4,869,882 loaded vertices across the generation outcomes. Vertex outcomes identify work to investigate, not its exact time; diagnostic timing includes the accounting overhead.

In that capture, 44.2% of ground, 51.3% of actor, 51.8% of player, and 57.7% of scenery vertex generations had no surviving triangle reference. Those percentages are an upper bound on avoidable loads: mixed visible and rejected geometry may share a `G_VTX` batch. Measure whole batches that contribute zero surviving triangles before estimating the gain from broader early culling.

Whole-batch diagnostics retain each `G_VTX` load until all its slots are overwritten or the task ends. Any emitted triangle referencing any retained slot makes that entire batch surviving, including clipped triangles. The hardware CSV reports batches, loaded vertices and vertex-handler microseconds by load-time source, unlit/lit/texgen class and outcome. It also ranks loading command addresses with a bounded site table; overflow affects site attribution only, and its totals are reported. Check `loaded_vertex_mismatch` is zero before using the results. `vertex_us` includes matrix preparation, texgen, transforms, lighting, fog and attribute work, excludes batch accounting, and overlaps frontend/source time. Its timer overhead and the extra tracking make this a diagnostic estimate of work to investigate, not a predicted production saving. No rendering work is skipped by these diagnostics.

A 1,259-frame whole-batch capture reconciled all 817,587 loads and 4,404,576 vertices, including frame-row totals. Batches with no surviving triangle contained 926,090 vertices (21.0%) and measured 1.732 ms/frame of vertex-handler work: ground 1.214 ms, actors 0.386 ms, player 0.076 ms and scenery 0.026 ms. All ground vertex work mapped to the rebatch. Its shared-reject-only batches accounted for 0.967 ms/frame; the six final restoration loads per tile accounted for another 0.245 ms/frame and were never referenced in this run (36 loads and 96 vertices per frame). Audit downstream slot consumers before suppressing those restores; their current purpose is to preserve the original list's outgoing vertex state. Bird display lists accounted for about 0.250 ms/frame of actor work with no surviving triangle; these named debris assets also form the live bird skeleton. Preserve post-limb callbacks if culling their geometry, since they capture positions used to spawn debris. No texgen batches occurred. Site overflow omitted only 0.21% of measured vertex time from address attribution; source/class/outcome totals remain complete. These findings favour a specific water audit over a general lazy vertex pipeline, but establish no new production speedup.

The restoration audit found no later Zoness geometry that reads incoming water vertex slots. Recursive inspection of all 1,508 compiled static display lists from the PSP source manifest found that each loads its own triangle vertices; all 89 RCP setup rows contain only state commands. Runtime draw-list producers were checked separately. The change removes only the six final loads, reducing each rebatch from 382 to 376 commands. Both actual water meshes passed triangle-order and bounds comparison against the restoring builder, all 256 group-skip combinations per mesh, and ASan/UBSan checks. This removes 36 loads and 96 vertices per six-tile frame, or six loads and 16 vertices for a single tile. The caller contract permits a different outgoing vertex cache; any future direct triangle producer following water must load its own slots. The measured 0.245 ms/frame is diagnostic work, not a promised saving.

A 303-frame baseline and 304-frame no-restoration candidate reconciled all frame rows, work, counters, sources, batch sites and water totals with no tracking overflow or mismatch. Each candidate frame removed exactly 36 ground commands and 96 ground vertices; never-referenced ground batches fell from 36 to zero. Water input and emitted triangles and all group-skip counts were identical throughout both runs. Measured water time fell from 16.530 to 16.215 ms/frame, while raw task time changed only from 29.484 to 29.428 ms. A six-frame offset best aligned unaffected command workloads. After excluding the first 30 frames, 105 pairs matched all non-ground source commands, submitted vertices, texture upload bytes and texture activity, with the expected vertex-load reduction. In these pairs ground time fell 0.309 ms, but task time rose 0.105 ms; later player, actor and HUD time and memory stalls increased. These instrumented captures confirm the intended removal and a lower water-region cost, but establish no whole-task win. Removing diagnostic batch accounting also contributes to the instrumented saving.

A subsequent hardware observation reported an approximately 0.1 ms improvement in water scenes. The restoration removal is retained in the standard path with no runtime or Makefile switch. No visual issues were reported.

The whole-batch ownership, timing and site-table diagnostic was removed during
the material-wrap commit cleanup after completing its water-restoration audit.
Its source is archived under `build/experiments/gu-material-wrap/retired-diagnostics/`.

In that short `WC1` run, ground still consumed about 13.9 ms of the 21.2 ms stable task, mostly from four substantially visible water tiles. The slowest frames instead added actor and effect work while ground stayed nearly flat. Effect ID 395 accounted for 402 ms of the 477 ms effect region across the capture, and its `aOrbTex` source was recreated 152 times, mostly after evictions or variant changes. The actor region also reached about 8 ms at p95 but lacks actor-ID attribution. Measure effect 395 by state and actors by ID before trying a specific fast path; texture creation time overlaps those source timings and must not be added to them.

## Measure the Machine, Not the Intended Optimisation

Interpolation can suppress presentations without missing scheduler deadlines.
The former whole-world topology check rejected a midpoint when even one object
changed the matrix stream. Host reproductions produced 48.0 or 54.9 FPS with
30 Hz simulation and no deadline misses. The repair matches compact matrix
indices by object and draw site within each player and reflection view, keeping
fallback local to incompatible groups. Initialization invalidates reused object
history. Stable objects now receive every midpoint in those tests; PSP speed and
visual validation are pending. GFX still excludes simulation and presentation.
The profiler now counts actual successful swaps and no longer calls exact
endpoints repeats. Separate dropped frames from partial interpolation and check
motion as well as FPS. See `build/experiments/frame-pacing/FIX.md`.

Depth sorting and material batching also require stable interpolation identities.
The map reused each planet draw site without an identity, allowing a sort change
to blend different planets. Scope history to the planet in both painter and
batched layer paths; do not use draw order as identity. Host map tests pass,
including batch transitions; the existing batching policy is retained.

Matching planet identity alone is insufficient when material batching changes
matrix call sites or repeat counts. A body can interpolate while its shadow
falls back to the current sample, exposing moving edges. Share the layer draw
helpers across painter and batched paths and preserve their call-site identity.
GU debug text must be written to the completed draw buffer before an immediate
swap; writing to the displayed buffer races scanout and capture. Host regressions
cover these cases; the user accepted both fixes on PSP.

Average FPS and mean GFX time do not establish even presentation spacing or a
per-frame budget. A map refresh-wait experiment evened idealized host timing but
did not fix PSP manual camera stutter, so it was removed. The FPS overlay now
reports mean/peak GFX and interpolation skips. Project painter bounds with the
same camera used for drawing: the map calculated them before input, then drew
with the new camera, allowing an unsafe batch or wrong depth order during pans.
Moving projection into Draw also removes repeated view calculations and unused
matrix commands. Host camera-crossing tests pass; PSP acceptance is pending.
Draw mod HUD text after the scene clear, not during Update, where it can be erased.

Map hardware readings show 30–70 ms moving-camera GFX peaks despite an 11–12 ms
mean. GFX includes frontend work and GE completion wait; map sorting runs outside
it. Use per-frame phase traces to separate these costs before changing painter
fallback or texture caching. Static workload bounds cannot explain rare wall-time
stalls, and a long GE wait alone does not establish a rasterization bottleneck.

Real PSP hardware timing is the acceptance criterion. Instruction counts, cache misses, profiler percentages, static disassembly, emulator performance and theoretical operation counts are diagnostic evidence only. Whole-task or frontend wall time is the primary performance endpoint. Hardware counters should explain why that time changed.

Validate CSV frame rows before using them for per-frame comparisons. Two long Zoness captures contained fewer physical `[frames]` rows than their reported frame counts, with merged rows, duplicated digits and incorrect field counts. Their aggregate sections and shorter `[texture frames]` rows parsed cleanly, but the malformed frame rows cannot support paired timing claims. The source formatter passed an 18,000-row host stress test; the corruption point between PSP file output and pasted text is unconfirmed. Keep the original CSV for the next capture and compare it with any pasted copy.

Do not treat stall counters as additive percentages of time that can simply be removed. Do not treat fewer cache misses as automatically faster: uncached accesses can reduce reported cache misses while increasing total memory stalls and making the program much slower. Instrumentation can materially perturb the workload. Fine hardware-counter scopes, logging, gprof, overlays and diagnostics are appropriate for locating or classifying work, not for claiming production timing.
Gprof in particular must be treated as reconnaissance. Compiler-generated constprop functions can be attributed to neighbouring symbols unless the complete ELF symbol table is supplied, and the PSP profiling implementation can leave time attributed to the last entered instrumented function rather than the instruction actually executing. Never derive an expected millisecond saving by multiplying a gprof percentage by a guessed per-call optimisation.

The extended GU Zoness capture gives a concrete attribution failure: supplying
the full `psp-nm` table changes matrix-multiply self time from 19.18 s to 0.06 s,
and exposes 6.94 million environment-blend pixel operations previously attributed
to texture support checks. Its separate phase capture measures 68.49 ms/task
with substantial nested instrumentation and cannot price a production saving.
Source inspection also found eager texture preparation at `G_SETTILESIZE`,
before geometry rejection or later material changes. Deferring it to existing
draw consumers remains a provisional candidate. Initial overlay observations
were Title 17.6 to 17.2 ms, Map 9.3 to 9.4 ms and the brief static Zoness opening
cutscene 12.7 to 12.6 ms. Moving gameplay was not compared; the 0.1 ms changes
are inconclusive and these results establish no heavy-scene gain. See
`docs/codex/gu_zoness_profile_review.md` for evidence, priorities and A/B binaries.

A following wrap experiment uses the existing material wrap fields to resolve
clamp/repeat at material changes and inspect triangle UVs only for fallback
mirrors. This avoids new context fields and removes the old per-triangle helper
calls. 15,376,960 differential host checks and material-resolver transitions
passed with ASan/UBSan, including encoded mirror precedence, zero masks and
signed/boundary UVs. GU and guarded/shared frontend builds passed. Against the
texture-deferral baseline, reported hardware timings improved from 17.2 to
16.8 ms on Title, 9.4 to 8.9 ms on Map and 12.6 to 12.4 ms in the static Zoness
opening. The user accepted the generic speedup and it is retained without a
selector. This supports removing repeated state work at its actual dependency
lifetime without adding a cache or enlarging the context. It establishes no
heavy-gameplay saving; a separate visual-test report was not supplied.

Commit cleanup also removes the deferred-texture statistic because it now
duplicates the total texture count. The cleaned binary needs a quick hardware
check because removing a field changes renderer layout.

For performance A/B tests use real PSP hardware. Keep clock, backend, audio state, presentation policy and build configuration constant. Warm the scene before measurement. Match commands, loaded vertices, submitted vertices, texture activity and presentation behaviour as closely as possible. Use deterministic scenes for exact comparisons and repeat moving gameplay tests rather than explaining away large workload differences through normalization.
If a candidate successfully lowers its intended micro-metric but does not improve clean wall time, it is not a successful optimisation.
Additionally, do not optimise waits only because they appear prominently in a profile. PSP software is expected to sleep when no useful work is available; semaphore, event, GE or VBlank waits are only performance problems if they delay the frame's critical path.

Fresh post-optimisation gameplay profiling builds and capture instructions are
in `docs/codex/zoness_reprofile.md`. Keep clean audio-off/on timing references
alongside phase/frame traces and hardware counters; gprof alone cannot rank
simulation/effects costs reliably. Title-only component markers add no useful
gameplay attribution and stay disabled. Preserve the matching ELF and full
symbols with every raw capture, and record source/configuration fingerprints.

## Prefer Exact Specialisation Over General Machinery

PSP wins in this project have often come from exploiting a proven invariant rather than introducing a more general optimisation framework. Examples include exact unchanged-light suppression, precise material-classification invalidation, direct construction into the final vertex stream, compact texture conversion specialised around the actual source representation, and explicit no-op state suppression.

A specialised fast path is attractive when its predicate is cheap, exact and overwhelmingly common, and when failure immediately falls through to the established path. Do not generalise a solution merely because a reusable abstraction appears cleaner. On PSP, abstraction often has measurable costs in code footprint, calls, metadata and cache traffic.
First prove the common case. Generalise into n64psp only after the primitive has demonstrated that its interface and setup costs remain appropriate for real N64-port workloads.

## VME Audio

`sf64-psp` is the first PSP game to attempt a custom, real world implementation of the Virtual Mobile Engine (VME). When experimenting with and implementing VME code, be mindful of this - ensure findings are well documented. It is imperative that new agents should be able to pick up from this work and avoid repeating experiments and mistakes.
Search ONLY mcidclan's repositories as VME samples and documentation - `mcidclan/psp-virtual-mobile-engine-ext` for samples and `mcidclan/psp-media-engine-cracking-the-unknown` for low-level information about the VME chip. The VME is a powerful reprogrammable chip that has proven to have potential to speed up audio processing, so consider it in relation to audio.

## Textures

Texture churn is a real problem that has not yet seen a good solution. A Zoness test run from September 27 recorded 33,197 converted-texture uploads and 33,176 evictions, plus 14,651 RGBA16 uploads. Increasing texture cache size from 128 to 255 reduced texture reloads, but produced more reservation work and thus no task time win. 

Check whether every cache-key field affects decoded pixels before changing cache
capacity or eviction policy. Moving-map captures show 1,456 uploads versus 16
with a stationary camera; the worst task spends 102.295 ms preparing textures
and only 0.053 ms waiting for GE completion. RGBA and IA requests incorrectly
carried the last loaded CI palette, creating identical copies as planet order
changed. Requests now include a palette only for CI textures. Production-cache
host tests reproduce the churn and eliminate these false variants; PSP timing
acceptance remains pending. See `frame_interpolation.md` for the captures.

Baked IA8/RGBA32 texture colours use only RGB. Their requests now mask off
primitive/environment alpha, which remains in material and vertex state.
Alpha-only setters already retained the current texture handle; this change
prevents duplicate variants when a texture is requested again after other
state or texture changes. PSPGL already normalised these colours internally.
The production-cache ASan/UBSan regression sweeps both alpha channels across
32 format/dimension/mirroring/premultiplication cases: each produces identical
bytes with 4 uploads instead of 2,048, and RGB changes still produce different
textures. This synthetic result does not establish the current Zoness saving.
GU and shared PSPGL builds pass. The user reported no visual side effects and
Zoness running well, and accepted the exact redundancy removal without a new
matched timing comparison. The normalization is retained unconditionally;
no numerical scene speedup is claimed. Binaries and exact commands are in
`build/experiments/gu-texture-alpha/`.

### References

- "The Naked PSP" (TyRaNID, 2006): https://uofw.github.io/upspd/docs/software/naked_psp.pdf
- "PSP Media Engine Cracking the Unknown" - mcid's low-level VME and ME research: https://github.com/mcidclan/psp-media-engine-cracking-the-unknown

## Warm Title Audit — October 2026

The October 3 GU Title capture has one texture creation and no evictions across
606 counter frames. Its task mean is 17.539 ms, frontend 17.461 ms and final
Submit/sync 0.064 ms; frame intervals average 19.126 ms. Do not import Zoness
texture-churn priorities or infer GE utilisation from the short final wait.
The phase build measures 55.513 ms and changes some lookup paths, so its nested
phase times cannot price release savings. Counter `one_vblank` buckets round
durations and do not prove 16.667 ms deadlines were met.

The matching ELF's captured static lists account for all 196 loads and 2,900
vertices per task, with no unreferenced load generations or repeated source
vertex addresses within a list. Source rebatching is therefore not the obvious
win it was for water. Mixed-cull TRI2 plus other direct triangles cover 24.3% of
direct output vertices, making three-vertex colour-mode hoisting a bounded lead.
GE UV scale/offset is another hypothesis, provided coefficient state does not
fragment material batches and reduced GE precision preserves sampling. Neither
has measured savings yet. See `gu_title_profile_review.md` for evidence and gates.

The mixed-cull three-vertex colour hoist passed 37,175,872 exact host comparisons
but lost on PSP: Title baseline 16.3–16.5 ms versus candidate 16.5–16.6 ms.
Its helper grew by 376 bytes. The user rejected it and the helper was restored;
no generic TRI1 colour change was made. Arithmetic removal with modest coverage
still needs a clean hardware win. Artifacts are in `build/experiments/gu-title-colours/`.

The 599-frame Title state/UV census reconciles exactly. OtherMode-L has 21 unchanged
writes out of 26, but classification/depth/fog resolve only 9/7/7 times per task.
Write repetition alone does not price redundant consumed work. Skipping unchanged
masked OtherMode-L writes was neutral in clean PSP timing and is reverted. Its
586-frame counter capture averages 17.442 ms/task with different animation
geometry, so it does not establish a release gain. Tests passed 13,041,136 exact
handler comparisons and the dispatcher grew only four bytes: neither correct
work removal nor small code growth replaces the clean hardware gate.

UV coefficients never mix within direct-only draws. Uniform draws cover 98.95%
of direct textured vertices and 93.91% of all emitted vertices; 0.386 draws/task
mix direct and normalised fallback. This supports testing GE affine UV state
without general coefficient-driven fragmentation, while retaining mixed-draw
fallback and validating GE precision/sampling. The completed census is removed;
raw results and source snapshots remain in `build/experiments/gu-title-state-uv/`.

The isolated GE UV candidate preserves pool keys and the 24-byte vertex stream.
Mixed coefficients or normalised fallback convert the pending batch instead of
splitting it. Command-exact finite coefficients avoid parameter truncation;
GE arithmetic and texture sampling still need hardware validation. Exact host
fallback/replay checks and PSP builds pass, but the extra coefficient checks,
commands and metadata remain costs. Clean text/BSS grow by 912/3,648 bytes.
Hardware rejected this implementation: the clean candidate was about 0.5 ms
slower. Its 587-frame counters confirm 94.14% GE UV vertex coverage and only
0.491% fallback rewriting. Against the original baseline, FPU instructions fall
11.54%, but memory stalls rise 18.54%, D-cache misses 28.17% and writebacks
41.64%, with nearly unchanged geometry and final completion time. These are
separate instrumented captures, not causal timing budgets. GE setup and coverage
were adequate; unchanged vertex traffic plus repeated coefficient checks and
pool/replay metadata failed the net hardware gate. The whole candidate and its
UV counters are reverted. Artifacts and exact commands remain in
`build/experiments/gu-title-ge-uv/`. A future offload must lower producer work or
traffic, not rely on fewer arithmetic instructions alone.

## Retained mesh hardware checkpoint

Peppy RM2 improves Title from 15.5 to 14.8–15.0 ms and passes motion, edges,
state restoration and transitions on PSP. The Venom fighter stress build
compares retained drawing with accepted conventional AOT; broader coverage
awaits that result. Details and exact commands remain in asset_aot.md.

[GBAdhoc layout pinning](https://gbadhoc.rayquaza.ca/docs/layout-pinning/) suggests
profiled hot-path ordering, archive-member handling for library code, actual
load-address logs and a padding-invariance check. Its heap JIT mechanism is
different from SF64's retained data cache. Do not copy its offset or treat its
results as an explanation of SF64's unresolved layout cost.

Venom now passes the stress test: RM0/1/2 20.0/21.1/18.6 ms, H30 F0 T720,
all visual/transition checks passed. This is 1.4 ms beyond conventional AOT.
The fighter-only Title is 16.0 ms in every mode; it excludes retained Peppy,
and a common binary cost remains. All 62 common Title leaf bodies compare
identically between these two objects. The paired control enables only the two
validated leaves through one shared adapter and two bounded cache owners.

Paired hardware confirms 0.5 ms Title and 1.5 ms Venom gains (16.1→15.6 and
20.2→18.7), H30 F0 T720 in Venom. External Title remains roughly neutral
against the older 15.5 ms reference. The completed RM test controls are removed
and a clean two-leaf GU candidate is provided for hardware confirmation. Keep
the common binary tax unresolved and do not expand coverage yet.

Clean retained GU regresses Title to 16.2–16.3 ms; reject it as the marker.
Cleanup changes four graphics instruction streams, including one Title leaf,
while retained build/draw helpers and GU backend bodies stay equivalent.
A single control runs clean code at paired function/data offsets and preserves
allocated extents. Absolute PSP load addresses remain unmeasured. Price that
control before concluding layout is the cause or attempting profile-based pins.

Do not transplant function bytes independently of compiler-owned data or IPA
calling conventions. The cleanup control corrupted visuals and crashed because
it retained old interpreter jump-table offsets with a different body. That
control is withdrawn and proves nothing about layout. The replacement links
complete clean objects into reserved text slots and verifies all jump-table
targets against their original compiler addends. Subsequent hardware results
validate the full-object approach below.

Full-object linker pins recover Title to 15.5 ms and pass visuals/transitions.
Venom 18.7–19.3 ms observations are unmatched; do not infer an exact delta.
Peppy-only remains faster at 14.8–15.0 ms. The next profile-derived candidate
groups shared TnL/state/texture work, fixes 200 function starts, and records
runtime anchors at startup. Archived counts/census are useful provenance;
gprof samples and inferred retained-path weights do not predict cache misses.
Hardware must decide whether the order improves the proven reference.

Profile-derived full-object linker reservations improve hardware Title to
14.6–14.8 ms; Venom is approximately matched at 18.7 ms. Normal release GU
builds now generate and verify the same 200 function starts/sizes. Padding
2 KiB cold text plus 4 KiB read-only data preserves those pins. Pinning fixes
relative placement, not physical cache residency; data and unpinned code can
still move. The integrated build removes the cold address logger and is now
hardware accepted: Title 14.4–14.6 ms, Venom observed at 18.5 ms on
7 October 2026. Use this normal GU build as the next baseline; Venom workload
matching remains approximate. No further layout changes are needed for promotion.

A small retained registration table inside the interpreter unit can still
change unrelated inlining: the Fox addition grows the matrix handler beyond
its pinned slot. A separate shared registration unit restores the matrix and
texture helper sizes, preserving all 200 starts. Keep registration growth
separate from renderer compilation; slot checks remain essential. The Fox
coverage candidate adds 100 text bytes and one 38,064-byte lazy cache.

The full Title model experiment registers 73 accepted immutable leaves with
the same retained representation. Packed N64 vertex sources need only 8-byte
alignment when copied into aligned retained storage; validate the real linked
alignment rather than aligning every test payload to 16 bytes. Bound the fixed
cache workspace by bytes: the current experiment caps it at 3 MiB, with about
2.69 MiB used if all 74 entries including Venom are loaded. Compact storage is
needed before corpus expansion; hardware must price small-limb coverage.

Full Title retention loses at 14.9–15.1 ms; large-leaf retention reaches
14.1–14.3 ms with correct visuals, but Venom drifts to 19.5 ms. Do not promote
from Title alone. Forty-four Title leaves save no position transforms under
eager final-slot restoration. Start qualification with avoided transforms and
final-slot fraction rather than triangle count: the proven Venom fighter has
only 24 triangles. A 32-saved/one-third-final heuristic reproduces the seven
winning selections here; broader thresholds require hardware evidence. Remove
completed activation controls and verify new header prerequisites and ELF sizes.

Removing the completed activation control yields Title 13.8–13.9 ms and Venom
19.0 ms on 7 October 2026. Use the clean seven-leaf build as the next reference.
All 200 function pins remain fixed, while data addresses change: the earlier
14.1–14.3 ms Title and 19.5 ms Venom control still carried inactive metadata.
Do not infer exact cache causality or recovery to an unmatched 18.5 ms scene.
Broader coverage should be generated from qualified inputs and measured in both
Title and gameplay; the seven-leaf cost heuristic is not a universal cutoff.

Retained selection now uses the existing shared asset generator and manifest.
Keep cost eligibility distinct from mutation evidence: large Solar geometry can
pass a size-only rule while using mutable vertices. Generate registry data and
slot counts together, make both renderer objects depend on those outputs, and
verify linked array sizes. Four gameplay candidates add no linked text growth
to this experiment; clipping and instance frequency still determine real benefit.

Retained qualification is optional at build time. Stale or missing source
evidence, changed vertex payloads, new runtime vertex users and changed reviewed
table consumers exclude affected leaves with a warning and use normal rendering.
Unchanged qualified leaves remain enabled. An empty selection emits an inert
entry so the same renderer still builds without zero-sized arrays. A changed
setup-table contract disables only its outgoing-slot proof shortcut, preserving
vertex outputs at those calls. Keep safety checks active and never refresh hashes
automatically to silence a build error. Saved qualifications can be reviewed
later to recover coverage; ordinary source edits must not require that review
before compilation. Scratchpad verification must accept the resulting registry
count and check texture helper growth against its reserved space rather than
an enumerated list of past compiler sizes.
Generate renderer pin sizes from the current object, use earlier reservation
padding for growth first, and move following slots only when needed. Keep
alignment, non-overlap and linked-size verification; changed placement still
needs hardware performance validation.

For repeated-mesh selection, rank avoided position loads by measured gameplay
calls/frame. Aggregate source labels and exclude Title; archived peak rates
are selection evidence rather than millisecond predictions. Two reviewed
event-table aliases now form a bounded Venom experiment without relaxing the
cost rule or auditing the whole alias corpus. Fingerprint reviewed consumer
sources and reject added consumers; retain vertex and fallback checks.

The repeated-mesh experiment reports a 0.2 ms Granga-scene saving, passes
visual checks and is promoted at the user's request. The default registry has
13 retained leaves. Keep reviewed render-table consumer checks active after
hardware acceptance; the tank has no separate timing measurement yet.

Deferred final-slot positions preserve load-time colours/UVs and matrix leases,
then resolve only CPU-consumed slots. Ordinary VTX overwrite and task reset
discard work. The first prototype keeps thirteen meshes and the 72-byte vertex
ABI. Test mixed-projection inheritance and drain batch queues when comparing
emission; a mock that only accepts ordinary projected output misses this case.
One compiler-grown Title leaf needs a new reservation while 199 starts and the
8 KiB core remain fixed, so hardware timing is not a fully isolated layout test.
Refresh archive timestamps after recursive builds and verify linked library
code against current objects; an empty prerequisite recipe can retain stale
ELF code on the first build after a library update.

The deferred-position prototype regresses to 14.8 ms Title and 20.7 ms Venom
and is rejected. Restore the committed thirteen-mesh baseline before further
research. Preserved function starts do not prove stable instruction/data cache
behaviour, and one moved function prevents clean attribution. Stop speculative
mesh changes; establish renderer-stage costs and repeatability with a fixed
workload before choosing another architecture change.

For native-mesh bounds checks, inspect the linked Allegrex path before assuming
matrix work reaches the VFPU. A proven affine modelview plus point W=1 removes
11 multiplies and 3 adds per corner while projection clip W still requires its
full calculation. The direct version also hoists matrix loads, but its larger
callee-save set and 364-byte helper growth can offset that work. Use one
same-binary comparison and hardware counters before promotion. A measured
placement effect does not establish I-cache causality. On the same A/B EBOOT,
the owner reports matched bounds counts and BM0/BM1 at 14.6–14.8/14.3–14.5 ms
in Title, 20.9/20.8 ms in paused Venom and 18.2/18.2 ms in Zoness. This supports
a small Title gain, a marginal Venom change and no Zoness change. At the owner's
direction, BM1 is promoted as the production bounds path in a separate change;
the A/B switch and native-mesh HUD rows are removed. The tested EBOOT remains
preserved. Hardware visual/fallback checks were not separately recorded.

The outgoing-slot proof begins after each retained draw and scans at most 32
commands across the live caller stack. Reads before overwrite, unknown list
effects, malformed loads, and depth/budget limits keep restoration. Its result
depends on the current continuation stack, mesh output mask and remaining task
budget. A profile-only census EBOOT now records aggregate proof work and repeated
command addresses in the hardware CSV; its timings are not comparable because
the counters add work to the measured build. Audit repeated addresses for stable
source and command contents before choosing a reusable summary.

The caller census resolves those addresses to `masterDL` inside the two rotating
`gGfxPools`, so address reuse across a capture does not make a continuation
immutable. Title examined 543 commands and 201 registry targets per frame;
Corneria examined 121/56 and Venom 234/94. All 436 Title addresses stayed
unchanged during its capture, while 94–96% of repeated gameplay addresses changed
at least once. Measure same-task reuse before adding a cache; the current totals
combine visits across frames and do not identify identical continuation stacks.

## Settings overlay boundaries

Keep controller polling on the simulation scheduler thread and publish bounded
snapshots to the serial and render threads. Apply display changes only after the
active graphics task completes and before generating the next simulation task;
reset interpolation history and invalidate HUD replay together. Append GU menu
sprites after the scene flushes and before the normal Submit. Closed-menu CPU
performance text needs the completed back buffer before swapping; PSPGL overlays
belong in the pending draw stream. Capture file writes run after graphics
completion on the scheduler thread. Cached host raster timings cannot price
uncached PSP VRAM blending: the owner observed a large slowdown with the CPU
menu, so menu rectangles and antialiased text now use GPU sprites and immutable
RAM font textures. The owner confirmed the performance fix on a physical PSP
with the GU backend.

Runtime audio starts Off and skips synthesis without restarting the sequencer or
output thread. Existing PCM drains before silence; enabling audio resumes the
retained audio clock rather than replaying missed ticks. Only the audio scheduler
changes mixer ownership after ME completion. CPU-to-ME handover writes back the
mixer and invalidates it on ME before execution; ME-to-CPU handover invalidates
the CPU copy. PCM queues stay shared. A timed-out ME still running must block
synthesis and buffer reuse even if its pending-job flag has already cleared.
Off and blocked handovers wait on VI because no PCM backpressure can pace them.

## Unified PSP graphics backend

The normal PSP build is one EBOOT containing the GU and PSPGL backends. The
SELECT settings overlay saves `sf64-settings.cfg` beside the EBOOT and applies
the chosen backend only at the next launch; missing or invalid settings default
to GU. The startup dispatcher binds one backend before graphics initialization,
and device initialization touches only that backend. Audio mode remains a
separate live setting.

`make psp` builds the unified EBOOT. `PSP_GFX_BACKEND=gu` and
`PSP_GFX_BACKEND=pspgl` remain useful for standalone builds. In the unified
build, Fast3D calls use the selected operations table; draw submission remains
batched, while individual texture and draw-state operations add an indirect
call. The PSP build validates the production placement plan, retained-mesh
qualification, and GU scratchpad relocation with both implementations linked.
The unified plan grows the `PspGfxDl_Run` slot from 704 to 768 bytes and moves
later pinned functions by 64 bytes. The standalone GU plan also grows the
direct-call MTX handler slot from 1408 to 1472 bytes for its 1424-byte function;
its pinned end moves 128 bytes from the original GU plan. Hardware A/B timing
and PSP-1000 heap measurements are still required before claiming GU
performance parity.

The [matched-build 60 Hz census](codex/gu_60hz_census.md) finds current Run slots
are 768 bytes in both plans; standalone's larger matrix slot moves 122 later
section starts by 64 bytes relative to unified. Unified also outlines the HUD
anchor helper and changes several frontend bodies. These are host findings,
not evidence that dispatch or I-cache explains the remembered Title delta.
Phase/hardware-counter builds disable production pins and scratchpad execution;
use clean A/B binaries for parity and the optional coarse census for delivery
accounting. Render-only tasks still traverse Fast3D, while missing-history
fallbacks can deliberately keep the displayed image without rendering a task.
Completed buffer swaps are not physical scanout timestamps.


The [Aquas Fast graphics candidate](codex/psp_fast_graphics_aquas.md) replaces only
Aquas effect 346 in optional GU Fast mode. Capture 000 from 25 October has a
10.339 ms effect mean in its first 300 tasks, but 4.915 ms across all 1789 tasks;
those scoped diagnostic times are not recoverable production savings. The orb
is already a quad: ordered native runs target repeated interpretation/state work.
Keep frame-owned interpolated matrices, the four tints and opacity, then restore
inherited material and vertex state. Diagnostic tags split runs, so price clean
batching separately. Accurate/Fast and a separate skip build were prepared for PSP acceptance.

The first owner Fast screen submitted 85 particles in 14 draws without fallback,
but displayed no flecks. Native UVs were incorrectly in pixels; the existing GU
vertex contract uses normalized coordinates, so clamp sampling hit transparent
atlas edges. Correct U/128 and V/32 and test actual-texture sample alpha, not just
vertex counts. The invisible candidate's timing reduction is not acceptance.

The corrected 26 October owner screen accepts the Aquas substitution: matching
visuals, Accurate 42.5 FPS / 21.3 ms versus Fast 57.9 FPS / 15.6 ms, P99 D4 F0 O0.
This is a 5.7 ms gain in that view, with 99 particles in four native draws and no
fallback or capacity failure. Keep the frozen accepted EBOOT; neither the older
invisible screen nor a single rounded result establishes a sustained 60 Hz lock.
Do not expand Fast mode to other effects without their own evidence.

After acceptance, remove Aquas overlay counters, per-task particle statistics,
capture exports and the diagnostic skip switch. Keep the accepted binary for
comparison and recheck the clean candidate in the same view on hardware.

Aquas native batching is now standard GU rendering in Accurate and Fast. Hardware
acceptance found no visual compromise, so it should not depend on quality. The
`PSP_FAST_AQUAS` build flag is removed; PSPGL and failure paths retain the original
renderer. The historical 5.7 ms result measures the representation change, not
a current Accurate/Fast difference.

The [Corneria Boss 2 investigation](codex/corneria_explosion_optimisation.md)
identifies effect 357's metal quad subtype in both main and reflected passes,
not the already retained six-triangle debris meshes. Its first 60 diagnostic
tasks average 10.24 ms for 357 across both passes. The separate actor region is
detailed Arwing rendering; the carrier has its own boss category. The later
sphere also comes from effect 395 state 12 as well as 383, and has no reflected
geometry. Measure these workloads independently before replacing rendering;
frontend timing does not establish the large blended sphere's GE pixel cost.

The [stage-one debris skip experiment](codex/corneria_debris_skip.md) now has
independent main/reflection controls and a debug Corneria checkpoint start.
Validated skipped runs keep only the last object's state-only commands while
preserving simulation and matrix records. Clean and coarse EBOOTs are prepared;
the hardware skip ceiling and visual-state checks were the first review gate.
Effect-tagged captures deliberately split runs; use clean GFX timings
and the coarse census for counts.

The viewport FPU fix in `53ff2f75` changes several compiled helper sizes when
outlining shared viewport resolution. The reviewed layout adapter expands two
slots using existing padding, moving 23 pins back 64 bytes without growing the
pinned extent. Texture preparation is now 1,228 bytes: scratchpad code totals
4,736 bytes with 5,248 bytes of guarded storage. Keep explicit size and
relocation checks; a small source fix can change distant compiler decisions.
Both Corneria EBOOTs rebuild with all 198 pins and ten scratchpad routes verified;
matched production timing for the placement change remains unmeasured.

The owner's 26 October retest `census-000` records Original/Original throughout:
up to 40 eligible metal fragments per pass, 32+8 runs at that population and
zero fallback. Debris-bearing tasks average 23.328 ms GFX in the coarse build.
This confirms adjacency and the validated source pattern on PSP, not a skip
ceiling; the changing recovery interval cannot substitute for a matched Skip run.

Retest `census-001` confirms Skip/Skip with zero fallback and exactly reconciled
command removal. Debris-bearing mean GFX is 19.021 versus 23.328 ms Original;
balancing exact per-pass counts, runs and presentation type gives a 4.760 ms
descriptive difference. Independent runs still differ in camera/other effects,
and the owner has not checked visual isolation. This supports the debris trial,
but clean matched-cue timing and skipped-state visual acceptance remain unverified.

The owner authorised the [native debris experiment](codex/corneria_debris_native.md)
on 26 October. Developer controls now cycle Original, Skip and Native separately
for main/reflection, independent of Accurate/Fast. Native consumes bounded,
frame-owned matrix records and uses the original quad, texture and lighting;
clipped or unsupported runs replay original bodies. Replay final vertex slots
as well as material state. Census records actual native coverage and fallback,
because eligible counts alone do not prove batching occurred. The clean/coarse
renderer objects match; eleven scratchpad routes and all layout checks pass.
Native performance and visual acceptance require PSP testing before promotion.

The first native-debris launch exposed a separate undefined gameplay read:
`ActorMissileSeek_Update` compared an uninitialised local steering delta when
banking without a steering update. Initialise the delta before the branches;
masking FPU exceptions or changing layout would leave the bug intact. The host
regression uses NaN auto-initialisation and checks floating-point invalid flags,
and rejects the original function while passing the corrected one.

The native-debris retest records 23.486 / 18.881 / 23.982 ms mean GFX for
Original / Skip / Native on debris-bearing tasks. Native has no demonstrated
overall win. Only 50.97% of eligible instance visits use Native; a single clipped
corner rejects the whole run, with particularly low middle-phase coverage.
The dense 33–40 population band achieves 100% coverage and lower descriptive
GFX time, but the scene is not fully matched. Do not equate run-fallback counts
with individually clipped fragments. Split native runs around original-path
fragments before another narrow A/B; visual acceptance is still unrecorded.

## PSPLink smoke flow — 2 November 2026

The one-command smoke helper is `scripts/psp/psplink_smoke.sh`. With PSPLink
running on the PSP, PSP Type B shared through usbipd, and the PSPDEV udev rule
installed, it builds an isolated unified PRX, attaches the device to WSL if
needed, launches through `host0`, captures a BMP after eight seconds, and records
hashes and logs. The 2 November run began with the device already attached to WSL;
it reached the Star Fox title screen and produced a 480×272 screenshot. This
verifies delivery and startup, not a benchmark result; gameplay input and
profiler capture are not automated yet.

PSP Type B is USB ID `054c:01c9`; use the bundled
`/home/dwelch/pspdev/share/psplinkusb/50-psplink.rules` for WSL device access.
`reset` returns to the PSPLink bootstrap and disconnects USB while the device
re-enumerates; reattach it with `usbipd` before the next run. `exit` leaves
PSPLink. The smoke script leaves the game running after it saves the capture.

## Hardware performance phase one — 3 November 2026

The direct PSPLink runner can start test-hook builds at a selected gameplay
scene, capture 1,800 low-instrumentation GU tasks, and retrieve the raw profile
and screenshot without extending the smoke script. Keep runtime audio state
separate from the audio compile flag: these captures built audio code but RAM
inspection confirmed requested and active audio were both Off.

Measure completion-to-completion intervals after `sceGuSwapBuffers` returns,
and sample `sceDisplayGetVcount` for the exact intervening VBlank pulses. These
are software Present completion timings, not LCD scanout timestamps. The PSP
hardware counter source was unavailable, so combined `sceGuFinish`/`sceGuSync`
CPU time must not be described as GE execution time. Run `PROFILE_PHASES`
captures separately: in Bolse, phase instrumentation raised mean task time
from about 14.4 ms to 26.6 ms while still providing useful simulation and
render-only counts.

The Bolse reference measured about 1.24 ms/task of retained vertex preparation
with about 15 admissions per task. Even eliminating all measured preparation
would miss the 2 ms architecture gate, so this evidence did not justify a
persistent retained vertex pilot. Aquas bubble draw omission removed about
0.57 ms/task of frontend work, but changed mean Present completion by only
0.019 ms and worsened p95 by 0.20 ms. Local producer or renderer savings do not
guarantee better delivered frame timing; keep end-to-end intervals as the
decision metric.

## Attribute retained meshes by asset and view — 3 November 2026

A retained asset is not automatically admissible from every camera position. In
Bolse, `aBoBaseDL` fell back on bounds for all 276 center visits, while 250 of
299 middle visits were admitted. The opaque body consumed 1.581 ms of center
frontend time, including 0.645 ms of CPU vertex preparation. The complete body
span is the upper bound for that class; do not add the nested vertex timer or
attribute the enclosing boss span to it. Keep conservative clipping fallback
until a hardware path is verified against the original output.

Put profiling markers around the measured asset and its neighboring effects,
and record retained rejection reasons by position. Capture requested and active
runtime graphics and audio modes in the profile itself. A HostFS-launched PRX
needs its settings file beside it; absent settings can change the rendering
policy and invalidate the intended comparison.

## Use whole-frame timing for decorative draw ceilings — 3 November 2026

In Bolse Fast mode, omitting the entire dynamic-ground draw while preserving
each tile's RNG call removed 0.832 ms of ground-source frontend work and
improved the matched completed presentation interval by 0.807 ms. It still
averaged 18.954 ms and visibly removed most of the ground. Treat this as an
upper bound, not a rendering policy. The smaller half-density ground policy
did not improve the A/B/B/A mean. Keep these changes test-only unless a smaller
visually acceptable policy demonstrates an end-to-end benefit on hardware.
See `docs/codex/hardware_performance_phase1.md` for profiles and screenshots.

## Consolidated PSP performance assessment — 4 November 2026

[PSP performance: architecture, evidence and remaining limits](psp-performance-state.md)
consolidates the historical `SF64PROFILE` captures, accepted optimisations and
the November hardware accounting. It distinguishes renderer regions from
recoverable work, GE replay from normal occupancy, and software Present
completion from physical display timing. Use its evidence boundaries before
reopening a concluded experiment.

The Bolse alternating-beam experiment is rejected: those beams are collision
hazards, and its compile override also suppressed them in Accurate. Preserving
simulation while hiding a damaging object's visual warning is incorrect. The
measured saving is not an accepted optimisation. Stable per-object identities
are also required for repeated interpolated draws: changing the visible members
without changing their count can pair different ground tiles by emission order.
The isolated correctness repair still needs moving PSP visual acceptance before
a corrected performance baseline is collected.

## Reserve PSP system memory with the heap threshold — 5 November 2026

The installed PSPSDK treats any negative `PSP_HEAP_SIZE_KB` as automatic sizing;
the negative magnitude is ignored. Its `_sbrk` subtracts
`PSP_HEAP_THRESHOLD_SIZE_KB`, defaulting to 512 KiB when that symbol is absent.
Changing `-1024` to `-2048` therefore left the same 48-KiB free block at failed
Ad-Hoc network initialisation. Use an explicit threshold to reserve memory for
firmware modules, threads and networking, and verify its value in the linked ELF.
The corrected two-MiB threshold reduces the libc heap by 1.5 MiB against the
effective old default; it does not demonstrate a speedup or successful radio
operation. Recheck game allocations on PSP-1000. The installed runtime
disassembly and [SDK source](https://github.com/pspdev/pspsdk/blob/master/src/libcglue/glue.c)
agree on this behaviour. See [the multiplayer review](psp-adhoc-multiplayer.md).
