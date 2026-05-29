# PSPGL Backend Notes

The `pspgl` renderer backend is a staging backend. It exists to validate PSPGL and prepare for a future Dreamcast-style renderer path, not to render Star Fox 64 display lists yet.

Current milestones:

* EGL/PSPGL context creation, clear, and swap are wired through the PSP renderer backend.
* The next feature probe is a hard-coded colored triangle over the purple clear color.
* The triangle uses fixed-function PSPGL client arrays: `glEnableClientState`, `glVertexPointer`, `glColorPointer`, and `glDrawArrays(GL_TRIANGLES, 0, 3)`.

The skeleton mirrors the Dreamcast `sf64-dc` split at a small scale:

```text
src/psp/gfx/gfx_psp.c
    PSP window/frame/context layer

src/psp/gfx/gfx_pspgl.c
    PSPGL rendering API probe layer

src/psp/renderer_pspgl.c
    public PspRenderer_* bridge used by the PSP task path
```

This is intentionally preparing for a later import or adaptation of the Dreamcast-style `gfx_retro_dc.c` display-list interpreter. Modern Starship/libultraship is still not imported, and the legacy native GU/RSP renderer remains untouched.
