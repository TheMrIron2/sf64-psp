# PSPGL Dirty Texture Cache Flush Experiment

This note documents the optional conservative dirty-gated texture-cache flush
experiment for the local PSPGL submodule.

## Build Options

The parent `sf64-psp` switch is:

```bash
SF64_PSP_PSPGL_DIRTY_TEXTURE_FLUSH=1
```

It defaults to `0` and requires `USE_LOCAL_PSPGL=1`. The parent makefile passes
the option to the PSPGL sub-make as:

```bash
PSPGL_DIRTY_TEXTURE_FLUSH=1
```

PSPGL also defaults `PSPGL_DIRTY_TEXTURE_FLUSH` to `0`. PSPGL archive rebuilds
are guarded by a configuration stamp containing both `PSPGL_PROFILE` and
`PSPGL_DIRTY_TEXTURE_FLUSH`, so changing either option rebuilds the relevant
objects without `make clean`.

## Original Policy

With `PSPGL_DIRTY_TEXTURE_FLUSH=0`, PSPGL preserves the old policy:

* `glBindTexture()` restores the effective texture object's GE texture state.
* If the effective texture object changes, `glBindTexture()` requests
  `CMD_TEXCACHE_FLUSH`.
* Restoring saved `GL_TEXTURE_BIT` state through `glPopAttrib()` also requests
  `CMD_TEXCACHE_FLUSH`.
* Texture image replacement paths keep their existing flush behavior.

This is the control build.

## Dirty-State Policy

With `PSPGL_DIRTY_TEXTURE_FLUSH=1`, each PSPGL context has one conservative
pending dirty bit. The bit means texture-visible memory may have changed since
the last emitted texture-cache invalidation.

Binding a different texture object no longer requests `CMD_TEXCACHE_FLUSH`.
The bind path still restores texture state, updates texture environment state,
handles CLUT dirtiness, validates the target, and preserves reference-counting
behavior.

Restoring a saved texture object through `glPopAttrib(GL_TEXTURE_BIT)` also no
longer requests a texture-cache flush solely because texture state was restored.

The dirty bit starts set for a new context, and is marked again on context
switch. This makes the first textured draw flush conservatively instead of
assuming useful initial texture-cache contents.

## Dirty Producers

The following successful paths mark pending dirty state:

* `glTexImage2D()` through `__pspgl_set_texture_image()` after a new texture
  image is installed.
* `glCompressedTexImage2D()` through `__pspgl_set_texture_image()` after a new
  compressed texture image is installed.
* `glTexSubImage2D()` after the target image is mapped, updated, and unmapped.
* `glCopyTexImage2D()` after framebuffer pixels are copied into the texture
  image.
* `glColorTable()` / `glColorTableEXT()` after the texture colour table image is
  replaced.
* `__pspgl_update_mipmaps()` after each GE render that writes a generated mipmap
  level.
* `eglBindTexImage()` through `__pspgl_set_texture_image()` after a surface
  buffer is exposed as a texture image. This is conservative because the bind
  does not itself write pixels, but the surface memory may already contain
  rendered data.

Validation failures and allocation failures do not mark dirty. Texture-object
binds, texture parameter changes, texture environment changes, and texture
deletion do not mark dirty by themselves.

General EGL surface render-to-texture lifetime tracking remains unresolved:
PSPGL does not currently track every later draw to a surface that was previously
bound as a texture. Hardware validation should include any application path that
renders to an EGL texture surface and samples it again.

## Dirty Consumer

Pending dirty state is consumed in `__pspgl_context_render_setup()` when a draw
has both a bound texture object and `CMD_ENA_TEXTURE` enabled.

The consumer runs after cached GE state, including texture pointer and format
registers, has been flushed to the command list and before `CMD_CLUT_LOAD`,
vertex pointers, index pointers, and `CMD_PRIM` are emitted. If texturing is
disabled, the dirty bit remains pending for a future textured draw.

## Flush/Sync Ordering

The dirty consumer emits this uncached command sequence:

```text
CMD_TEXCACHE_SYNC
CMD_TEXCACHE_FLUSH
```

Both commands are inserted immediately into the GE command stream. The dirty bit
is cleared only after both commands have been enqueued. The sequence preserves
the existing copy-texture path's sync-before-flush ordering and is conservative
for CPU writes, GE render-to-texture mipmap writes, and framebuffer copy paths.

No `glFinish`, queue wait, command-list resize, or native GU path is introduced.

## Profiling Counters

The existing aggregate fields keep their meanings:

* `texture_cache_flush_requests`
* `texture_cache_flush_commands`
* `texture_cache_sync_requests`
* `texture_cache_sync_commands`

The experiment adds:

* `texture_cache_dirty_marks`: clean-to-dirty transitions.
* `texture_cache_dirty_mark_coalesced`: repeated dirty producers while an
  invalidation is already pending.
* `texture_cache_dirty_flushes`: dirty-driven sync/flush sequences emitted
  before textured draws.
* `texture_bind_flushes_suppressed`: bind-driven flushes skipped by the
  experiment.
* `texture_state_restore_flushes_suppressed`: saved texture-state restoration
  flushes skipped by the experiment.
* `texture_cache_dirty_textured_draw_checks`: textured render-setup boundaries
  that checked for pending dirty state.

These counters compile out when `PSPGL_PROFILE=0`.

## Hardware Test Procedure

Prepare these builds:

```bash
make USE_LOCAL_PSPGL=1 SF64_PSP_PSPGL_DIRTY_TEXTURE_FLUSH=0 \
     SF64_PSP_PSPGL_PROFILE=0 SF64_PSP_PROFILE_PHASES=1 \
     PSP_FPS_OVERLAY=0 BUILD_DIR=build/psp-texture-flush-off psp

make USE_LOCAL_PSPGL=1 SF64_PSP_PSPGL_DIRTY_TEXTURE_FLUSH=1 \
     SF64_PSP_PSPGL_PROFILE=0 SF64_PSP_PROFILE_PHASES=1 \
     PSP_FPS_OVERLAY=0 BUILD_DIR=build/psp-texture-flush-on psp

make USE_LOCAL_PSPGL=1 SF64_PSP_PSPGL_DIRTY_TEXTURE_FLUSH=1 \
     SF64_PSP_PSPGL_PROFILE=1 SF64_PSP_PROFILE_PHASES=1 \
     PSP_FPS_OVERLAY=0 BUILD_DIR=build/psp-texture-flush-on-counting psp
```

Capture title-to-map, Corneria introduction, and opening gameplay on real PSP
hardware. Correct counting should keep texture bind and upload counts broadly
unchanged, sharply reduce bind-driven flushes, report dirty coalescing where
multiple writes occur before sampling, and emit flush commands according to
dirty periods consumed before textured draws rather than texture bind count.
Do not expect an exact flush count.
