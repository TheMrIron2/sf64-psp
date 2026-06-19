# Legacy RSP Renderer Retrospective

## Summary

The original PSP renderer attempted to interpret N64/Fast3D/RSP/RDP-style display-list state directly into native PSP GU rendering. This path was valuable during early bring-up, but it is now retired from the active production path.

The active renderer direction is PSPGL, using a Dreamcast-style display-list frontend and GL-like backend architecture.

## What the legacy renderer proved

The legacy renderer established several important PSP-specific facts:

* The PSP build and real-hardware deployment pipeline worked.
* The framebuffer/presentation path required careful handling, including framebuffer width issues.
* Native batching was necessary for performance.
* The starfield should not be rendered as hundreds of individual RDP fill rectangles.
* Texture conversion, palette handling, and PSP-side upload behaviour would be central to the port.
* Title/logo display lists could be traversed and partially rendered.
* Directly reproducing N64 graphics semantics from first principles grows technical debt quickly.

## Why it is retired

The legacy renderer became blocked on deeper RSP/RDP semantic fidelity:

* matrix stack and projection behaviour,
* vertex-load-time state,
* lighting and normal transforms,
* combine modes,
* texture state,
* render modes,
* clipping,
* depth and alpha ordering,
* display-list state lifetime.

The PSPGL renderer has now overtaken it in stability, performance, and recognisable output. It renders title/menu/cutscene scenes without the same crash pattern and provides a cleaner path toward a Dreamcast-style renderer architecture.

## Lessons to carry forward

Future renderer work should preserve these lessons:

* Keep a single Star Fox 64 / Fast3D display-list frontend.
* Prefer multiple backends under that frontend rather than multiple independent interpreters.
* PSPGL is the active backend.
* A future GU/GE backend should be backend-only, not a revival of the old standalone RSP interpreter.
* Native PSP substitutions are acceptable when documented, such as the starfield batch path.
* Diagnostics should be targeted and gated, not permanent gameplay-code noise.

## Policy

The legacy renderer should no longer receive feature work.

Useful code or knowledge may be mined from it, but new rendering work should target the PSPGL-era frontend/backend architecture.

The source files remain in-tree but are intentionally absent from
`src/psp/sources.mk`. This keeps the experiment inspectable without allowing it
to constrain gameplay code or the normal build.

Inactive files confirmed during the PSPGL audit:

* `src/psp/renderer.c`
* `src/psp/renderer_diag.inc.c`
* `src/psp/renderer_starfield.inc.c`
* `src/psp/renderer_texture.c`
* `src/psp/renderer_texture.h`

## Educational Value

Retiring this implementation does not retire native PSP rendering as a project
goal. A future sceGu/GE backend should coexist with PSPGL beneath the shared
display-list frontend. Keeping both implementations will allow other PSP ports
to compare:

* GL-style fixed-function state against direct GU state,
* texture conversion and cache strategies,
* CPU interpretation cost against backend submission cost,
* PPSSPP behavior against real PSP hardware,
* portability against platform-specific optimisation.

The old renderer is useful as a record of first-principles bring-up and as a
source of measured PSP techniques. It should not be mistaken for the structure
of the future GU backend.
