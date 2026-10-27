# Digitone II machines

Experimental, OS 1.12 only. `machines-dn2` is a companion to core-dn2 0.2.
It puts [SHARC track synths](MACHINES-DN2-VOICE.md) in the native
**FUNC+SYN** selector and [filters](MACHINES-DN2-FILTER.md) in its FLTR
category, using the `mk2_*` interface in `mk2/machines.h`. A mod registers
a descriptor and carries its DSP code, without firmware addresses or hook
sites of its own; those pages show how.

Each descriptor starts with a permanent ID: 5..31 for synths, 6..31 for
filters, two separate namespaces. Give each independently distributed mod a
different ID. The linker reads the descriptor's ID and refuses conflicts,
even if a mod omits a resource claim. Keep the ID when adding/removing
other mods or changing their order; never reuse it for a different mod.
There is no global ID allocator yet: agree IDs when combining mods.

The stock OS only knows stock types, so the companion shows it every custom
machine as its kind's **host type**: FM TONE for a synth, LOWPASS 4 for a
filter. A synth takes FM TONE's track permissions, and its knobs FM TONE's
parameter ownership and MIDI numbers; a filter takes LOWPASS 4's FLTR page,
defaults and parameters. Identity reads, saving and the audio frame keep
the custom ID, so the mod's own kernel runs. The host is fixed by the
companion, not chosen by a mod.

The menu lists stock synths first, added synths by stable ID, then MIDI.
The FLTR category lists the stock filters, then added filters by ID.

With the companion present, a saved synth ID 5..31 whose mod is absent is kept,
named MISSING by the name accessors and omitted from the selector. Its DSP
type maps to MIDI (silence), rather than another mod's synth. Its synth
MIDI CCs and NRPNs do nothing, so they cannot overwrite the saved values
with a MIDI track's parameters. The native
loader also checks assignment permissions, so missing IDs use MIDI's track
mask to allow the saved sound to load without replacing its identity. A
missing filter is described in [filters](MACHINES-DN2-FILTER.md). Removing the companion
restores stock OS behavior, which clamps an unknown saved type to FM TONE
when loading a sound. Keep backups of projects; do not save over the only
copy when changing the installed mod set.

## Maintainer checks

The manifest in `mods/machines-dn2/mod.json` holds every exact OS 1.12 byte
guard and address. The adapters cover the cached list's allocation/end,
name/descriptor/permission lookups, synth grouping, three parameter-page
readers, parameter ownership, the audio frame's type and the saved type
reader. Identity callers retain the real type; page logic sees the host
type, except the FLTR page's knob lookups (`0x4006efcc`, `0x4006f004`),
which read a custom filter's own descriptor, where G may be declared. The audio frame is indexed by allocated voice slot, not by track;
qualification follows the owner map at `0x40288306 + 0x14*v` during note-on.
A small wrapper gives the native track-type accessor the real reader without embedding a copied firmware function. SHARC synth pages add
parameter rows after the stock table's 321: at boot, before the OS's
initializers, the companion copies the table (`0x401f83a4`) to its RAM,
points the table's 56 references at the copy and raises its 54 row bounds
by 41, for 40 synth knobs and a filter's G; the value-class accessor (`0x400c257c`) gives an added row its
template's object, and the bounds that end loops keep the stock count. The CC
and NRPN lookups (`0x400dc978`, `0x400dca5a`) ask FM TONE's map for a paged
synth, and an FM TONE row found there becomes the synth's row for its slot. The sound-load bound
accepts IDs through 31. Unused IDs never enter the cached list.

Firmware-free C checks exercise registration order, gaps, missing mods,
invalid descriptors and stock behavior:

```sh
cc -std=c99 -Wall -Werror -I elekloader/sdk/include tests/test_dn2_machines.c -o /tmp/dn2-machines-test
/tmp/dn2-machines-test
cc -std=c99 -Wall -Werror -DEMPTY -I elekloader/sdk/include tests/test_dn2_machines.c -o /tmp/dn2-machines-empty-test
/tmp/dn2-machines-empty-test
```

`python tests/test_units.py` runs the C checks when a native `cc` is
available (and reports a skip otherwise). It and the TypeScript suite
check conflicting and distinct IDs. Runtime qualification must also
exercise the native selector, stock-equivalent audio, both categories, another track, project save/load
and the missing-mod control. Firmware builds alone do not qualify these.

Sources: [DigiKit's OS 1.12 machine paths](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/docs/findings/02-machines-and-parameters.md),
[cached list](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/docs/findings/33-runtime-machines-dn2.md),
[stable IDs](https://github.com/ElekLabs/digikit/blob/38dfbe4ae88263511eed95691c07fc07e17b6f26/docs/findings/52-sticky-machine-ids.md)
and [reader/load corrections](https://github.com/angellinares/dn2_firmware_explore/blob/39815895720b84c59ce2fa0ff5b13bd47dc415d2/docs/machine-list.md).
