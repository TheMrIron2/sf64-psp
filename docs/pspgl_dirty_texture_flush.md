# PSPGL Dirty Texture Flush Finding

The dirty-gated texture-cache flush experiment was rejected after real PSP
hardware testing.

The hypothesis was that PSPGL could suppress texture-cache invalidation on
effective texture image changes and only invalidate after texture-visible memory
was modified. Hardware testing showed that this is not valid on PSP: texture
cache invalidation is required when changing effective texture image state, not
solely after writes to texture memory.

Suppressing bind-driven invalidation caused universal texture corruption. Even a
corrected dirty-write `SYNC`/`FLUSH` sequence would still need the bind-driven
flush demonstrated by the required `sceGuTexImage()` behavior, eliminating the
tens-of-thousands-of-flushes saving this experiment was intended to obtain.

The valid PSPGL implementation is therefore the OFF/control path: keep the
bind-driven texture-cache invalidation behavior intact.

Future optimisation work should move away from texture-cache invalidation.