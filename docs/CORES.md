[elekloader docs](README.md) › Cores

# Cores

Users install mods from their `.elemod` files.
[FORMAT.md](FORMAT.md) describes both kinds:

- **format 1**: a whole custom build as one file;
- **format 2**: separate, linkable mods. A `core` mod provides a hook
  bus; other mods subscribe to its events and add entries to each other's
  tables. The loader's linker places them, resolves their symbols and
  checks them.

Every set of format-2 mods needs the **core** mod for its device, and
exactly one. The Windows and macOS apps have them built in, the web page
lists them, and each release carries them. Files from before version 0.2
used the `.dtmod` extension; they still load.

## Which file

There is one core file per device and OS:

| device | OS | core | where |
|---|---|---|---|
| Digitakt mk1 | 1.53 | `core-2.1.elemod` | the latest release (v0.4.0) |
| Digitakt mk1 | 1.54 | `core-2.1-os1.54.elemod` | the latest release (v0.4.0) |
| Digitakt mk1 | 1.53 | `core-3.0.elemod` | the [core-v3.0](https://github.com/irpina/elekloader/releases/tag/core-v3.0) pre-release |
| Digitakt mk1 | 1.54 | `core-3.0-os1.54.elemod` | the same pre-release |
| Digitakt mk1 | 1.53, 1.54 | `core-4.0.elemod`, `core-4.0-os1.54.elemod` | the [core-v4.0](https://github.com/irpina/elekloader/releases/tag/core-v4.0) pre-release |
| Digitone mk1, Digitone Keys | 1.43 | `core-dn1-2.3.elemod` | the [core-dn1-v2.3](https://github.com/irpina/elekloader/releases/tag/core-dn1-v2.3) pre-release (v0.4.0 has 2.0a) |
| Digitone mk1, Digitone Keys | 1.44 | `core-dn1-2.3-os1.44.elemod` | the same pre-release (v0.4.0 has 2.0a) |
| Digitone mk1, Digitone Keys | 1.43 | `core-dn1-3.2.elemod` | the [core-dn1-v3.2](https://github.com/irpina/elekloader/releases/tag/core-dn1-v3.2) pre-release |
| Digitone mk1, Digitone Keys | 1.44 | `core-dn1-3.2-os1.44.elemod` | the same pre-release |
| Digitone mk1, Digitone Keys | 1.43, 1.44 | `core-dn1-4.0.elemod`, `core-dn1-4.0-os1.44.elemod` | the [core-dn1-v4.0](https://github.com/irpina/elekloader/releases/tag/core-dn1-v4.0) pre-release |
| Digitakt II | 1.17 | `core-dt2-1.0.elemod` | not released yet: build it ([below](#building-a-core)) |
| Analog Rytm MKII | 1.73 | `core-ar2-0.2.elemod` | not released: the last core with 1.73; build it from commit 92a99c9 ([below](#building-a-core)) |
| Analog Rytm MKII | 1.74 | `core-ar2-0.3.elemod` | the [core-ar2-v0.3](https://github.com/irpina/elekloader/releases/tag/core-ar2-v0.3) pre-release, and the shop; 1.74 only |
| Analog Rytm MKII | 1.74 | `core-ar2-0.5.elemod` | not released: build it ([below](#building-a-core)); 1.74 only. Mods with pages built for 0.3 (rytm2_mods v1.2's lfo-rnd, voice-dac, euclid-accents, velocity-humanise, comp-preview) do not link with it: their sites overlap core's page sites |
| Analog Four MKII | 1.56 | `core-a4-0.2.elemod` | a proof of concept, not released: build it ([below](#building-a-core)) |
| Octatrack MKI, MKII | 1.40C | `core-ot-0.1.elemod` | the latest release (v0.4.0); PURGE SAMPLES freezes the unit with it, fixed in 0.3.1 |
| Octatrack MKI, MKII | 1.40C | `core-ot-0.3.1.elemod` | the [core-ot-v0.3.1](https://github.com/irpina/elekloader/releases/tag/core-ot-v0.3.1) pre-release, and the shop: with the hook bus (0.2) and the sized reserve (0.3), neither released on its own |
| Octatrack MKI, MKII | 1.40C | `core-ot-0.4.elemod` | not released yet: the frame's end and MIDI out (`ev_frame_end`, `ev_midi_out`, `ev_midi_out_rt`) |
| Model:Cycles | 1.13 | `core-mc-0.1.elemod` | on main, not released yet: build it ([below](#building-a-core)) |
| Model:Samples | 1.13 | `core-ms-0.1.elemod` | on main, not released yet: build it ([below](#building-a-core)) |

Each core keeps all of the one before it, on each OS both support: the same sites with the same
bytes, the same events and tables, and every symbol where the older core
has it (`tests/test_sdk.py` checks the Digitakt mk1's against the released
2.1 and the Digitone's against the released 2.3). So a mod built for an
older core links with the newest as it did with its own, and a build takes
the newest. The older files stay on their releases.

## How a build picks one

A mod says which core it needs at least (`resources.core` in the file,
`needs_core` in the shop's list). A build takes the newest core it has for
the stock OS: core 4.0 on the Digitakt mk1 and core-dn1 4.0 on the
Digitone, for a mod built for 2.1 or 2.3 too. A mod that needs a core
newer than any listed is refused with *"needs core ... or newer"*.

The 4.0 cores also give the mods' code, tables and variables more RAM:
1.25 MB on the Digitone, 256 KB on the Digitakt mk1, where the 2.x and 3.x
lines give them 128 KB ([ADAPTING.md](ADAPTING.md), "The mods' RAM"). How
a builder tells whether a core holds the mods: each mod's file gives the
most it can take of the area, its sections with their alignment and its
table entries, each rounded up to 4 (`ram_bound` in the mod list), and a
core holds the mods when theirs and its own add up to no more than its
area (`ram_area`). The linker never lays them out in more. A build takes
the newest core that holds them; when none does, the newest, and the
check says how many bytes are over.

The web page lists only the newest core of each device and OS (the site
carries the older ones too). The web page, the kit and the window (from
source, and the apps after 0.4.0) tick that core for you and keep one: a
core you ticked yourself (your own file, added to the page or the
window's library) stays while it is new enough and holds the mods, and
gives way to the newest when it does not. A profile the page saved with an
older core takes the newest in its place. The 0.4.0 apps refuse a build
with two cores ticked: there, untick the built-in one when you add a newer
core.

## What each core adds

- **Digitakt mk1** ([mods/core](../mods/core)): the hook bus, the shared
  events (tick, draw, key, encoder, SETTINGS, the render), and the
  machines list, where mods add SRC machines. From 3.0, with
  [machine-pages](../mods/machine-pages), a machine describes its own SRC
  page, so machine mods combine ([ADAPTING.md](ADAPTING.md), "Machine
  pages"). From 4.1 the master output (`ev_render_master`), pages of the
  mods' own among the master pages (`core_mpages`) and rows in SETTINGS >
  GLOBAL FX/MIX (`ev_fxmix`), so master effects combine
  ([ADAPTING.md](ADAPTING.md), "The master output and the master pages").
- **Digitone mk1** ([mods/core-dn1](../mods/core-dn1)): the same hook bus,
  plus a voice note-on event, parameter slots, mod pages and project data
  (2.1), and the Mod Menu (2.2, a grid of icons from 2.3):
  [below](#the-digitones-mod-menu). From 3.0 it also exports the firmware
  locations Digitone mods use, so a mod that names only those builds for
  1.43 and 1.44 unchanged ([ADAPTING.md](ADAPTING.md), "Firmware locations
  (core-dn1 3.0)"). From 3.1 it has what machines need: the voices' sound
  before the filter, stock parameters a mod relabels for its tracks, sound
  edits the voices hear and submenus in the Mod Menu. With
  [machines](../mods/machines-dn1), a sound plays a machine mod's voice in
  place of FM ([ADAPTING.md](ADAPTING.md), "Machines on the Digitone").
  From 3.2 a mod may take the whole output, with the synths silent, and
  keep large buffers in the profile's `bulk` area ([ADAPTING.md](ADAPTING.md),
  "Exclusive audio"), and take MIDI CCs before the stock applies them
  (`ev_midi_cc`). From 3.3 a mod sets key LEDs over the stock views'
  (`ev_key_leds`) and changes each block's sound parameters before the
  controllers and the LFOs (`ev_render_params`), and core exports more of
  the firmware: its C library (`fw_memcpy`, `fw_crc32`, the 64-bit shifts),
  the stock menus' text box, the transport, the active pattern and the
  project.
  From 4.0 a mod may instead take what the master stage made, an effect on
  everything the Digitone plays, with the synths on ("Insert audio"),
  the mods' code, tables and variables have 1.25 MB in place of
  128 KB ("The mods' RAM"), and a mod may take program changes before the
  stock applies them (`ev_midi_pc`, "Program changes").
- **Digitakt II** ([mods/core-dt2](../mods/core-dt2)): the hook bus's tick,
  draw, key and encoder events, and rows in SETTINGS > PERSONALIZE.
- **Analog Rytm MKII** ([mods/core-ar2](../mods/core-ar2)): the Digitakt
  mk1's core.s, settings.s and render.s with the Rytm's addresses: the hook
  bus's tick (about 20 a second), draw, key, encoder and SETTINGS events,
  and the render's entry and exit, which cover the digital path only (the
  sample voices, the delay and the reverb), not the analog voices. From 0.2,
  mod storage: fields a mod declares per sound, kit, global slot, pattern
  track, pattern or project, kept in bytes the stock OS never uses and
  placed from the build's lockfile, and an event after every project load
  and CREATE NEW ("Mod storage (core-ar2 0.2, Analog Rytm MKII)" in
  ADAPTING.md). From 0.3, 56 bits a sound (24 with 0.2), in the velocity
  routes' amount low bytes too, and pattern-track and pattern fields kept
  by stock's saves of one edited track, a pattern's settings or its locks,
  not only by whole-pattern saves. From 0.4, parameter pages: core owns
  the entries of the four page routines and asks the mods' `core_pages`
  and `core_params` entries, and builds each page key's list from stock's
  pages and the mods' `core_view_pages` entries (a page can be hidden; the
  key shows each page's title and a view keeps its page), so several mods
  with pages fit one build, behind the same key too; and the audio
  interrupt's load, `core_audio_load`, for work a mod can skip
  ("Parameter pages (core-ar2 0.4, Analog Rytm MKII)" in ADAPTING.md).
  From 0.5, machines: a mod's `core_machines` entry puts a machine of its
  own in one of the hidden slots 34..43, written into the machine table and
  the slot's parameter records at boot, and the linker refuses two mods
  that take one slot ("Machines (core-ar2 0.5, Analog Rytm MKII)" in
  ADAPTING.md). From 0.6, a page's entry can come from a function
  (`core_page_fns`), so its knobs can follow the selected sound. No
  firmware exports.
- **Analog Four MKII** ([mods/core-a4](../mods/core-a4)): core-ar2 0.3
  with the A4's addresses, every one measured on the A4's own image: the
  same hook bus (tick about 20 a second, draw, key, encoder, SETTINGS,
  the render's entry and exit, which cover the digital path: the effects,
  not the analog voices) and the same mod storage, in bytes of the A4's own
  sounds, kits, global slots and songs that the stock OS never uses
  ([mods/core-a4](../mods/core-a4) has where). Checked in rytm-emu's A4
  build; on a unit, the hook bus with hooks-check-a4 and mod storage with
  store-check-a4. From 0.2, core-ar2 0.4's parameter routines: core owns
  the entries of the four page routines and asks the mods' `core_pages` and
  `core_params` entries, adds the views mods list in `core_view_pages`
  behind the page keys (each A4 page is a view of its own), and
  `core_audio_load` ("Parameter pages (core-a4 0.2, Analog Four MKII)" in
  ADAPTING.md).
- **Octatrack** ([mods/core-ot](../mods/core-ot)): a source of its own. It
  reserves RAM for mods, copies their code there, and from 0.2 has a hook
  bus with the Octatrack's events; from 0.3 it reserves only the RAM the mods
  use, at most 10 MB. Beside it,
  [mods/dspbus-ot](../mods/dspbus-ot) is an optional bus for mods' DSP code,
  and [mods/intercepts-ot](../mods/intercepts-ot) (intercepts, with 0.3.1) gives
  places a mod intercepts (MIDI notes, the CHROMATIC keyboard, locks and knobs,
  the project file) to the one mod whose record claims each, with Modwerk's
  Octatrack base's events and names. [mods/machines-ot](../mods/machines-ot)
  (machines, with 0.3.1) gives source machines the chooser, SRC SETUP, the
  page resolver, the Part validator and the FLEX renderer, so several combine
  (`ot_machines`, the record of Modwerk's base of the same name).
  [mods/usb-ot](../mods/usb-ot) (usb, with 0.3.1) owns the stock USB stack's
  places for USB functions, mods of their own that combine:
  [mods/usb-midi-ot](../mods/usb-midi-ot) (USB MIDI, with 0.4) is the first,
  and [mods/usb-audio-ot](../mods/usb-audio-ot) (USB audio out, with 0.4, in
  five layouts) the second.
- **Model:Cycles, Model:Samples** ([mods/core-mc](../mods/core-mc),
  [mods/core-ms](../mods/core-ms)): the shared core.s with the hook bus's
  tick, draw, key and encoder events. Their OS entry makes its SRAM copy and
  its .bss clear in two calls, and the boot site covers both, so `.boot`
  copies the mods' DDR image out first. Like the Digitakt II's, they leave
  DMA timer 0 as the stock OS has it (`NO_DTIM0`).

### The Digitone's Mod Menu

Hold a track key (T1, say) on its own for about half a second, and core
lists what your mods add to it, as a grid of icons from core-dn1 2.3. Move
with the arrows or a knob and pick with YES, or pick with the entry's trig
key; NO closes it. With no mod that adds an entry, holding a track key does
nothing, as on the stock OS. Here digitables 1.3 and a test mod with seven
entries are linked, in digikit's emulator; [mods/core-dn1](../mods/core-dn1)
has more screens and how a mod adds an entry.

![The Digitone's Mod Menu: four tiles, each an icon over a label with its trig key's number, TABLES selected, and a scroll bar](img/dn-modmenu-grid.png)

## Building a core

One source, [mods/core/core.s](../mods/core/core.s), is built with each
device's addresses ([mods/core](../mods/core) for the Digitakt mk1,
[mods/core-dn1](../mods/core-dn1) for the Digitone mk1,
[mods/core-dt2](../mods/core-dt2) for the Digitakt II,
[mods/core-ar2](../mods/core-ar2) for the Analog Rytm MKII, [mods/core-a4](../mods/core-a4)
for the Analog Four MKII, [mods/core-mc](../mods/core-mc)
and [mods/core-ms](../mods/core-ms) for the Model:Cycles and the Model:Samples). Each `mod.json`
gives the addresses of every OS it supports (its `os`, and its `ports`):
the stock file you build with picks them. The Octatrack's has its own
source. Build any of them with the SDK, which needs the device's cross
toolchain ([ADAPTING.md](ADAPTING.md), "What you need"):

```bash
python -m elekloader.sdk.build mods/core --stock Digitakt_OS1.54.syx                          # the Digitakt mk1's (or 1.53)
python -m elekloader.sdk.build mods/core-dn1 --stock Digitone_and_Digitone_Keys_OS1.44.syx   # the Digitone mk1's (or 1.43)
python -m elekloader.sdk.build mods/core-dt2 --stock Digitakt_II_OS1.17.syx                      # the Digitakt II's
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx                  # the Analog Rytm MKII's (1.74 only from 0.3)
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx                   # the Analog Four MKII's
python -m elekloader.sdk.build mods/core-ot --stock OCTATRACK_OS1.40C.syx                    # the Octatrack's
python -m elekloader.sdk.build mods/core-mc --stock Model_Cycles_OS1.13.syx                  # the Model:Cycles'
python -m elekloader.sdk.build mods/core-ms --stock Model_Samples_OS1.13.syx                 # the Model:Samples'
python -m elekloader.sdk.build mods/dspbus-ot --stock OCTATRACK_OS1.40C.syx                  # its DSP bus (optional)
python -m elekloader.sdk.build mods/usb-ot --stock OCTATRACK_OS1.40C.syx                     # its USB companion (optional)
```

The SDK writes `out/core-<version>.elemod`; a release renames each as the
table above does ([RELEASING.md](RELEASING.md)).

A new core version may drop an old OS: core-ar2 0.3 to 0.5 build for the
Analog Rytm MKII 1.74 only, and 0.2 is the last core with 1.73. A build takes the
newest core it has for the stock OS ([above](#how-a-build-picks-one)), so a
1.73 build takes 0.2 when it is given, and a mod that needs 0.3 is refused
on 1.73 (*"needs core ... or newer"*). Build 0.2 for 1.73 from the tree it
was last in, commit 92a99c9 (`git worktree add ../core-ar2-0.2 92a99c9`,
then its `mods/core-ar2` with the 1.73 stock file); the SDK writes
`core-0.2.elemod` there.
