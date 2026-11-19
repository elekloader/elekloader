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

There is one core file per device and OS. For each device, the table
lists the newest released core, the one on main when it is newer, and
older ones people still have:

| device | OS | core | where |
|---|---|---|---|
| Digitakt mk1 | 1.53, 1.54 | `core-4.1.elemod`, `core-4.1-os1.54.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page |
| Digitakt mk1 | 1.53, 1.54 | `core-4.2.elemod`, `core-4.2-os1.54.elemod` | on main, not released: build it ([below](#building-a-core)) |
| Digitakt mk1 | 1.53, 1.54 | `core-2.1.elemod`, `core-2.1-os1.54.elemod` | the v0.4.0 release, built into its apps |
| Digitone mk1, Digitone Keys | 1.43, 1.44 | `core-dn1-5.2.1.elemod`, `core-dn1-5.2.1-os1.44.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page |
| Digitone mk1, Digitone Keys | 1.43, 1.44 | `core-dn1-5.3.elemod`, `core-dn1-5.3-os1.44.elemod` | on main, not released: build it ([below](#building-a-core)) |
| Digitone mk1, Digitone Keys | 1.43, 1.44 | `core-dn1-2.0a.elemod`, `core-dn1-2.0a-os1.44.elemod` | the v0.4.0 release, built into its apps |
| Digitakt II | 1.17 | `core-dt2-1.0.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page |
| Digitone II | 1.12 | `core-dn2-0.3.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page (experimental) |
| Analog Rytm MKII | 1.74 | `core-ar2-0.12.elemod` | on main: build it ([below](#building-a-core)). The Drive Loader needs it: the +Drive mods' spare entries in its tables |
| Analog Rytm MKII | 1.74 | `core-ar2-0.11.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page. Mods with pages built for 0.3 (rytm2_mods v1.2's lfo-rnd, voice-dac, euclid-accents, velocity-humanise, comp-preview) do not link with it: their sites overlap core's page sites |
| Analog Rytm MKII | 1.74 | `core-ar2-0.3.elemod` | the [core-ar2-v0.3](https://github.com/elekloader/elekloader/releases/tag/core-ar2-v0.3) pre-release, and the shop |
| Digitone II | 1.12 | `core-dn2-0.4.elemod` | on main, not released: build it ([below](#building-a-core)) |
| Analog Rytm MKII | 1.73 | `core-ar2-0.2.elemod` | not released: the last core with 1.73; build it from commit 92a99c9 ([below](#building-a-core)) |
| Analog Four MKII | 1.56 | `core-a4-0.5.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page (a proof of concept) |
| Octatrack MKI, MKII | 1.40C | `core-ot-0.8.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page |
| Octatrack MKI, MKII | 1.40C | `core-ot-0.1.elemod` | the v0.4.0 release: PURGE SAMPLES freezes the unit with it, so use 0.3.1 or newer |
| Model:Cycles | 1.13 | `core-mc-0.3.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page |
| Model:Samples | 1.13 | `core-ms-0.1.elemod` | the [v0.10.0](https://github.com/elekloader/elekloader/releases/tag/v0.10.0) release, built into its apps, and the web page |

Each core keeps all of the one before it, on each OS both support: the same sites with the same
bytes, the same events and tables, and every symbol where the older core
has it (`tests/test_sdk.py` checks the Digitakt mk1's against the released
2.1 and the Digitone's against the released 2.3). So a mod built for an
older core links with the newest as it did with its own, and a build takes
the newest. The older files stay on their releases (core-v3.0,
core-dn1-v2.3, core-dn1-v3.2 and core-dn1-v4.0).

One kind of mod does not link with a newer core: a mod that patches a
place itself which a later core took for every mod. The Digitakt mk1's
core 4.1 patches the master output's places, and Digi EQ 1.0c and
Tone+FX's Digitakt files (3.2a3 and 4.0c) patch them too: they link
with core 4.0 and are refused with 4.1 (*"core 4.1 site ... and tonefx
4.0c site ... overlap"*). Such a mod builds with the newest core that
leaves its places alone ([below](#how-a-build-picks-one)), and not beside
a mod that needs a newer core, until its author builds it on the core's
event.

## How a build picks one

A mod says which core it needs at least (`resources.core` in the file,
`needs_core` in the shop's list). A build takes the newest core it has for
the stock OS that takes its mods, for a mod built for an older core too. A
core takes the mods when:

- **it is new enough** for each of them. A mod that needs a core newer than
  any listed is refused with *"needs core ... or newer"*;
- **its RAM holds them** (the next paragraph);
- **it patches no place one of them patches** (the list each file gives of
  its sites). A mod that patches a place core 4.1 patches gets core 4.0
  when the builder has it: the web page and the kit do, the apps when you
  add the older core's file.

When no core does all three, the build takes the newest that is new
enough, and the check says what is wrong.

Cores 4.0 and newer give the mods' code, tables and variables more RAM:
1.25 MB on the Digitone, 256 KB on the Digitakt mk1, where older cores give
them 128 KB ([ADAPTING.md](ADAPTING.md), "The mods' RAM"). How
a builder tells whether a core holds the mods: each mod's file gives the
most it can take of the area, its sections with their alignment and its
table entries, each rounded up to 4 (`ram_bound` in the mod list), and a
core holds the mods when theirs and its own add up to no more than its
area (`ram_area`). The linker never lays them out in more. A build takes
the newest core that holds them; when none does, the newest, and the
check says how many bytes are over.

The web page lists one core of each device and OS: the one the build
takes. That is the newest, or an older one of the site's while a ticked
mod cannot link with the newest; the row then names that mod, and the
newest comes back when the mod is unticked. The web page, the kit and the
window (from source, and the apps after 0.4.0) tick the core for you and
keep one: a core you ticked yourself (your own file, added to the page or
the window's library) stays while it takes the mods, and gives way to the
newest that does when it does not. A profile the page saved with one of
the site's cores takes the core a build of its mods takes. The 0.4.0 apps
refuse a build with two cores ticked: there, untick the built-in one when
you add a newer core.

## What each core adds

Every core copies the mods' code into RAM at boot and gives them a hook
bus: events at the UI's tick, draw, key and encoder that mods subscribe to
([ADAPTING.md](ADAPTING.md), "The hook bus"). A companion, a mod beside the
core, owns a group of the firmware's places once, for every mod, so the
mods that need those places combine. Below is what each core and its
companions add. A version after a feature is the oldest core that has it,
the one a mod that uses it names in `resources.core`.

- **Digitakt mk1** ([mods/core](../mods/core)): SETTINGS rows, the render's
  entry and exit, and SRC machines past the stock four (ADAPTING.md, "SRC machines").
  Firmware locations as exports, so one mod source builds for 1.53 and 1.54
  (core 3.0; ADAPTING.md, "Firmware locations (core 3.0)"). Machines that
  describe their own SRC page, which the companion
  [machine-pages](../mods/machine-pages) draws, so machine mods combine
  (core 3.0; ADAPTING.md, "Machine pages"). The master output, the mods'
  own master pages and rows in SETTINGS > GLOBAL FX/MIX, so master effects
  combine (core 4.1; ADAPTING.md, "The master output and the master pages").
  The notes played live on a track, before its voice, which a handler may
  take, and the stock routines past them (`ev_live_note`, `fw_note_on`,
  `fw_note_off`; core 4.2), so mono machines play one key at a time.
  MIDI CCs before the stock applies them (`ev_midi_cc`, core 4.3).
- **Digitone mk1** ([mods/core-dn1](../mods/core-dn1)): the Digitakt mk1's
  events, and:
  - the voices' note-on, parameter slots, mod pages and project data
    (core-dn1 2.1);
  - the Mod Menu ([below](#the-digitones-mod-menu); core-dn1 2.2);
  - firmware locations as exports, so one mod source builds for 1.43 and
    1.44 (3.0, and 3.3 for the C library, the transport, the pattern and
    the project; ADAPTING.md, "Firmware locations (core-dn1 3.0)");
  - what machines need: the voices' sound before the filter, stock
    parameters a mod relabels (including LFO destinations in 5.2), sound
    edits the voices hear and submenus in the Mod Menu (3.1;
    ADAPTING.md, "Machines and stock parameters"). With
    them, the companion [machines](../mods/machines-dn1) plays a machine
    mod's voice in place of FM (ADAPTING.md, "Machines on the Digitone");
  - exclusive audio, one mod taking the whole output (3.2;
    ADAPTING.md, "Exclusive audio"), and insert audio, an effect on
    everything the Digitone plays (4.0; ADAPTING.md, "Insert audio");
  - MIDI CCs (`ev_midi_cc`, 3.2) and program changes (`ev_midi_pc`, 4.0)
    before the stock applies them (ADAPTING.md, "Program changes");
  - key LEDs over the stock views' and each block's sound parameters (3.3;
    ADAPTING.md, "Key LEDs and each block's sound parameters");
  - the note queue: each note with the time it plays and its step's
    p-locks, which [fxkit](../mods/fxkit-dn1) is built on (4.1;
    ADAPTING.md, "The note queue");
  - the parameter pages' knob turns and values, each voice once it holds
    its sound and p-locks, and project data blocks that save only what they
    hold (4.2; ADAPTING.md, "Parameter pages and the starting voice").
  - the firmware's moments, a prototype: the +Drive ready, the boot, USB, crashes and a safe restart, project
    saves, transport, the sequencer's tick, every MIDI message in and out, SysEx, voices released, the UI
    task's messages, sequencer edits, parameter commits and the master's effect stages (5.0; the core's
    README);
  - the sequencer's MIDI out (the MIDI tracks' messages, which `ev_midi_out` never sees), a message
    played in as if from DIN, a pool sound put on a track, the global mutes, and the key dispatch (5.0.1;
    the core's README);
  - lane parameters, a mod's values per track and per step with p-locks saved in the pattern, ten mod
    parameter ids, knobs on the stock pages' empty boxes and a second TRIG page, and each note the
    sequencer builds (5.1; ADAPTING.md, "Lane parameters, knobs on pages and TRIG's second page");
  - the notes played live on a track, as the Digitakt's core 4.2 has them (`ev_live_note`,
    `fw_note_on`, `fw_note_off`; 5.3), so mono machines play one key at a time.
- **Digitakt II** ([mods/core-dt2](../mods/core-dt2)): SETTINGS and
  PERSONALIZE rows, and `.fast` code in SRAM. No render events: its audio
  renders on the DSP.
- **Digitone II** ([mods/core-dn2](../mods/core-dn2), experimental): the
  Digitakt II's events and rows, without `.fast`; remote panel input and
  SysEx observers (ADAPTING.md, "Remote panel input and SysEx"); and SHARC
  mods, one mod's code on the DSP at the master output (core-dn2 0.2;
  ADAPTING.md, "SHARC mods"; the [callback contract](SHARC-DN2-ABI.md) and
  the [sine example](../examples/sharc-sine/README.md)); from 0.3 a DSP
  memory peek (ADAPTING.md, "The DSP peek"); from 0.4 MIDI CCs before the
  stock applies them (`ev_midi_cc`) and words posted to DSP code
  (`core_dsp_post`, ADAPTING.md, "The DSP peek"). The optional
  [dspbusy-dn2 companion](../mods/dspbusy-dn2/README.md) enables the busy
  measure for a build with a SHARC mod. The
  [machines companion](MACHINES-DN2.md) adds
  [selectable track synths](MACHINES-DN2-VOICE.md) and
  [custom filters](MACHINES-DN2-FILTER.md) through the `mk2_*` tables. An
  elekloader without SHARC support, such as the 0.4.0 apps, refuses a 0.2
  file, so 0.2 is released with an elekloader release.
- **Analog Rytm MKII** ([mods/core-ar2](../mods/core-ar2)): SETTINGS rows
  and the render's entry and exit, which cover the digital path only (the
  sample voices, the delay and the reverb), not the analog voices. No
  firmware exports. Mod storage: fields a mod keeps per sound, kit, global
  slot, pattern track, pattern or project (core-ar2 0.2;
  ADAPTING.md, "Mod storage (core-ar2 0.2)"). Parameter pages that several
  mods share, and pages whose knobs follow the selected sound (0.4 and 0.6;
  ADAPTING.md, "Parameter pages (core-ar2 0.4)"). Machines of the mods' own
  in the hidden slots (0.5; ADAPTING.md, "Machines (core-ar2 0.5)").
  A lockable parameter's value as stock would play it, smoothed with
  stock's one-pole (0.9; ADAPTING.md, "The smoothed value (core-ar2 0.9,
  Analog Rytm MKII)"), and as a destination of each sound's velocity and
  aftertouch routes (0.9; ADAPTING.md, "Lockable parameters"). Lockable
  parameters of the FX track, in kit fields (0.9; ADAPTING.md, "The FX
  track's parameters (core-ar2 0.9, Analog Rytm MKII)"). A field's default
  other than 0, its `init` (0.10; ADAPTING.md, "Mod storage (core-ar2 0.2,
  Analog Rytm MKII)"). P-locks a mod keeps itself, on knob ids from 555
  (0.11; ADAPTING.md, "Locks a mod keeps itself (core-ar2 0.11, Analog Rytm
  MKII)"). Spare entries in its tables for +Drive mods, which the Drive
  Loader fills at boot (0.12; the core's README, "Room for +Drive mods").
  The USB input's gain, one of stock's USB TO MAIN steps a mod holds in
  place of the setting (0.15; ADAPTING.md, "USB input gain (core-ar2 0.15,
  Analog Rytm MKII)").
- **Analog Four MKII** ([mods/core-a4](../mods/core-a4), a proof of
  concept): core-ar2's events and mod storage at the A4's addresses
  (ADAPTING.md, "Mod storage (core-a4 0.1)"), and parameter pages, whose
  knobs can follow the selected sound (core-a4 0.2 and 0.3; ADAPTING.md,
  "Parameter pages (core-a4 0.2)"). Lockable parameters for each track
  kind, their p-locks, their destinations, their value as stock would
  play it and their NRPNs and CCs (ADAPTING.md, "Lockable parameters (core-a4 0.5, Analog Four
  MKII)"). Its render events cover the effects, not the analog
  voices.
- **Octatrack** ([mods/core-ot](../mods/core-ot)): a source of its own. It
  takes the mods' RAM from the sample and recorder memory, only what they
  use and at most 10 MB, and has a hook bus of the Octatrack's own: the UI,
  MIDI, the audio frame, the transport, the project and the edits mods'
  data must follow, the voices, the track icons and the SETUP knobs
  (ADAPTING.md, "The hook bus on the Octatrack", and the sections after it,
  which name the core-ot each part needs). Its companions:
  - [dspbus-ot](../mods/dspbus-ot): the mods' DSP code;
  - [intercepts-ot](../mods/intercepts-ot): places one mod at a time takes
    (MIDI notes, the CHROMATIC keyboard, locks and knobs, the project
    file), with the events and names of Modwerk's Octatrack base;
  - [machines-ot](../mods/machines-ot): source machines, several in one
    build (`ot_machines`, Modwerk's record of the same name);
  - [usb-ot](../mods/usb-ot): USB functions, mods of their own that
    combine, [usb-midi-ot](../mods/usb-midi-ot),
    [usb-audio-ot](../mods/usb-audio-ot) and
    [usb-audio-in-ot](../mods/usb-audio-in-ot) (audio in, with the DSP bus);
  - [settings-ot](../mods/settings-ot): every mod's settings, in a MODS
    category of the MAIN MENU;
  - [trig-events-ot](../mods/trig-events-ot): each trig as the frame
    interrupt hands it to its machine, which a mod may veto, and (0.2) STOP's
    tail kill.
- **Model:Cycles, Model:Samples** ([mods/core-mc](../mods/core-mc),
  [mods/core-ms](../mods/core-ms)): the hook bus. They leave DMA timer 0
  as the stock OS has it (`NO_DTIM0`), so a mod cannot count on it as a
  time base. The Model:Cycles' core also has:
  - the firmware locations its mods use, as exports (`fw_project`,
    `fw_key_held`, `fw_fill`, the kits, the file system;
    `elekloader/sdk/include/model-cycles/core.h`; 0.2; ADAPTING.md,
    "Firmware locations (core-mc 0.2, Model:Cycles)");
  - `ev_boot`, a mod's work once at boot, before the OS starts, after
    `.boot` has invalidated the caches the bootstrap leaves on (0.3;
    ADAPTING.md, "Boot (core-mc 0.3, Model:Cycles)").

The companion seq-events gives the Digitone mk1
([mods/seq-events-dn1](../mods/seq-events-dn1)), the Digitakt mk1
([mods/seq-events-dt1](../mods/seq-events-dt1)) and the Octatrack
([mods/seq-events-ot](../mods/seq-events-ot)) the same step-fire events, so
mods that change what the sequencer plays combine, and are written once for
all three (ADAPTING.md, "Step-fire events").

### The Digitone's Mod Menu

Hold a track key (T1, say) on its own for about half a second, and core
lists what your mods add to it, as a grid of icons. Move
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
[mods/core-dn2](../mods/core-dn2) for the Digitone II,
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
python -m elekloader.sdk.build mods/core-dn2 --stock Digitone_II_OS1.12.syx                      # the Digitone II's
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx                  # the Analog Rytm MKII's (1.74 only)
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx                   # the Analog Four MKII's
python -m elekloader.sdk.build mods/core-ot --stock OCTATRACK_OS1.40C.syx                    # the Octatrack's
python -m elekloader.sdk.build mods/core-mc --stock Model_Cycles_OS1.13.syx                  # the Model:Cycles'
python -m elekloader.sdk.build mods/core-ms --stock Model_Samples_OS1.13.syx                 # the Model:Samples'
python -m elekloader.sdk.build mods/dspbus-ot --stock OCTATRACK_OS1.40C.syx                  # its DSP bus (optional)
python -m elekloader.sdk.build mods/usb-ot --stock OCTATRACK_OS1.40C.syx                     # its USB companion (optional)
```

The SDK writes `out/core-<version>.elemod`; a release renames each as the
table above does ([RELEASING.md](RELEASING.md)).

A new core version may drop an old OS: core-ar2 0.3 and newer build for the
Analog Rytm MKII 1.74 only, and 0.2 is the last core with 1.73. A build takes the
newest core it has for the stock OS ([above](#how-a-build-picks-one)), so a
1.73 build takes 0.2 when it is given, and a mod that needs 0.3 is refused
on 1.73 (*"needs core ... or newer"*). Build 0.2 for 1.73 from the tree it
was last in, commit 92a99c9 (`git worktree add ../core-ar2-0.2 92a99c9`,
then its `mods/core-ar2` with the 1.73 stock file); the SDK writes
`core-0.2.elemod` there.
