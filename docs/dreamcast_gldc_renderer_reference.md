# Dreamcast GLdc Renderer Reference

## Scope

The Dreamcast Star Fox 64 port is not present in this repository. It was inspected from a shallow reference checkout of:

```text
https://github.com/jnmartin84/sf64-dc
```

For ongoing renderer work, add that repository as a nearby checkout, reference remote, or submodule so diffs can be reviewed against exact source revisions. Do not vendor it into `sf64-psp` until there is a deliberate import plan.

This note treats the Dreamcast port as the primary renderer reference for PSPGL staging. Modern Starship/libultraship is intentionally out of scope for the next milestone.

## Recommendation

Treat `src/gfx/gfx_retro_dc.c` from `sf64-dc` as the highest-priority renderer reference. The first PSPGL production renderer should adapt the Dreamcast split:

Star Fox 64 display-list interpreter
    -> small rendering API
    -> PSPGL implementation
    -> PSP swap/pacing

Do not begin by importing modern Starship/libultraship. Do not continue expanding the legacy PSP RSP/RDP renderer unless using it for comparison.

## Files Inspected

Dreamcast reference files:

* `src/sys/sys_main.c`
* `src/gfx/gfx_dc.c`
* `src/gfx/gfx_dc.h`
* `src/gfx/gfx_gldc.c`
* `src/gfx/gfx_retro_dc.c`
* `src/gfx/gfx_buf.c`
* `src/gfx/gfx_rendering_api.h`
* `src/gfx/gfx_window_manager_api.h`
* `src/gfx/gfx_pc.h`
* `src/gfx/gl_fast_vert.h`
* `Makefile`

Current PSP-side comparison points:

* `src/psp/platform.c`
* `src/psp/renderer.h`
* `src/psp/renderer.c`
* `src/psp/renderer_pspgl.c`
* `src/psp/sources.mk`
* `src/sys/sys_main.c`
* `src/psp/ultra_reimpl.c`

## Dreamcast Rendering Flow

The Dreamcast port keeps the game producing normal `Gfx` display lists through `gMasterDisp`, but bypasses the original RSP/RDP task scheduling model for graphics.

High-level flow:

```text
Main_ThreadEntry()
    -> gfx_init(&gfx_dc, &gfx_opengl_api, "Star Fox 64", false)
    -> per frame:
        -> gfx_start_frame()
        -> Graphics_InitializeTask()
        -> Game_Update()
        -> Graphics_SetTask()
             -> gfx_run(gGfxPool->masterDL)
        -> gfx_end_frame()
```

In `src/sys/sys_main.c`, `Graphics_SetTask()` still fills out an `SPTask`, but for graphics it directly calls `gfx_run(gGfxPool->masterDL)` after initial warmup. The display list passed to the renderer is the master display list built from `gGfxPool->masterDL`, the same conceptual object that current PSP receives through `task->task.t.data_ptr`.

`gfx_init()` in `src/gfx/gfx_retro_dc.c` stores two interfaces:

```text
GfxWindowManagerAPI:
    platform/window/frame timing/swap

GfxRenderingAPI:
    GL-style renderer implementation
```

Dreamcast selects:

```text
GfxWindowManagerAPI gfx_dc
GfxRenderingAPI gfx_opengl_api
```

## Display List Interpreter

The main reusable renderer layer is `src/gfx/gfx_retro_dc.c`.

Important functions:

* `gfx_run(Gfx* commands)`
* `gfx_run_dl(Gfx* cmd)`
* `gfx_sp_matrix(...)`
* `gfx_sp_vertex(...)`
* `gfx_sp_tri1(...)`
* `gfx_sp_quad_2d(...)`
* `gfx_dp_texture_rectangle(...)`
* `gfx_dp_set_combine_mode(...)`
* `gfx_sp_movemem(...)`
* `gfx_sp_moveword(...)`
* `gfx_flush()`

`gfx_run()` resets RSP-ish state, starts the rendering API frame, interprets the display list with `gfx_run_dl()`, flushes buffered triangles, ends rendering, then asks the window API to begin buffer swap.

`gfx_run_dl()` is the display-list interpreter. It handles nested `G_DL`, `G_ENDDL`, matrices, vertices, triangles, texture rectangles, tile/image setup, TLUT loading, combine mode, fog, fill rectangles, viewport/scissor, and other mode state. It is not just a GLdc wrapper; it is a Star Fox 64 oriented N64 display-list interpreter that emits a simpler GL-style draw stream.

## GLdc Entry Points

GLdc enters through `src/gfx/gfx_gldc.c`, which implements `struct GfxRenderingAPI gfx_opengl_api`.

Key API methods:

* `init`
* `start_frame`
* `end_frame`
* `finish_render`
* `new_texture`
* `select_texture`
* `upload_texture`
* `set_sampler_parameters`
* `set_depth_test`
* `set_depth_mask`
* `set_zmode_decal`
* `set_viewport`
* `set_scissor`
* `set_use_alpha`
* `draw_triangles`

`gfx_gldc.c` uses a legacy OpenGL-style fixed-function path:

* `glKosInitConfig`, `glKosInitEx`
* `glGetString`
* `glClearColor`, `glClearDepth`, `glClear`
* `glViewport`, `glScissor`
* `glMatrixMode`, `glLoadIdentity`, `glLoadMatrixf`, `glOrtho`
* `glEnable`, `glDisable`
* `glDepthFunc`, `glDepthMask`
* `glBlendFunc`
* `glTexEnvi`
* `glFog*`
* `glEnableClientState`
* `glVertexPointer`, `glTexCoordPointer`, `glColorPointer`
* `glDrawArrays`
* `glGenTextures`, `glBindTexture`, `glDeleteTextures`
* `glTexImage2D`, `glTexParameteri`

Presentation is split out through `gfx_dc.c`: `gfx_dc_swap_buffers_end()` calls `glKosSwapBuffers()` and applies frame pacing based on `gVIsPerFrame`.

## Textures

Texture state is mostly interpreted in `gfx_retro_dc.c`, not hidden inside GLdc.

`gfx_retro_dc.c` tracks RDP-ish texture state:

* source image address, format, size, and line width,
* tile number and TMEM-ish load state,
* tile size and wrap/clamp/mirror state,
* TLUT/palette state,
* texture cache nodes keyed by source address and palette.

Texture conversion routines import common N64 formats into 16-bit buffers before calling the rendering API:

* RGBA16 -> `GL_UNSIGNED_SHORT_1_5_5_5_REV`
* RGBA32 -> converted to 4444-style data
* IA8 / IA16 -> converted to 4444-style data
* CI4 / CI8 -> expanded through `tlut`

`gfx_gldc.c` then pads non-power-of-two textures, tracks UV scale compensation, chooses GLdc/KOS internal formats such as `GL_ARGB1555_TWID_KOS` or `GL_ARGB4444_TWID_KOS`, and uploads with `glTexImage2D`.

For PSPGL, the useful split is:

```text
N64 texture interpretation/cache/conversion:
    port from gfx_retro_dc.c, with PSP-safe alignment and types

GL upload/sampler setup:
    adapt from gfx_gldc.c to PSPGL-supported formats
```

Do not copy Dreamcast twiddled texture formats directly; PSPGL will need ordinary PSPGL-supported internal formats and measured upload behavior.

## Matrices

The Dreamcast renderer interprets `G_MTX` in `gfx_sp_matrix()`.

It maintains:

* a small modelview stack,
* a projection matrix,
* a combined modelview-projection matrix,
* a `matrix_dirty` flag.

When drawing, it flushes buffered geometry if needed and loads the combined matrix into GL with `glMatrixMode(GL_PROJECTION)` and `glLoadMatrixf((const float*) rsp.MP_matrix)`.

Important caveat: this code is specialized for Star Fox 64. Comments explicitly note assumptions such as limited modelview stack behavior and Titania-specific pop handling. This is acceptable as a renderer reference for this PSP-only Star Fox 64 port, but it should be documented as game-specific rather than treated as a general N64 renderer.

## Lighting

Dreamcast lighting is vertex-load-time lighting in the display-list interpreter, not OpenGL fixed-function lighting.

Flow:

```text
G_MOVEMEM light data
    -> gfx_update_light()
G_MOVEWORD G_MW_NUMLIGHT
    -> current light count
G_VTX
    -> gfx_sp_vertex()
        -> transform vertices by MP matrix
        -> transform/normalize light and look-at directions when dirty
        -> compute per-vertex RGB from normal dot light coefficients
        -> store lit color in loaded vertex
triangle emission
    -> copies computed color into GL vertex color
```

This is the most important architectural lesson for PSPGL. The GL backend does not need to reproduce N64 lighting through `glLight*`. The interpreter computes vertex colors before GL draw submission. That keeps PSPGL usage simple and avoids depending on PSPGL fixed-function lighting fidelity.

The Dreamcast implementation also contains Star Fox 64 specific lighting assumptions:

* directional light slots `0` and `4` are emphasized,
* ambient uses `current_lights[current_num_lights - 1]`,
* color tables apply brightness/gamma-like remapping,
* several level/game-state conditionals modify final color behavior.

These should be ported only after the basic path is running and with comments explaining why they exist.

## Combine Modes

Dreamcast does not implement a complete programmable N64 combiner in GL.

`gfx_retro_dc.c` reduces the RDP combine mode into a compact `cc_id` and `ColorCombiner`. `gfx_gldc.c` mostly maps that to fixed-function `glTexEnvi` modes such as `GL_MODULATE`, `GL_DECAL`, and `GL_REPLACE`, then `gfx_retro_dc.c` computes per-vertex colors from primitive/env/shade inputs before submitting geometry.

There are also many Star Fox 64 specific escape hatches and state toggles, including magic no-op-like commands used for effects such as blur, starfield, z-fighting fixes, radar marks, fillrect blending, menu cards, and space backgrounds.

For PSPGL, start with the same reduced combine approach. Do not attempt a full RDP combiner or an OpenGL-to-GU shim as the first step.

## Buffering And Drawing

`gfx_retro_dc.c` batches triangles into `dc_fast_t buf_vbo[]` and flushes them through:

```text
gfx_rapi->draw_triangles(...)
```

The GLdc backend uses client arrays:

```text
glVertexPointer(...)
glTexCoordPointer(...)
glColorPointer(...)
glDrawArrays(GL_TRIANGLES, ...)
```

2D texture rectangles use a four-vertex path and `glDrawArrays(GL_QUADS, ...)`.

PSPGL compatibility questions:

* whether `GL_QUADS` is supported and performant,
* whether `GL_BGRA` vertex colors and texture uploads are accepted,
* whether `GL_UNSIGNED_SHORT_1_5_5_5_REV` and `GL_UNSIGNED_SHORT_4_4_4_4_REV` are supported,
* whether `glEnableClientState` plus client arrays work reliably,
* whether `glLoadMatrixf`, `glOrtho`, fog, alpha test, scissor, and depth mask behave as expected.

If `GL_QUADS` is missing or unreliable in PSPGL, split only the 2D quad submission into two triangles. That is not a manual OpenGL-to-GU shim; it is a small compatibility adaptation at the renderer API boundary.

## Dreamcast-Specific Code To Avoid Copying Directly

Do not copy these parts blindly:

* KallistiOS setup and headers: `<kos.h>`, `<dc/video.h>`, vblank handlers, VMU, Maple, G1 ATA/SD support.
* `glKosInit*`, `glKosSwapBuffers`, `GLdcConfig`, and KOS/GLdc internal formats.
* SH4-specific acceleration and cache code without review: `sh4zam`, `shz_*`, `pref`, `shz_dcache_alloc_line`.
* Dreamcast video mode selection and frame pacing.
* GLdc twiddled texture formats.
* CD/ODE/audio/file-system support.

Some game-specific renderer workarounds may be useful, but import them only after the generic title-screen path is alive and each workaround has a named purpose.

## Comparison With Current PSP Renderer

Current PSP flow:

```text
Game builds gGfxPool->masterDL
Graphics_SetTask()
    -> queues SPTask
src/psp/ultra_reimpl.c
    -> PspPlatform_RunGfxTask(task)
src/psp/platform.c
    -> PspRenderer_RenderGfxTask(task, taskIndex)
```

`legacy_rsp` currently interprets the display list inside `src/psp/renderer.c` and emits native GU commands directly. This has proven PSP hardware setup, texture conversion progress, starfield batching, and useful diagnostics, but the renderer is now entangled with RSP/RDP semantic debt.

`pspgl` currently preserves the same public PSP renderer API but only performs clear/swap proof-of-life:

```text
PspRenderer_Init()
PspRenderer_RenderGfxTask(SPTask* task, u32 taskIndex)
```

The Dreamcast architecture maps cleanly onto the PSP API if the PSPGL backend eventually does:

```text
PspRenderer_Init()
    -> gfx_init(&gfx_psp, &gfx_pspgl_api, "Star Fox 64", false)

PspRenderer_RenderGfxTask(task, taskIndex)
    -> gfx_start_frame()
    -> gfx_run((Gfx*) task->task.t.data_ptr)
    -> gfx_end_frame()
```

The PSP backend does not need to rewrite `src/sys/sys_main.c` the way Dreamcast did. It can keep the existing PSP task handoff and make the new PSPGL backend consume `task->task.t.data_ptr`.

## Reusable Pieces For PSPGL

Best first candidates:

* `gfx_rendering_api.h`
* `gfx_window_manager_api.h`
* `gfx_pc.h` interface shape
* `gfx_retro_dc.c` display-list interpreter, after isolating Dreamcast/SH4 dependencies
* `gfx_cc.c` / `gfx_cc.h`
* `gfx_buf.c` buffer definitions, adjusted for PSP alignment and limits
* the `dc_fast_t` concept, likely renamed to a platform-neutral vertex type

Adapt rather than copy:

* `gfx_gldc.c` as the starting point for `gfx_pspgl.c`
* `gfx_dc.c` as the starting point for a tiny `gfx_psp.c` window/swap API
* texture upload internal formats and padding behavior
* frame pacing and swap

## PSPGL Features To Test

Before importing the full Dreamcast interpreter, write small PSPGL probes or guarded test paths for:

* EGL/context init plus repeated clear/swap.
* `glEnableClientState`, `glVertexPointer`, `glTexCoordPointer`, `glColorPointer`, `glDrawArrays(GL_TRIANGLES)`.
* `GL_QUADS`; if unavailable, convert quads to triangles at the PSPGL backend boundary.
* `glMatrixMode`, `glLoadMatrixf`, `glOrtho`.
* `glTexImage2D` with 16-bit source types: 1555, 4444, 565 if needed.
* `GL_BGRA`; if unavailable, switch vertex/texture conversion to RGBA.
* `glTexParameteri` wrap/filter modes, especially clamp and mirrored repeat.
* `glScissor`, `glViewport`, depth test, depth mask, alpha test, blend func.
* `glFog*`.
* Texture cache lifetime and `glDeleteTextures` behavior under memory pressure.

## Recommended First Implementation Milestone

Keep the current backend selector and leave `legacy_rsp` untouched.

First real PSPGL milestone:

```text
PspRenderer_RenderGfxTask(task, taskIndex)
    -> call a tiny PSPGL renderer API using one hard-coded client-array triangle
    -> exercise matrix load, vertex/color arrays, depth state, and swap
```

Then port the Dreamcast renderer in layers:

1. Add platform-neutral `src/psp/gfx/` staging files with the Dreamcast API shapes.
2. Implement `gfx_psp` window API over the existing PSPGL context/swap.
3. Implement a minimal `gfx_pspgl_api` from `gfx_gldc.c` using only clear, matrix, client arrays, depth/blend, and triangle draw.
4. Import the smallest `gfx_retro_dc.c` subset that can run a trivial display list containing matrix, vertex, triangle, and end commands.
5. Add texture upload and texrect support after triangle geometry works.
6. Add Dreamcast-style vertex-load-time lighting after matrix and vertex colors are visibly correct.
7. Add combine/fog/effect workarounds incrementally, each with a short comment and a screenshot or hardware note where possible.

The important architectural move is to reuse the Dreamcast split:

```text
Star Fox 64 display-list interpreter
    -> small rendering API
    -> PSPGL implementation
    -> PSP swap/pacing
```

That gives PSPGL the same kind of staging role GLdc has on Dreamcast, while preserving the current native GU renderer as `legacy_rsp` for reference and future optimization.
