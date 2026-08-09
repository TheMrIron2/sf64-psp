# PSP Media Engine Audio Architecture

## Status

The scalar mixer remains the known-good implementation. With `PSP_AUDIO=1`,
SF64 now builds audio commands on Allegrex and executes them asynchronously on
the Media Engine. It has been stable and almost entirely full speed through the
first two levels on real PSP hardware. Vita and PPSSPP fallback need scalar fallback.

## Driver Basis

The current `src/audio` tree remains canonical. It is Star Fox 64's revision
of Nintendo EAD's shared N64 audio driver and keeps its global-state layout.
It has not been refactored into Ocarina of Time's later `AudioContext`
representation.

The closest PSP reference used was:

* `https://github.com/z2442/oot-PSP.git`
* commit `0e40bb931e934248362c05820f0de68b3894affe`

The useful conceptual mapping is:

| OOT concept | SF64 representation |
| --- | --- |
| audio context heap and pools | `gAudioHeap`, `gAudioHeapSize`, global pools and caches |
| AI buffers and lengths | `gAiBuffers`, `gAiBuffLengths` |
| notes and synthesis state | `gNotes`, `gNoteSubsEu`, `gNoteSynthesisState` |
| sequence players | `gSeqPlayers` |
| load tables and status | `gSequenceTable`, `gSoundFontTable`, `gSampleBankTable`, global status arrays |
| audio specification | `gAudioSpecId`, `gAudioBufferParams`, related globals |

OOT PSP informed the early ME boot, uncached mailbox, cache ownership, delayed
completion wait, and scalar fallback design. No OOT source was copied because
a clear repository license was not found.

The Media Engine loader and core mapping come from:

* `https://github.com/mcidclan/psp-media-engine-custom-core.git`
* commit `9406d17af0fa831ced752d6e352c1c1e761b2f38`

The library is MIT licensed and supplies model-aware ME core selection, the
kernel bridge, suspend hooks, and ME cache operations.

Secondary references:

* `sf64-dc`, commit `94d879d00f2d0e808b796b3a014dbb6600d18277`,
  for SF64-specific little-endian relocation and synthesis behavior.
* Starship, commit `6202c44356fee70dd23e80a16933b211863d3e2d`,
  for its portable scalar mixer. The adapted mixer retains attribution and
  originates from CC0-1.0 code.
* `z2442/sm64-port`, commit
  `8c219c77a94ed458958cbe661db7e1f537dc8b26`, for PSP SRC output patterns.

`sf64-dc` and Starship declare CC0-1.0. SM64 PSP was used only as a platform
reference, not copied.

## Build Closure

`src/psp/sources.mk` explicitly includes all twelve SF64 audio units:

```text
audio_context.c audio_effects.c audio_general.c audio_heap.c
audio_load.c audio_playback.c audio_seqplayer.c audio_synthesis.c
audio_tables.c audio_thread.c note_data.c wave_samples.c
```

It also includes `src/psp/audio_me.c`, `src/psp/audio_mixer.c`,
`src/psp/audio_output.c`, `src/psp/audio_assets.S`, and the retained kcall
import in `src/psp/audio_me_kcall.S`. No audio source wildcard is used.

Every audio source inherits `TARGET_PSP`, `NON_MATCHING`, `COMPILER_GCC`, and
`AVOID_UB`. `AVOID_UB` is a required PSP invariant because it selects the
existing corrected audio-heap behavior. There is no duplicate heap fix.

## Loading And DMA

`audio_assets.S` embeds the original `audio_seq`, `audio_bank`, and
`audio_table` binaries and exports the segment symbols expected by SF64.
The existing load tables, cache ownership, asynchronous states, and
completion queues remain in use.

PSP audio DMA is a bounded memory copy through the port's libultra
reimplementation. It preserves the original request/completion message
semantics even though the source data is already memory resident.

PSP is 32-bit little-endian, matching the driver's pointer width but not the
N64 asset byte order. PSP-only corrections cover:

* sequence-to-font table offsets;
* sound-font pointer relocation;
* sample codec, medium, preload, and relocation flags;
* sample size and sample-bank addresses;
* envelope points and tuning floats;
* ADPCM loop positions and loop predictor state;
* ADPCM book dimensions and coefficients.

Raw pointer-bearing asset structures remain 32-bit. They are not widened or
replaced with Starship's host resource representation.

The original `AudioLoad_Init()` assumes that all audio globals are contiguous
between linker sentinels `gAudioContextStart` and `gAudioContextEnd`. PSP ELF
section ordering does not preserve that source-level range and may place the
end symbol below the start symbol. PSP therefore relies on normal
zero-initialized BSS instead of performing that unsafe contiguous clear.
Audio heap initialization and later specification resets still run normally.

## Synthesis

SF64's normal `AudioThread_CreateTask()` path remains responsible for command
processing, sequence players, notes, effects, reverb, and buffer sizing. The
audio ABI macros now build the original eight-byte command stream. The ME runs
the scalar command interpreter and publishes PCM plus persistent mixer state.

Submission is asynchronous. Allegrex waits at the next synthesis period before
queueing the completed buffer or building commands which depend on mixer state.
This overlaps mixing with the game while preserving command order.

The mailbox is uncached. Allegrex collects command, sample, codec-state, filter,
loop, and output ranges from the command stream. Up to sixteen coalesced input
ranges are written back individually; busier lists use one whole-cache
writeback because many kernel range calls cost more than flushing the PSP's
small data cache. The ME invalidates external command inputs and writes back
external results by opcode instead of applying whole-cache barriers. Normal
Allegrex completion invalidates only command-owned state and output ranges.
Range overflow and fault recovery retain whole-cache fallbacks.

The complete scalar mixer state is published after each ME job so Allegrex can
replay safely after a fault. This includes the byte-swapped ADPCM loop history,
which no longer lives in an untracked function-static buffer. Diagnostic builds
retain whole ME barriers because their additional mutable probe state is not
part of the normal command ownership model.

The ME sends a completion interrupt after publishing the idle state. Allegrex
sleeps on a semaphore while synthesis is active, with bounded polling retained
when interrupt setup is unavailable. The ME itself remains in its persistent
idle loop between jobs. The mixer uses private scalar memory routines so ME
execution does not enter Allegrex VFPU code.

If ME startup fails, the same command interpreter runs synchronously on
Allegrex. A faulted ME job is interrupted and replayed on the scalar fallback
from the last published cache state.

## PCM Output

The audio thread reserves a block in the bounded PSP-owned ring before command
construction. The final `A_SAVEBUFF` writes signed 16-bit interleaved stereo PCM
directly into that block, which is published only after ME completion. This
removes the previous `osAiSetNextBuffer()` copy on the ME path. The legacy copy
submission remains as an initialization failure fallback. Only the dedicated
output thread performs blocking PSP submission.

Current configuration:

```text
sample rate:       32000 Hz
channels:          2
sample format:     signed 16-bit interleaved
ring blocks:       8
maximum block:     1152 stereo frames
output API:        sceAudioSRCChReserve / sceAudioOutput2OutputBlocking
```

Blocks use the frame count selected by SF64, up to the bounded maximum.
Cache writeback/invalidation occurs before PSP output. `osAiGetLength()`
reports queued bytes including the block currently being played.

## Exchange Formats

The Allegrex-to-ME work item is a compact native array of eight-byte `Acmd`
records plus a command count. Commands contain 32-bit PSP pointers to input,
state, and output memory. This avoids translating or duplicating the command
stream, but it intentionally couples the backend to the PSP address space and
requires explicit cache ownership. A fixed-width offset-based transport would
be more portable but would add translation work without improving this native
PSP path.

The ME-to-output format is the PSP device format itself: native little-endian
signed 16-bit interleaved stereo at 32 kHz. No byte swap is needed between the
two PSP processors. N64 big-endian conversion remains confined to asset loading,
including ADPCM books and loop state, so compressed voice data is decoded into
the same PCM format as music and effects.

## Voice State

The former PSP audio stubs were removed. `Audio_PlayVoice`,
`Audio_GetCurrentVoice`, `Audio_GetCurrentVoiceStatus`, and related calls now
resolve to SF64's real implementations and sequence-player state. There are
no message-specific timers, permanent silence values, or dialogue gates.

Radio samples load, play, and complete correctly on real PSP hardware.

## Diagnostics

With `PSP_LOG=1`, startup records the active ME backend or Allegrex fallback
and 32 kHz stereo output.
The first queued PCM frame count is logged once. Output-ring overruns are
logged for the first four occurrences and then at powers of two, avoiding
per-buffer spam.

Synthesis duration and bounded output health counters remain available for
performance work without sequence-specific logging.

## Next Steps

Profile the adaptive Allegrex writeback threshold and targeted ME barriers on
hardware. VME acceleration can then be added behind the same interpreter
without changing SF64 synthesis code.
