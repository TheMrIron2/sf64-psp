# n64psp Queue Experiment

The PSP build can replace SF64's local message queue implementation with the
queue implementation from the `TheMrIron2/n64psp` submodule at `lib/n64psp`.
This is intentionally limited to four libultra symbols:

- `osCreateMesgQueue`
- `osSendMesg`
- `osJamMesg`
- `osRecvMesg`

Enable the experiment with the default build option:

```sh
make USE_N64PSP_QUEUES=1
```

Build the original SF64 PSP queue implementation with:

```sh
make USE_N64PSP_QUEUES=0
```

The optional startup self-test is enabled with:

```sh
make USE_N64PSP_QUEUES=1 N64PSP_QUEUE_SELFTEST=1
```

The self-test includes SF64's own libultra headers rather than n64psp headers,
so it verifies that SF64 reaches n64psp through the existing declarations and
`OSMesgQueue` layout. Compile-time checks cover `sizeof(OSMesg)`,
`sizeof(OSMesgQueue)`, and the offsets of `mtqueue`, `fullqueue`, `validCount`,
`first`, `msgCount`, and `msg`.

`src/psp/n64psp_integration.c` is the only SF64 source file that includes
n64psp headers. Keeping that include boundary isolated avoids n64psp's
top-level compatibility headers shadowing SF64's libultra headers. The Makefile
also appends `lib/n64psp/include` after SF64 and ultralib include paths.

n64psp must be initialized before the first message queue is created. PSP
startup calls `PspN64psp_Init()` after `pspDebugScreenInit()` and before
`PspPlatform_Init()` or `bootproc()`. Normal shutdown is attempted only if
`bootproc()` returns; the asynchronous PSP exit callback does not shut down
n64psp.

Queue reinitialization while a sender or receiver is blocked remains
unsupported. n64psp currently destroys and recreates queue-side synchronization
objects during `osCreateMesgQueue`, so callers must not recreate an active queue
with blocked waiters.

SF64 has convenience macros that compare non-blocking `osRecvMesg` failures
with exact `-1`. n64psp's libultra-shaped `osSendMesg`, `osJamMesg`, and
`osRecvMesg` entry points therefore preserve the libultra-visible contract:
`0` on success and `-1` on any failure. Detailed native error reporting belongs
behind separate `n64psp_*` APIs rather than leaking `n64psp_result` values
through the compatibility API.

Physical PSP or emulator testing still needs to confirm that blocking receives
wake reliably across the game/audio/timer threads, that queue resource cleanup
does not leak over repeated boots, and that no timing-sensitive gameplay path
depends on the old busy-wait queue behavior.
