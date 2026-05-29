# PSP Renderer Pivot: From First-Principles RSP/RDP to Starship + PSPGL

## Summary

`sf64-psp` has reached an important decision point. The current first-principles PSP renderer has proven that *Star Fox 64* content can be traversed and partially drawn natively through GU, but it is now encountering rapidly growing RSP/RDP semantic debt.

The project should pivot toward using Starship’s renderer architecture, initially via PSPGL, while preserving the current native GU renderer as a legacy/research backend.

This is not a retreat. It is a maturation of the porting strategy.

## What the current PSP renderer has achieved

The current renderer has made substantial progress:

* PSP build and real-hardware iteration are working.
* Framebuffer width/presentation issues were solved.
* The title logo texrect path works.
* CI/TLUT, IA, and RGBA texture paths were brought up.
* The starfield is batched and renders.
* The “64” logo renders as geometry.
* Some title-screen 3D content now appears.
* Lighting is partially implemented.
* The renderer has useful logging and diagnostics infrastructure.

This work remains valuable. It should be preserved.

## Current problem

The latest blocker is title-screen model lighting.

Fox and the Arwing are partially drawn, but lighting does not match the intended *Star Fox 64* title screen. Several plausible fixes were tried or considered:

* raw normals,
* modelview-transformed normals,
* negated transformed normals,
* raw vs transformed light vectors,
* `G_MW_LIGHTCOL` handling,
* vertex-load-time vs triangle-draw-time lighting,
* matrix/normal-space variations.

None of these produced a substantially correct result.

This suggests the problem is not merely one lighting sign or one missing colour update. It is a deeper fidelity issue: the renderer is trying to reproduce enough of the N64 RSP/RDP pipeline to make arbitrary display lists behave correctly.

## Why the first-principles path is risky

The current renderer path requires reproducing or approximating:

* `G_MTX` semantics,
* modelview/projection stack behaviour,
* `G_VTX` vertex-load-time transformation and lighting,
* light and ambient state,
* normal transforms and possibly inverse-transpose handling,
* `G_MOVEWORD` and `G_MOVEMEM` state updates,
* combine modes,
* texture state,
* alpha/depth/blend modes,
* culling,
* clipping,
* display-list nesting and state lifetime.

Each new scene can expose another missing semantic. This creates technical debt very quickly.

The project risks becoming a bespoke N64 microcode renderer instead of a playable PSP port.

## Strategic pivot

The new renderer strategy should be:

```text
Starship renderer semantics
    -> PSPGL backend/staging layer
    -> profile
    -> improve PSPGL and/or replace hot paths with direct GU
```

This follows the Dreamcast port’s precedent of relying on the GL renderer path rather than rebuilding the N64 renderer from first principles. The Dreamcast `sf64-dc` port and its GLdc renderer should be treated as the highest-priority renderer reference before attempting to backport modern Starship/libultraship renderer code, because it already represents a legacy OpenGL-style console backend much closer to PSPGL’s likely feature profile.

## Why PSPGL first?

PSPGL gives the project a practical staging layer.

Advantages:

* It may allow Starship’s GL renderer to build sooner.
* It provides known renderer semantics.
* It gives a desktop GL reference point.
* It avoids immediately reimplementing RSP/RDP details.
* It creates a useful real-game testbed for PSPGL.
* PSPGL improvements benefit the wider PSP homebrew community.
* Direct GU can still be used later where profiling proves it necessary.

The goal is not to prove PSPGL is perfect. The goal is to determine honestly how far it can get and what needs improving.

## Proposed renderer tracks

The project should support or at least conceptually separate three tracks:

```text
legacy_rsp
```

The current first-principles native GU renderer. Preserve it as a research/reference backend.

```text
pspgl
```

The new primary bring-up target. Use Starship’s GL renderer through PSPGL.

```text
gu_native
```

A future optimisation backend. Implement direct GU only where PSPGL proves insufficient.

## Suggested build direction

A future Makefile structure might expose:

```make
PSP_RENDERER_BACKEND ?= pspgl
```

with possible invocations:

```bash
make psp PSP_RENDERER_BACKEND=legacy_rsp
make psp PSP_RENDERER_BACKEND=pspgl
make psp PSP_RENDERER_BACKEND=gu_native
```

The exact names can be changed to fit the repository, but the separation should be clear.

## Immediate next steps

1. Freeze the current renderer state in git.
2. Document the current renderer’s achievements and known blockers.
3. Add a renderer pivot document.
4. Inspect Starship’s renderer boundary.
5. Identify the smallest PSPGL integration point.
6. Build a PSPGL “clear screen” proof of life.
7. Integrate Starship renderer calls incrementally.
8. Profile before replacing PSPGL with native GU.

## What not to do next

Avoid continuing to chase the current lighting bug through more one-off variants unless it directly informs documentation.

Avoid adding increasingly specific hacks to the legacy renderer.

Avoid beginning a direct GU rewrite before testing PSPGL.

Avoid deleting the current renderer; it remains valuable as reference work.

## Success criteria for the pivot

Short-term success:

* PSPGL builds in the PSP project.
* A PSPGL context clears and swaps on real hardware.
* Starship renderer code can be compiled far enough to identify missing PSPGL features.

Medium-term success:

* Title screen starts rendering via the Starship/PSPGL path.
* Renderer output can be compared against desktop Starship.
* Missing PSPGL functionality is documented or fixed.

Long-term success:

* The game becomes playable.
* PSPGL is used where sufficient.
* Direct GU replaces only measured bottlenecks.
* The port reaches stable 60 FPS with a clean renderer architecture.

## Initial PSPGL staging backend

The first PSPGL backend is intentionally only a proof of life. It exists to verify dependency detection, PSPGL/GL context creation, clear, and buffer swap on PSP hardware.

This stage does not import Starship or libultraship yet, and it does not attempt to render game display lists. The existing native GU/RSP/RDP renderer remains the default `legacy_rsp` backend and should be kept intact as a research/reference path.

The Dreamcast `sf64-dc` port and its GLdc renderer path remain the closest external architecture reference: preserve the idea of a platform/window backend feeding a GL renderer path before replacing hot paths with native console-specific code.
