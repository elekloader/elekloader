# Digitone II SHARC track synths

Experimental, OS 1.12 only. machines-dn2 0.1 can register an external C
oscillator in the native FUNC+SYN selector. Its output passes through the
voice envelope, the selected stock or [custom filter](MACHINES-DN2-FILTER.md),
pan, sends and mixer. Timing, full-polyphony and long-duration stability
are unqualified.

## Author interface

Use the existing four-field `mk2_synth` descriptor from
`mk2/machines.h`, contributing its pointer to `mk2_voice_synths`
instead of `mk2_synths`. The fields remain stable ID, stock defaults type,
name and short label. Use permanent IDs 5..31; clones share that namespace.
The companion supplies a named empty SYN page and stock AMP controls,
unless the mod declares its own pages (below). FLTR controls belong to the
selected filter.

### Pages

Contribute a pointer to an `mk2_voice_pages` with the synth's ID, a page
count (1..5) and the pages. Each page has eight knobs; a `NULL` knob is an
empty slot, and a used one shows the stock knob with its label (1..5 ASCII
characters, shown in capitals) and starts at its default (0..127) when the
synth is selected:

```c
#include "mk2/machines.h"
static const struct mk2_param level = {"LEV", 100}, detune = {"DET", 64};
static const struct mk2_page sine_pages[1] = {{{&level, &detune}}};
const struct mk2_voice_pages sine_layout = {7, 1, sine_pages};
```

```json
{"to": "mk2_voice_pages", "data": "00000000", "relocs": [[0, "abs32", "sym:sine_layout", 0]]}
```

Page `p`, knob `k` is sound slot `25 + 8p + k`. The value is saved with
the sound and copied with it, as a stock synth's. SYN steps through the
pages, as on a stock synth; the header reads the synth's name and the page,
such as `SINE (2/2)`. MIDI CC and NRPN reach a knob by position:
a knob uses the number the clone's stock synth has for the same slot (for
an FM TONE clone, SYN1 knob A is CC 40 and NRPN 1:73), turning it sends
that CC, and a number whose slot is empty does nothing. A slot the clone
gives no CC (FM TONE: 34 and 50..57) has none. Knobs take parameter locks as on a
stock synth (hold a step and turn): the OS puts the locked value in the
voice that plays the trig, so a kernel needs no code for them.

Declared knobs appear by label in all three LFO destination lists and in
the TRACK setup's modulation lists (**FUNC+FLTR**): velocity, aftertouch,
pitch bend, mod wheel, breath controller and key tracking. Empty slots are
omitted. Route them as on a stock synth; the OS applies their modulation
before the callback receives each voice's values in `param`.

The companion ignores an invalid entry (an unregistered or non-SHARC ID,
a second entry for one ID, a count outside 1..5, a bad label or default)
and the synth keeps its empty page. The linker refuses two mods that give pages to one ID.

The same mod must carry a `dn2-voice-mono32-l2-v1` artifact with a matching
`machine_id` and `entries: {"render_offset_bytes": ...}`. Require `core`
(core 0.1) and `machines-dn2`. The linker refuses a missing/mismatched
payload, conflicting IDs or registering the voice's ID as a clone too.
A build accepts at most one SHARC synth and one SHARC filter, in separate memory
regions. Neither can be combined with a master-output SHARC mod.

```c
#include "digitone-mk2/voice.h"
void sharc_voice_render(const struct dn2_voice_input *in);
```

The 20-byte input is an output pointer, slot, unsigned 8.8 pitch, flags
and `param`, this voice's 40 knob values (page p, knob k at `param[8p + k]`).
They are unsigned 8.8 (0..32512 = 0..127.0), the words the stock engines
get: the voice's own values from its note-on, then live edits, parameter
locks, LFOs and MIDI modulation, smoothed by the OS. An empty knob holds
whatever its slot holds; read only the knobs you declared.
Write all 32 mono floats at 48 kHz and return nothing. Slot is 0..15,
**not the track number**: keep independent state per slot. A trigger edge's
block is silent; the next block has settled pitch and `DN2_VOICE_START`.
Reset phase on START. The stock envelope supplies release.

## Context and memory

The adapter preserves PEx R0–R15 in full through PX, both halves of PX,
I0–I5/I12/I13, M0–M7/M13–M15, L0–L5, MODE1 and both ASTAT/STKY pairs.
It pins M5=0, M6=1, M7=-1, M13=0, M14=1, M15=-1, L0–L5=0, SIMD off,
32-bit floating-point rounding on, saturation/truncation off. Register
banks and interrupt enablement are retained. The other stock caller passes
through without a callback.

Preserve everything else: the other processing element, accumulators,
FIFO, other DAG registers, MODE2, interrupt masks, stack bases/lengths and
the contents/depth of the hardware PC, loop and status stacks. Do not mask
interrupts. Use the short-word C call/return convention (input in R4),
return I6/I7 unchanged and retain the pinned constants needed by that
convention. The callback may declare at most **128 stack bytes**; its
adapter adds an eight-byte call frame. A declaration is not a measurement
or an instruction sandbox.

The synth owns the first 16 KiB of the L2 reservation:

| Part | Loader byte range | Size |
|---|---|---|
| Pinned adapter, including guard | `0x20090000..0x20090800` | 2 KiB |
| Author code, including 64-byte guard | `0x20090800..0x20092000` | 6 KiB |
| Author data | `0x20092000..0x20093c00` | 7 KiB |
| Zeroed adapter state | `0x20093c00..0x20094000` | 1 KiB |

Ranges exclude their end. A producer's build script reads
`elekloader.sdk.sharc.machine_layout('voice')` for the compiler-independent
`code_pc16`, `code_bytes`, `data_address` and `data_bytes` bounds. Link the
kernel and data at those addresses, then call
`voice_artifact(code, data, render_offset_bytes, machine_id, stack_bytes)`.
The entry offset counts from the start of the supplied kernel; data may
be empty. The SDK adds the pinned adapter, NOP padding and zero ranges;
the producer owns compilation. The linker writes the render PC and ID at
`0x20093e00`. Keep writes in the author data area and supplied voice buffer.

The exact OS 1.12 hook is PC16 `0xb8251b`, loader `0x20004a36`, called by
the voice pipeline at `0x1c9459`. Its guarded six-byte stack adjustment is
`8715ffffeeff`; the adapter executes it and continues at `0xb8251e`.
Rebuild both machine adapters from `mods/machines-dn2/*-bridge.s` with
`rebuild_voice.py`; the Selache pin is
`2b26d3b75c53063575bc5c820fa0d38879335187`. The voice adapter's 1,840 code
bytes are padded to 2 KiB, SHA-256
`11cb0abb8a60cd9e97253ff7a8c063b90bd70f110a1cee95c52c98a98dbbcef1`.

The adapter uses register counts for right shifts. This Selache pin omits
the required sign-extension bits in negative immediate shifts, which can
halt the physical DSP even when emulation passes. See the
[DN2 instruction investigation](https://github.com/angellinares/dn2_firmware_explore/blob/main/docs/waverider-dsp-compare.md#11-the-cause-found-on-silicon-negative-shift-immediates-2026-09-29).
The unit suite checks the generated adapter for this encoding defect.

## Example and checks

The [paired C example](https://github.com/simonjohansson/digitone-2-mod-hack) builds SINE and
LOWPASS together with DigiHealth. SINE
supports integer MIDI pitches 36..84 and plays one octave above its input;
fractional pitch rounds down and other pitches produce zeros. Each voice
slot keeps independent phase and resets it on START. Its two pages show
how a kernel reads `param`: LEV (`param[0]`) is the level, TUNE
(`param[1]`) transposes in semitones around 64 and OCT (`param[8]`) in
octaves, one per 16; FINE (`param[12]`) is declared but unused. This
example rounds fractional TUNE values down.

Run `python tests/test_units.py` and the TypeScript suite for the artifact,
registration, callback binding and stack-bound checks. Runtime checks must
cover native selection, overlapping notes and slot reuse, stock-track
coexistence, AMP/filter controls, preset copy and saved/missing registrations.
Run these checks in the coupled emulator and on the device for each
changed workload. Hardware timing needs separate qualification.
