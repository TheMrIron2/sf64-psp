# PSPGL Backend

## Role

PSPGL is the active renderer for `sf64-psp`. Standard full and bootstrap builds
compile the same PSPGL renderer surface:

```text
src/psp/gfx/gfx_psp.c
src/psp/gfx/gfx_psp_dl.c
src/psp/gfx/gfx_pspgl.c
src/psp/renderer_pspgl.c
```

The backend has progressed well beyond its original clear-screen and triangle
probe. It now consumes the real `Gfx*` task pointer, traverses nested display
lists with safety limits, interprets a growing Fast3D/RDP subset, converts and
caches several texture formats, submits textured and vertex-coloured geometry,
and supports title-screen starfield, depth, combine, and lighting bring-up.

## Structure

```text
src/psp/gfx/gfx_psp.c
    EGL/PSPGL context, frame lifecycle, and presentation

src/psp/gfx/gfx_psp_dl.c
    Star Fox 64 display-list frontend and renderer state

src/psp/gfx/gfx_pspgl.c
    PSPGL texture cache, client arrays, and fixed-function draw state

src/psp/renderer_pspgl.c
    public PspRenderer_* bridge and PSP-specific starfield batching
```

This follows the Dreamcast renderer's architectural lesson without importing
the full Dreamcast implementation wholesale.

## Diagnostics

The display-list frontend intentionally retains concise task statistics,
including command, vertex, triangle, nested-list, matrix, texture, texrect,
unsupported-opcode, and traversal-limit counts. These diagnostics are part of
the port's educational value and should remain bounded rather than removed.

Per-command logging should be used only for short, targeted investigations.

## Future Native Backend

A native sceGu/GE renderer remains a desirable future backend for comparison
and measured optimisation. It should implement the same backend-facing draw
contract used by PSPGL. PSPGL should remain available so behavior, performance,
and implementation tradeoffs can be compared on PPSSPP and real hardware.

The retired `src/psp/renderer.c` experiment is not that future backend: it owns
too much display-list interpretation itself. Useful PSP-specific techniques may
be recovered from it, but the next GU backend should share the PSPGL-era
frontend.

## Build

```bash
make psp
make bootstrap
```

There is no active renderer selector. PSPGL and its dependencies are required
for PSP builds.
