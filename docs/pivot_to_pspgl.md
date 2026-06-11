# PSP Renderer Direction

## Current Status

The renderer pivot is complete at the build-system level. PSPGL is the active
and only renderer compiled by the standard PSP targets:

```bash
make psp
make bootstrap
```

The original native GU/RSP/RDP experiment remains in the repository as
research material, but it is no longer selectable and is not part of normal
builds.

## Long-Term Architecture

The project should retain one Star Fox 64 / Fast3D display-list frontend and
support multiple PSP rendering backends beneath it:

```text
Star Fox 64 display lists
    -> shared Fast3D/RSP/RDP interpretation and draw batches
        -> PSPGL backend
        -> future native sceGu/GE backend
```

PSPGL is the primary renderer, not a throwaway prototype. A future native GU
backend should coexist with it for profiling, testing, education, and hardware
comparison. It should reuse the shared frontend rather than revive the old
standalone interpreter.

## Why The Original Renderer Was Retired

The first native renderer proved that the PSP toolchain, framebuffer,
presentation, display-list traversal, texture conversion, starfield batching,
and real-hardware deployment all worked. It also exposed how quickly semantic
debt grows when matrix state, vertex-load-time lighting, texture state, combine
modes, clipping, depth, and alpha are implemented inside one PSP-specific
interpreter.

PSPGL overtook that renderer in stability and recognisable output while using
a cleaner Dreamcast-style separation. The legacy implementation and its
lessons are documented in `docs/legacy_rsp_renderer.md`.

## Reference Priority

The Dreamcast `sf64-dc` GLdc renderer remains the highest-priority architectural
reference. Its useful shape is:

```text
Star Fox 64 display-list interpreter
    -> small rendering API
    -> GLdc backend
    -> Dreamcast presentation
```

The analogous PSP shape is:

```text
Star Fox 64 display-list interpreter
    -> small rendering API
    -> PSPGL backend
    -> PSP presentation
```

Modern Starship/libultraship may remain a semantic reference, but importing it
wholesale is not required for the current renderer.

## Development Policy

* Keep PSPGL diagnostics and display-list statistics useful and bounded.
* Do not add renderer-specific gameplay or title-content gates.
* Keep the retired native files available for study until their useful lessons
  have been captured.
* Add a native GU backend only beneath the shared frontend.
* Maintain both PSPGL and any future GU backend as first-class comparison
  targets once that backend exists.

This dual-backend goal is part of the port's research and educational value:
other PSP projects should be able to compare a GL-driven implementation with a
native GU implementation of the same game-facing renderer contract.
