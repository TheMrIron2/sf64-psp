# PSP Software Audio Architecture

## Status

The PSP build now links the real Star Fox 64 audio driver, a portable scalar
audio-ABI mixer, the original US revision 1 audio data, and a dedicated PSP
PCM output thread.

This is a build-complete first production path. BGM, SFX, radio voice
playback, timing, underrun behavior, real-PSP cost, and Vita compatibility
remain runtime-untested as of June 15, 2026. Do not infer support from a
successful EBOOT build.

## Driver Basis

The current `src/audio` tree remains canonical. It is Star Fox 64's revision
of Nintendo EAD's shared N64 audio driver and keeps its global-state layout.
It has not been refactored into Ocarina of Time's later `AudioContext`
representation.

The closest PSP reference used was:

* `https://github.com/z2442/oot-PSP.git`
* commit `805181d938e60afc843d6e7a37f2d98e78fb4ee1`

The useful conceptual mapping is:

| OOT concept | SF64 representation |
| --- | --- |
| audio context heap and pools | `gAudioHeap`, `gAudioHeapSize`, global pools and caches |
| AI buffers and lengths | `gAiBuffers`, `gAiBuffLengths` |
| notes and synthesis state | `gNotes`, `gNoteSubsEu`, `gNoteSynthesisState` |
| sequence players | `gSeqPlayers` |
| load tables and status | `gSequenceTable`, `gSoundFontTable`, `gSampleBankTable`, global status arrays |
| audio specification | `gAudioSpecId`, `gAudioBufferParams`, related globals |

OOT PSP informed 32-bit PSP execution, output ownership, and future
acceleration boundaries. No OOT source was copied because a clear repository
license was not found.

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

It also includes `src/psp/audio_mixer.c`, `src/psp/audio_output.c`, and
`src/psp/audio_assets.S`. No audio source wildcard is used.

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
processing, sequence players, notes, effects, reverb, and buffer sizing.
On PSP, the audio ABI macros call the scalar mixer directly while building
the nominal command list. The scheduler still acknowledges the resulting
audio task, but synthesis has already produced PCM on the CPU.

The mixer implements the ABI operations emitted by this SF64 revision,
including ADPCM decode, resampling, envelope mixing, reverb-related mixing,
DMEM movement, interleave, and loop state handling. It has no SSE, NEON,
SH-4, VFPU, or Media Engine requirement.

## PCM Output

`osAiSetNextBuffer()` copies signed 16-bit interleaved stereo PCM into a
bounded PSP-owned ring. Only the dedicated output thread performs blocking
submission.

Current configuration:

```text
sample rate:       32000 Hz
channels:          2
sample format:     signed 16-bit interleaved
ring blocks:       8
maximum block:     1024 stereo frames
output API:        sceAudioSRCChReserve / sceAudioOutput2OutputBlocking
```

Blocks use the frame count selected by SF64, up to the bounded maximum.
Cache writeback/invalidation occurs before PSP output. `osAiGetLength()`
reports queued bytes including the block currently being played.

## Voice State

The former PSP audio stubs were removed. `Audio_PlayVoice`,
`Audio_GetCurrentVoice`, `Audio_GetCurrentVoiceStatus`, and related calls now
resolve to SF64's real implementations and sequence-player state. There are
no message-specific timers, permanent silence values, or dialogue gates.

Whether radio samples load and complete correctly is pending runtime testing.

## Diagnostics

With `PSP_LOG=1`, startup records the scalar backend and 32 kHz stereo output.
The first queued PCM frame count is logged once. Output-ring overruns are
logged for the first four occurrences and then at powers of two, avoiding
per-buffer spam.

Future measured diagnostics should add synthesis duration, active notes and
players, DMA/load backlog, underruns, and current voice state after the first
runtime pass identifies where bounded instrumentation is most useful.

## Acceleration Plan

The scalar mixer is the permanent correctness and compatibility baseline.
After PPSSPP and real-PSP correctness:

1. Profile ADPCM decode, resampling, envelope mixing, reverb, and copies.
2. Add optional VFPU replacements behind the same mixer boundary.
3. Consider an optional Media Engine worker only after deterministic scalar
   comparison is available.

A future Media Engine path must define shared-buffer ownership, cache
boundaries, work packets, startup/shutdown, failure fallback, PPSSPP behavior,
and Vita behavior. It must never be required for normal playback.
