# The Corneria Streak: a Post-Mortem

*How the port's longest-lived visual bug turned out to be a single unpainted
column of pixels — and why every tool built to find it was blind by
construction.*

---

## The symptom

During the Corneria intro (the flight over the ocean, before "open the
wings"), thin bright vertical streaks appeared on screen: one pixel wide,
spanning the full frame height, cutting through sky and sea alike. They
appeared at deterministic moments (the camera pan up to the fleeing ships;
the instant the wing-open dialogue closed), persisted for a few seconds,
and vanished. They reproduced identically on PPSSPP and on real PSP
hardware. The original N64 game never shows them.

It looked exactly like a classic geometry bug: a degenerate or collapsed
triangle being rasterized as a line. That resemblance cost roughly two
dozen investigation rounds.

## The hunt, in phases

### Phase 1 — "a cloud is collapsing" (the geometry era)

A renderer-side detector that scanned every batched triangle for
thin-and-tall shapes (`[sliver-mid]`) immediately found matches at the
streak moments: full-screen-height slivers carrying the **cloud** material
(`aPlCloudsTex`, prim color `90ffffff`). The obvious theory: the cloud
billboard's transform was collapsing its X axis.

That theory survived an embarrassingly long time because every attempt to
confirm it *almost* worked:

- Captured cloud matrices were always **perfect billboards** (uniform
  scale, exact `RotX(90°)` rows) — explained away as "we captured the
  wrong cloud instance."
- The game-side math was audited repeatedly: `Corneria_SpawnClouds` →
  `Object_SetMatrix` → `Effect_Clouds_Draw` uses only trivial angles and
  uniform scale. Authentic code *cannot* produce anisotropy. Explained
  away as "the port's matrix pipeline must corrupt it."
- The cloud spawn cadence (one every 32 frames) matched the streak
  recurrence (~31 frames). Compelling — and pure coincidence.

Suspects indicted and acquitted in this era: the VFPU matrix multiply, the
VFPU vertex-transform batch routine (validated instruction-by-instruction
against the scalar path), the modelview cache and its serials, the
software clipper's fan triangulation, `Matrix_ToMtx` fixed-point
round-trips, behind-camera vertices leaking through the perspective
divide, exact-on-frustum-edge vertices re-clipped by the GE, texture
rectangles, and the `TexturedLine` contrail renderer.

Some genuinely useful hardening came out of this era even though none of
it was *the* bug: a missing `G_SETSCISSOR` implementation (which fixed a
different bug — the black water outline), a degenerate-triangle cull
(Corneria emits 600+ zero-area triangles per frame; the PSP GE rasterizes
them as lines, PPSSPP discards them), and a dedicated `w <= 0` guard plane
in the clipper.

### Phase 2 — the instrumentation war

The detectors themselves became the story. On a real PSP there is no
debugger and no `printf("%f")` (floats logged as `value*1000` integers),
so evidence came from capped, deduplicated log lines — and the caps were
drained, run after run, by *benign* thin geometry: the title-screen arc,
the Arwing's ground-flattened shadow, close-range silhouette edges, the
map screen's route line. Eight separate budget-drain mechanisms were
found and patched across the runs. Each fix produced a cleaner run that
disproved the previous run's conclusion.

The turning point of this phase was a shape-gate-free dump of **every**
cloud vertex load (`[cloud-quad]`): every single cloud, at every streak
moment, was a mathematically perfect billboard (`xImg` exactly twice
`zImg`, purely axis-aligned). Better yet, the flagged "sliver" was
reconstructed by hand through the Sutherland–Hodgman clipper and matched
to three decimal places: it was a benign **fan slice** of a huge,
healthy, mostly-off-screen cloud — the quad's *diagonal* crossing the top
frustum plane, not a collapsed edge. The clip-fan slices tile the polygon
seamlessly. The detector had been flagging correct geometry for twenty
rounds.

The clincher: a build that skipped every cloud batch entirely
(`PSP_DIAG_SKIP_PLCLOUDS=1`). **The streak persisted on a screen with no
clouds.** The correlation had been coincidence all along — the clouds were
merely on screen at those moments, and the sky texture happens to *be*
clouds.

### Phase 3 — ground truth (the GE debugger)

With inference exhausted, the PPSSPP GE debugger (Ctrl+G, Step
Frame/Draw/Prim) provided what twenty diagnostic builds could not:
per-primitive observation. Even here there were two last false leads:

1. Pointer-like words in the display list decoded as `BBOX_JUMP` and
   `Texture V scale: -0.0` — structurally identified as pspgl's own
   `struct pspgl_dlist` headers sitting between command buffers, never
   executed. (A GE running through one *would* have corrupted texture
   state — a lovely theory, wrong.)
2. The streak seemed to be "drawn" by the frame's very first primitive —
   the depth-only clear. In fact the port never clears the color buffer,
   so the streak visible at frame start was **stale color from previous
   frames**. The real question became: *why is that column never
   overdrawn?* The depth buffer was clean, ruling out z-poisoning.

The answer came from stepping primitives and reading the Verts tab. The
frame draws, in order: depth clear → four black letterbox bars → a
full-playfield fill rect in the background color `#4e5b78` → **the sky
panorama** (24 vertices, four quads). And in the sky's vertex list:

```
strip A right edge:  x = 221.4375   u = 63.97   (texture right edge)
strip B left edge:   x = 221.5      u = 0.00    (texture left edge)
```

The two strips of the sky panorama **do not touch**. There is a
1/16-pixel geometric hole between them. Pixel column 221 — whose center
at x=221.5 lies exactly on strip B's edge, which the GE's fill rule
excludes — is painted by *neither strip*. The lighter background fill
shows through as a one-pixel, full-height vertical line.

The streak was never a primitive. It was the **absence** of one.

## The root cause

`Background_DrawBackdrop` (`fox_bg.c`) draws the Corneria backdrop DL
**twice** to wrap the panorama around the camera:

```c
Matrix_Translate(gGfxMatrix, bgXpos2, -2000.0f + bgYpos, -6000.0f, MTXF_APPLY);
Matrix_SetGfxMtx(&gMasterDisp);
gSPDisplayList(gMasterDisp++, aCoBackdropDL);        /* instance A */
Matrix_Translate(gGfxMatrix, 7280.0f, 0.0f, 0.0f, MTXF_APPLY);
Matrix_SetGfxMtx(&gMasterDisp);
gSPDisplayList(gMasterDisp++, aCoBackdropDL);        /* instance B */
```

Instance A's right edge and instance B's left edge are *mathematically*
the same coordinate, but they reach the GPU through different float
expressions:

```
edge_A = +3640 * m00 + t1
edge_B = -3640 * m00 + t2      where t2 = round(7280 * m00 + t1)
```

Those differ by about one float ulp — roughly **1/16 of a pixel** on
screen.

On the N64 this can never matter: the RSP outputs all screen coordinates
in **s13.2 fixed point** — everything snaps to a quarter-pixel grid, and
two coordinates one ulp apart quantize to the same value. The seam is
welded shut by the hardware's own imprecision. The PSP port hands the GE
full-precision floats, and the GE faithfully rasterizes the sub-pixel
hole.

The port's sin was rendering *too precisely*.

This is also why no geometry detector could ever catch it (there was no
malformed triangle to detect), why removing any suspect draw changed
nothing (the gap belongs to two *correct* draws), why it reproduced
bit-identically on emulator and hardware (same float math), and why it
moved with the camera (the seam position follows `camYaw`).

## The fix

A flush-time **seam weld** in `gfx_psp_dl.c` — a targeted re-creation of
the RSP's quantization, applied only where it can matter. The backdrop
wrap pair has a distinctive signature: a small (12–48 vertex),
non-pretransformed batch whose view-space depth is constant. For such
batches, any vertex within ~1/8 screen pixel of an earlier vertex is
snapped onto it:

```c
/* Flat and in front of the eye only (view z < 0, constant across batch). */
if ((maxZ >= 0.0f) || ((maxZ - minZ) > (0.001f * -minZ))) {
    return;
}
/* ~1/8 pixel at this depth. */
eps = 3.0e-4f * -minZ;
/* ... snap b->x/y onto a->x/y for any pair within eps ... */
```

UVs are left untouched, so each strip keeps sampling its own edge texel.
The displacement is at most 1/8 px — invisible — and the same weld closes
the identical wrap-pair seams on every other planet backdrop (Fortuna,
Katina, Venom, Versus all use the same two-instance pattern). Terrain,
HUD, and all ordinary geometry are excluded by the gates.

Confirmed fixed on PPSSPP and real hardware.

## Lessons

1. **You cannot detect a gap by inspecting triangles.** Twenty rounds of
   increasingly clever geometry detectors were blind by construction: the
   defect was in the space *between* two correct primitives. When
   detectors keep coming back clean, consider that the bug may not be an
   object at all.
2. **Correlation from shared scenery is a trap.** The streak co-occurred
   with clouds because the streak moments *are* cloudy moments — and the
   sky texture is literally a picture of clouds. Two independent
   "cloud-material" fingerprints pointed at an innocent system.
3. **Interventions beat observations.** The single most informative
   experiments were removals: skip the clouds — streak stays; skip the
   texrects — streak stays. Each A/B killed an entire theory family in one
   run, where passive logging kept producing interpretable-either-way
   evidence.
4. **Emulating old hardware means emulating its imprecision.** The N64's
   fixed-point pipeline isn't just a limitation to overcome; game content
   silently depends on it. Quarter-pixel quantization was load-bearing.
   (The same class of issue produced the GE's line-rasterization of
   zero-area triangles that N64 coverage handling hides.)
5. **Get ground truth early.** The PPSSPP GE debugger settled in three
   sessions what capped, deduplicated, budget-starved printf-style logging
   could not settle in twenty runs. On a platform without a debugger it's
   tempting to build ever-better logging; the frame debugger was worth
   more than all of it combined.
6. **The dead ends still paid rent.** The hunt produced a scissor
   implementation (fixed the water-outline bug), a degenerate-triangle
   cull, and a `w<=0` clip guard — all real correctness fixes that ship,
   even though none of them was the streak.

## Demo script (for the presentation)

1. Old build, PPSSPP, Corneria intro: pause right after the wing-open
   dialogue closes — the streak is on screen.
2. Ctrl+G → Step Prim to the sky draw → Verts tab: show `221.4375` /
   `221.5` and `u=63.97` / `u=0.00` side by side.
3. One slide of the wrap-pair code from `fox_bg.c` and the ulp arithmetic.
4. Fixed build, same moment: no streak.
