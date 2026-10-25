# Digitone II synth clones

Experimental, OS 1.12 only. `machines-dn2` is a companion to core-dn2 0.1.
It also supports [SHARC track synths](MACHINES-DN2-VOICE.md) and independent
[filters](MACHINES-DN2-FILTER.md), using the `mk2_*` interface.
It puts external synth mods in the native **FUNC+SYN** selector. A clone
keeps its own saved machine ID but uses a stock synth's DSP and parameter
pages. A synth clone provides no custom DSP code.

A mod needs one descriptor and one contribution, without firmware addresses
or hook sites of its own:

```c
#include "mk2/machines.h"
const struct mk2_synth my_synth = {5, 0, "CLONE", "CLONE"};
```

```json
{
 "id": "synth-clone", "version": "0.1", "license": "GPL-2.0-or-later",
 "device": "digitone-mk2", "os": "1.12", "sources": ["clone.c"],
 "contribute": [
  {"to": "mk2_synths", "data": "00000000",
   "relocs": [[0, "abs32", "sym:my_synth", 0]]}
 ],
 "resources": {"core": "0.1"}, "requires": ["core", "machines-dn2"]
}
```

The fields are the permanent ID (5..31), the stock synth (0 FM TONE,
1 WAVETONE, 2 FM DRUM, 3 SWARMER), the name (ASCII, at most 31 characters)
and the short label (at most 7). Give each independently distributed mod a
different ID. The linker reads the descriptor's ID and refuses conflicts,
even if a mod omits a resource claim. Keep the ID when adding/removing
other mods or changing their order; never reuse it for a different mod.
There is no global ID allocator yet: agree IDs when combining mods.

Build the companion and your mod with the normal SDK, then combine them
with core-dn2 and DigiHealth. No SHARC compiler is needed for a stock clone.
Do not include a master-output replacement when checking the clone's sound.
The companion leaves section 7 unchanged.

```sh
python -m elekloader.sdk.build mods/core-dn2 --stock Digitone_II_OS1.12.syx --out out/core
python -m elekloader.sdk.build mods/machines-dn2 --stock Digitone_II_OS1.12.syx --out out/machines
python -m elekloader.sdk.build /path/to/my-mod --stock Digitone_II_OS1.12.syx --out out/clone
python -m elekloader.patch --stock Digitone_II_OS1.12.syx --mod out/core/core-0.1.elemod --mod out/machines/machines-dn2-0.1.elemod --mod out/clone/synth-clone-0.1.elemod --mod /path/to/digihealth.elemod --out out/clone.syx --version MC01
```

The menu lists stock synths first, added synths by stable ID, then MIDI.
The synth clone shares its original's SYN, FLTR and AMP pages; page titles
retain the stock synth's name. The FLTR category independently lists any
registered stock clones and custom filters.

With the companion present, a saved ID 5..31 whose mod is absent is kept,
named MISSING by the name accessors and omitted from the selector. Its DSP
type maps to MIDI (silence), rather than another mod's synth. The native
loader also checks assignment permissions, so missing IDs use MIDI's track
mask to allow the saved sound to load without replacing its identity. Removing the companion
restores stock OS behavior, which clamps an unknown saved type to FM TONE
when loading a sound. Keep backups of projects; do not save over the only
copy when changing the installed mod set.

## Maintainer checks

The manifest in `mods/machines-dn2/mod.json` holds every exact OS 1.12 byte
guard and address. The adapters cover the cached list's allocation/end,
name/descriptor/permission lookups, synth grouping, three parameter-page
readers, parameter ownership, the audio frame's type and the saved type
reader. Identity callers retain the real type; page logic sees the stock
clone. The audio frame is indexed by allocated voice slot, not by track;
qualification follows the owner map at `0x40288306 + 0x14*v` during note-on.
A small wrapper gives the native track-type accessor the real reader without embedding a copied firmware function. The sound-load bound
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
