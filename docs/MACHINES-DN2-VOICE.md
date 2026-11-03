# Digitone II SHARC track synths

Experimental, OS 1.12 only. machines-dn2 0.1 can register an external C
oscillator in the native FUNC+SYN selector. Its output passes through the
voice envelope, the selected stock or [custom filter](MACHINES-DN2-FILTER.md),
pan, sends and mixer. Timing, full-polyphony and long-duration stability
are unqualified.

## Author interface

Register an `mk2_synth` descriptor from `mk2/machines.h`: its permanent
ID (5..31, see [Digitone II machines](MACHINES-DN2.md)), its name (ASCII,
at most 31 characters) and its short label (at most 7). Contribute its
pointer to `mk2_voice_synths`:

```c
#include "mk2/machines.h"
const struct mk2_synth sine = {7, "SINE", "SINE"};
```

```json
{"to": "mk2_voice_synths", "data": "00000000", "relocs": [[0, "abs32", "sym:sine", 0]]}
```

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
a knob uses the number FM TONE, the host type, has for the same slot
(SYN1 knob A is CC 40 and NRPN 1:73), turning it sends
that CC, and a number whose slot is empty does nothing. A slot FM TONE
gives no CC (34 and 49..57) has none. Knobs take parameter locks as on a
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
(core 0.2) and `machines-dn2`. The linker refuses a missing/mismatched
payload or conflicting IDs.
A build accepts at most one SHARC synth and one SHARC filter, in separate memory
regions. Neither can be combined with a master-output SHARC mod.

```c
#include "digitone-mk2/voice.h"
void sharc_voice_render(const struct dn2_voice_input *in);
```

The 24-byte input is an output pointer, slot, unsigned 8.8 pitch, flags,
`param` and `state`. `param` is this voice's 40 knob values (page p, knob k at `param[8p + k]`).
They are unsigned 8.8 (0..32512 = 0..127.0), the words the stock engines
get: the voice's own values from its note-on, then live edits, parameter
locks, LFOs and MIDI modulation, smoothed by the OS. An empty knob holds
whatever its slot holds; read only the knobs you declared.
Write all 32 mono floats at 48 kHz and return nothing. Slot is 0..15,
**not the track number**: keep independent state per slot. A trigger edge's
block is silent; the next block has settled pitch and `DN2_VOICE_START`.
Reset phase on START. The stock envelope supplies release.

For per-voice state, declare its type once, at file scope:

```c
struct voice { uint32_t phase; };
DN2_VOICE_STATE(struct voice);
```

`in->state` then points at this slot's `struct voice`. There are 16, one
per slot, zeroed when the DSP loads and kept between calls and across
notes: the adapter never clears them, so reset what needs it on START.
Without the macro `state` is null. `machine_from_executable` reads the
size from the executable and reserves the 16 after the data, in the
31 KiB data area; it refuses with the sizes when they do not fit.

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
convention.

The callback runs on its own 4 KiB stack in L1, `0x2de000..0x2df000`
(loader `0x282de000`), not on the stock Audio Task's. It is in L1 like
stock's stacks: a FreeRTOS context switch saves the task's registers onto
the running stack, partly through the PM bus. The adapter points I7 at its
top, with B7 at its base and L7 = 0 (linear), and puts stock's I7, B7 and
L7 back afterwards. Writing B7 also loads I7, so it writes B7 first, with
interrupts off for those few instructions. The callback may declare at
most **2,048 stack bytes**, plus the adapter's eight-byte call frame. The
rest is for interrupts: one taken during the call (its dispatcher, its C
handler, a context switch's 516-byte save) pushes onto the same stack.

The linker zero-fills the stack and writes a guard word at its base. The
adapter checks the guard before and after every call. Once an overrun has
broken it, the machine is called no more until the DSP reloads and its
slots play silence; a tool reads the guard to see that. The overrun
itself has already written below the stack by then. A declaration is not
a measurement or an instruction sandbox.

The synth owns 64 KiB of L2:

| Part | Loader byte range | Size |
|---|---|---|
| Pinned adapter, including guard | `0x20090000..0x20090800` | 2 KiB |
| Author code, including 64-byte guard | `0x20090800..0x20098000` | 30 KiB |
| Author data | `0x20098000..0x2009fc00` | 31 KiB |
| Zeroed adapter state | `0x2009fc00..0x200a0000` | 1 KiB |

Ranges exclude their end. A mod's build never names them: it compiles the
kernel, links it with the script `elekloader.sdk.sharc.machine_linker_script('voice')`
writes (code from `seg_swco`, data from `seg_dmda`, entry
`sharc_voice_render`), and passes the linked executable to
`machine_from_executable('voice', executable, machine_id)`. That checks the
placement, guard and entry and returns the artifact; `stack_bytes` defaults to
the limit below. Compilation belongs to the mod. A producer that links some other
way can call `voice_artifact(code, data, render_offset_bytes, machine_id,
stack_bytes)` with the bounds `machine_layout('voice')` gives. The SDK adds the
pinned adapter, the code's NOP guard and the zero ranges (zeroed words are NOPs, so unused code
space costs no update bytes). The linker writes the render PC and ID at
`0x2009fe00`, and the voice state's address and one voice's size at `0x2009fe20`. Keep writes in the author data area and supplied voice buffer.

The exact OS 1.12 hook is PC16 `0xb8251b`, loader `0x20004a36`, called by
the voice pipeline at `0x1c9459`. Its guarded six-byte stack adjustment is
`8715ffffeeff`; the adapter executes it and continues at `0xb8251e`.
Rebuild both machine adapters from `mods/machines-dn2/*-bridge.s` with
`rebuild_voice.py`; the Selache pin is
`2b26d3b75c53063575bc5c820fa0d38879335187`. The voice adapter's 1,980 code
bytes are padded to 2 KiB, SHA-256
`5bbf8b98c5ff13d4121a17e2d7a8ca550f05821b4564a67c0ce84737319da3f0`.

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
