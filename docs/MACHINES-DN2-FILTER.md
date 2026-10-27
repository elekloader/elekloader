# Digitone II filters

Experimental, OS 1.12 only. `machines-dn2` 0.1 registers custom SHARC
filters in **FUNC+SYN → FLTR**. Filter selection
is independent of the synth: a custom filter can process SINE or a stock
synth. Require core-dn2 0.2. Timing, full-polyphony and long-duration
stability are unqualified.

## Registration

```c
#include "mk2/machines.h"
const struct mk2_filter my_filter = {8, "LOWPASS", "LOWPASS"};
```

Contribute its pointer to `mk2_voice_filters`, with the normal SDK manifest:

```json
"contribute": [
 {"to": "mk2_voice_filters", "data": "00000000",
  "relocs": [[0, "abs32", "sym:my_filter", 0]]}
]
```

Require `core` and `machines-dn2`, and set `resources.core` to `"0.1"`.
The fields are permanent ID **6..31**, name (ASCII, at most 31 characters),
label (at most 7) and an optional `curve` (below). The linker
refuses duplicate filter IDs and stock-ID conflicts. A synth and filter may
use the same numeric ID: their namespaces and stored bytes are separate.
Never reuse a distributed filter's ID for a different filter.

The selector lists the six stock filters in their native order, followed
by registered filters in ID order. Saving and copying retain the ID.
A missing registration keeps its saved ID, disappears from the selector
and uses **MULTI-MODE**, with the existing parameter values. This can change
the sound. Keep project backups when changing installed mods. Removing the
companion restores the stock loader's unknown-ID clamp.

The `mk2_*` interfaces are implemented only for Digitone II. Digitakt II
needs independently qualified firmware adapters and limits. Synths use
`struct mk2_synth` from the same header.

## Custom DSP

LOWPASS 4, the host type, supplies the FLTR page, defaults, knobs, CC and
NRPN, but not the audio: the stock algorithm never runs for the custom ID.
Its page shows ATK/DEC/SUS/REL, FREQ/RESO, G and ENV under the
stock envelope graphic, and FUNC+A edits pre-delay. G is a dotted empty
cell unless the descriptor's optional `g` declares it (below). The
response graph stays empty unless the descriptor's optional `curve` draws
it. The kernel receives the resulting cutoff and resonance, and G; it
does not implement pre-delay, the envelope or ENV itself. The same mod
must carry a
`dn2-filter-mono32-l2-v1` artifact with the matching `machine_id` and
`entries: {"render_offset_bytes": ...}`. The linker refuses a missing or
mismatched payload and conflicting filter IDs.

```c
#include "digitone-mk2/filter.h"
void sharc_filter_render(const struct dn2_filter_input *in);
```

The `curve` callback runs on the ColdFire when the FLTR page draws:
`void curve(uint32_t freq, uint32_t reso, uint32_t g, uint8_t y[51])`. `freq`,
`reso` and `g` are the shown FREQ, RESO and G, including a lock being edited,
as 16-bit words (the whole value in the high byte: 64.00 is `0x4000`); `g`
is 0 without a declared G, and a curve may ignore it. The companion reads
G's as the page reads the others (`0x4006538e`). Fill one entry
per graph column, left to right, with 0 (bottom) to 16 (top), or `0xff`
for no dot; the core draws values above 16 at the top and nothing outside
the graph (screen x 21..71, y 40..56). It hooks the page's curve dispatch
at `0x4006eca0`, which stock filter types pass through unchanged; a missing
filter draws MULTI-MODE's curve, its fallback. Keep it
short: it runs on every redraw of the page.

G is an `mk2_param`: a label of 1..5 printable ASCII characters, shown in
capitals, and a default of 0..127. It is sound slot 66, the slot of the
stock filters' third knob (MULTI-MODE's TYPE), so it answers their CC 18
and NRPN 1:22, takes locks and is saved with the sound. Selecting the
filter keeps the sound's slot-66 value, as the stock filters do, so the
kernel should treat any value as valid:

```c
static const struct mk2_param mode = {"MODE", 0};
const struct mk2_filter my_filter = {8, "LOWPASS", "LOWPASS", curve, &mode};
```

The linker refuses an invalid G. The companion adds one parameter row for
it, after the synths' 40: a copy of MULTI-MODE's slot-66 row owned by
LOWPASS 4, which the stock filters never show. A build has one custom
filter, so one row serves it. The cell shows its label and value; the
picture above it stays empty.

The 24-byte input contains `float *samples`, `uint32_t slot`,
`uint32_t flags`, `float cutoff`, `float resonance` and `float g`.
Process 32 mono floats at 48 kHz in place before the stock AMP stage and
return nothing. Slot is 0..15, not a track number; keep independent state
per slot. `DN2_FILTER_RESET` is set on the first callback after entering this filter
and on a new trigger edge. Clear that slot's history before processing
the block when this flag is set. Keep history between ordinary callbacks.

`cutoff` and `resonance` are the stock values for this slot in this
block, once per 32 samples and not smoothed; smoothing belongs to the
kernel. `cutoff` is normalized: FREQ/128, plus the stock filter envelope
(pre-delay, ATK/DEC/SUS/REL scaled by ENV, which is bipolar) and key
tracking, clamped to 0..1.
`resonance` is RESO/128 (0..0.992). `g` is G/127 (0..1) when the filter
declares G; otherwise ignore it. The callback receives no pitch.
One custom synth and one custom filter can coexist; a master-output mod
cannot be combined with either.

The adapter uses the register preservation and C context described for
the [synth adapter](MACHINES-DN2-VOICE.md#context-and-memory), and also
preserves LCNTR around the callback. The filter
callback may declare at most **20 stack bytes**, plus its adapter's
eight-byte call frame. Preserve the other registers and hardware stacks;
return I6/I7 unchanged. These limits do not measure actual stack use or
prove an audio deadline.

## Memory and dispatch

The filter owns the second 16 KiB of the L2 reservation:

| Part | Loader byte range | Size |
|---|---|---|
| Pinned adapter, including guard | `0x20094000..0x20094800` | 2 KiB |
| Author code, including 64-byte guard | `0x20094800..0x20096000` | 6 KiB |
| Author data | `0x20096000..0x20097c00` | 7 KiB |
| Zeroed adapter state | `0x20097c00..0x20098000` | 1 KiB |

Ranges exclude their end. The producer reads
`elekloader.sdk.sharc.machine_layout('filter')` for its link bounds and
calls `filter_artifact(code, data, render_offset_bytes, machine_id,
stack_bytes)` with the prelinked bytes. Its arguments follow
[`voice_artifact`](MACHINES-DN2-VOICE.md#context-and-memory); empty data is
allowed. Compilation belongs to the producer. Keep writes in the author
data area and supplied sample buffer.

The controls come from the stock filter preparation, which runs for every
voice before the render loop and leaves the final cutoff in F8. The
linker points entry 0 of its setup table (`0x8052dba4`, internal type 0,
which only custom filters use) at the adapter's capture entry instead of
PC16 `0x1c927e`. That entry keeps F8, the resonance word and slot 66's
word, two words below it, for the voice (R3, the same slot the render hook
sees) in three 16-entry arrays at `0x20097d40`, `0x20097d80` and
`0x20097dc0`, preserves ASTATX, and continues at
`0x1c927e`. It makes no identity check; the render gate below is the only
one.

The linker binds the render PC and ID at `0x20097e00`. Its hook replaces
the six-byte table-pointer load at `0x28392b0e` and continues at PC16
`0x1c958a` after restoring that load. A matching raw filter ID dispatches
the callback when the stock internal filter type has settled to zero.
The first block after selection may still use the previous stock filter.
Other IDs follow the stock path.

`mods/machines-dn2/filter-bridge.s` produces 1,698 code bytes, padded to
2 KiB, SHA-256
`25ecf301d2dfc322b5311afa58cd00f89316f0afe7df66a45e62c0aca590538c`.
The [adapter rebuild instructions](MACHINES-DN2-VOICE.md#context-and-memory)
cover both machine adapters. Native sites are declared in
`mods/machines-dn2/mod.json`; the DSP hook and limits live in the device
profile.

Sources: [DigiKit's machine/parameter investigation](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/docs/findings/02-machines-and-parameters.md),
[native selector research](https://github.com/angellinares/dn2_firmware_explore/blob/39815895720b84c59ce2fa0ff5b13bd47dc415d2/docs/machine-list.md)
and [DSP stage research](https://github.com/angellinares/dn2_firmware_explore/blob/39815895720b84c59ce2fa0ff5b13bd47dc415d2/docs/waverider-feasibility.md).

## Checks

Build the [paired C example](https://github.com/simonjohansson/digitone-2-mod-hack); its
firmware includes SINE (synth ID 6), LOWPASS (filter ID 8) and DigiHealth.
LOWPASS is a resonant 2-pole state-variable filter, in assembly so that
it needs no stack, with per-slot state. Its cutoff follows FREQ like a
MIDI note (69/128 is 440 Hz) and the stock envelope; RESO sets its damping,
and its G, MODE, picks the low-pass (0..63) or the high-pass (64..127),
which its curve draws too.
Compare its response with the input and check RESET on retriggers, slot
reuse and returning from a stock filter.
Removing the filter must retain the synth and saved filter ID; reinstalling it
must restore selection. Check stock filter controls with MULTI-MODE.

Run `python tests/test_units.py` and the TypeScript suite for registration,
memory and artifact refusals. Run the four synth/filter combinations in
the coupled emulator and on the device for each changed workload. A host
or emulator pass does not qualify hardware timing.
