# Digitone II filters

Experimental, OS 1.12 only. `machines-dn2` 0.1 registers stock filter
clones and custom SHARC filters in **FUNC+SYN → FLTR**. Filter selection
is independent of the synth: a custom filter can process SINE or a stock
synth. Require core-dn2 0.1. Timing, full-polyphony and long-duration
stability are unqualified.

## Registration

```c
#include "mk2/machines.h"
const struct mk2_filter my_filter = {6, 0, "CLONE FILTER", "CLONE"};
```

Contribute its pointer to `mk2_filters`, with the normal SDK manifest:

```json
"contribute": [
 {"to": "mk2_filters", "data": "00000000",
  "relocs": [[0, "abs32", "sym:my_filter", 0]]}
]
```

Require `core` and `machines-dn2`, and set `resources.core` to `"0.1"`.
The fields are permanent ID **6..31**, stock filter **0..5**, name (ASCII,
at most 31 characters) and label (at most 7). Stock types are MULTI-MODE,
LOWPASS 4, EQUALIZER, COMB-, LEGACY LP/HP and COMB+, respectively. The linker
refuses duplicate filter IDs and stock-ID conflicts. A synth and filter may
use the same numeric ID: their namespaces and stored bytes are separate.
Never reuse a distributed filter's ID for a different filter.

The selector lists the six stock filters in their native order, followed
by registered filters in ID order. For clones, pages, defaults, parameters
and audio use the chosen stock filter. Saving and copying retain the ID.
A missing registration keeps its saved ID, disappears from the selector
and uses **MULTI-MODE**, with the existing parameter values. This can change
the sound. Keep project backups when changing installed mods. Removing the
companion restores the stock loader's unknown-ID clamp.

The `mk2_*` interfaces are implemented only for Digitone II. Digitakt II
needs independently qualified firmware adapters and limits. Synths use
`struct mk2_synth` from the same header.

## Custom DSP

Contribute the same four-field descriptor to `mk2_voice_filters` instead
of `mk2_filters`. The stock-type field must still be 0..5, but custom
filters use only the common stock defaults; filter-specific parameter
slots 66..68 have no mapping. Its FLTR page has no controls, but the common
stock envelope graphic remains. The same mod must carry a
`dn2-filter-mono32-l2-v1` artifact with the matching `machine_id` and
`entries: {"render_offset_bytes": ...}`. The linker refuses a missing or
mismatched payload, conflicting filter IDs, or the same ID registered as
both a clone and a custom filter.

```c
#include "digitone-mk2/filter.h"
void sharc_filter_render(const struct dn2_filter_input *in);
```

The twelve-byte input contains `float *samples`, `uint32_t slot` and
`uint32_t flags`.
Process 32 mono floats at 48 kHz in place before the stock AMP stage and
return nothing. Slot is 0..15, not a track number; keep independent state
per slot. `DN2_FILTER_RESET` is set on the first callback after entering this filter
and on a new trigger edge. Clear that slot's history before processing
the block when this flag is set. Keep history between ordinary callbacks.
The callback receives no pitch or filter controls.
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

The linker binds the render PC and ID at `0x20097e00`. Its hook replaces
the six-byte table-pointer load at `0x28392b0e` and continues at PC16
`0x1c958a` after restoring that load. A matching raw filter ID dispatches
the callback when the stock internal filter type has settled to zero.
The first block after selection may still use the previous stock filter.
Other IDs follow the stock path.

`mods/machines-dn2/filter-bridge.s` produces 1,566 code bytes, padded to
2 KiB, SHA-256
`dea368ada96b1b08665e6f3399111aef43c84281a2fe4f85f585acd288e9dd32`.
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
LOWPASS uses per-slot history and a fixed coefficient of 1/16 at 48 kHz
(about 493 Hz cutoff). Compare its response with the input and check RESET
on retriggers, slot reuse and returning from a stock filter.
Removing the filter must retain the synth and saved filter ID; reinstalling it
must restore selection. Check stock filter controls with MULTI-MODE.

Run `python tests/test_units.py` and the TypeScript suite for registration,
memory and artifact refusals. Run the four synth/filter combinations in
the coupled emulator and on the device for each changed workload. A host
or emulator pass does not qualify hardware timing.
