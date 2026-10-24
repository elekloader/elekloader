# core-a4

The boot copier, the hook bus and mod storage for the Analog Four MKII, OS
1.56. It builds the Digitakt mk1's `core.s`, `settings.s` and `render.s` with
the A4's addresses, which `mod.json` gives (`defsym`, `sites`), as
[core-ar2](../core-ar2) does for the Analog Rytm MKII, and its own `store.c`
and `store.s` (Mod storage, below). On its own it changes nothing the unit
does.

The A4 MKII is the Rytm MKII's platform (a ColdFire MCF5441x): its OS entry,
bootstrap loader and UI loop are the Rytm's, instruction for instruction
apart from addresses, so each site below is the counterpart of one core-ar2
hooks on Rytm 1.74, found by masked matching of the two images and checked
by the code around it (`elekloader/devices/analog_four_mk2.py` has the
platform evidence).

| event | A4 1.56 | Rytm 1.74 | note |
|---|---|---|---|
| boot | 0x40001180 | 0x400008c0 | the OS entry's third call, VBR := 0x40000000 (0x40002064) |
| `ev_tick` | 0x40098898 | 0x400a2c5a | about 20 Hz, as the Rytm's |
| `ev_draw` | 0x40098900 | 0x400a2cc2 | drawAll(ctrl, bmp), 128 x 64 |
| `ev_key` | 0x400999f8 | 0x400a4838 | |
| `ev_enc` | 0x40099a40 | 0x400a4882 | |
| `ev_settings` | 0x400d05d2 | 0x400db8ea | the settings menu's builder, 0x400cff5a (the GLOBAL key's menu); rejoins 6 bytes on |
| `ev_render_in` | 0x401013c6 | 0x4011fb6e | the audio interrupt (vector 191, 0x401013be) |
| `ev_render_out` | 0x401040f6 | 0x401216c2 | its only exit; the frame is 0xec (the Rytm's 0xb4) |

The routines `core_additem` calls are the Rytm's, the same code with the
addresses in it masked: `OP_NEW` 0x400653b8, `ITEM_CTOR` 0x4005c7c4,
`MENU_ADD` 0x4005bffa and `FN_MGR` 0x40164940. The A4 has no manager for the
Rytm's function-pointer type (`PFSs15FileStorageInfoE`); 0x40164940 is the
only one with this code, for `PFSsR15FileStorageInfoPhE`, and its code is the
same for any function-pointer type.

What differs from the Rytm at these sites:

- **The UI object.** The UI routine (0x40098754) is the Rytm's with its
  object's fields moved (the idle counter +0x918, the Rytm's +0x748); it
  loads the view controller from +0x20 and skips the frame when it is null,
  as the Rytm's does.
- **The key and encoder calls.** The main loop pushes the brain from the
  stack, not from a frame (`2f2f0044`, `2f2f0048`); the callee sees the same
  arguments.
- **The settings menu** opens with the GLOBAL key (PROJECT, GLOBAL SLOT,
  MIDI CONFIG, CV CONFIG, SYSEX DUMP, …, AUDIO ROUTING, CONTROL, SYSTEM);
  rows added through `ev_settings` come after SYSTEM.
- **The render.** The A4's voices are analog; the audio interrupt is the
  digital path (effects and the inputs). Another vector 191 handler
  (0x401162a4) exists, as on the Rytm; core hooks only the main one.

## Parameter pages (0.2)

From 0.2, `pages.s` is core-ar2 0.4's parameter routines at the A4's
(docs/ADAPTING.md, "Parameter pages (core-a4 0.2, Analog Four MKII)"), and
core builds core-ar2's `load.s` for `core_audio_load`. Each A4 site is the
Rytm 1.74 routine's counterpart, found by its code and its callers:

| routine | A4 1.56 | Rytm 1.74 | displaced | note |
|---|---|---|---|---|
| page_info | 0x400f6212 | 0x400ff5e0 | `moveq #47,%d1; movel %sp@(4),%d0` | stock's pages are 0..47 (the Rytm's 0..11); an entry is 0x2c bytes, the name and ten knob ids, in a table at 0x401b31e8 |
| page_get_value | 0x40023d54 | 0x40038462 | `lea %sp@(-12),%sp; moveml %d2-%d3/%a2,%sp@` | the Rytm's prologue and frame; slot 21 of each of the 9 page views' vtables |
| param_apply_delta | 0x400232e2 | 0x400378a6 | the same | slot 22 |
| param_value_text | 0x4009ea68 | 0x400a864c | `movel %d3,%sp@-; movel %d2,%sp@-; movel %sp@(20),%d2` | the same arguments; slot 30 of the ParameterSet vtables (the Rytm's 21) |

The redraw after a turn a mod takes is VIEW_INVALIDATE, 0x40061a58 (the
Rytm's 0x40078580, the same code). No page site overlaps one of 0.1's.

core-ar2 0.4's page-key lists become views here. The A4's main screen
makes one view per page, 29 in all (0x400c1366, indices 0..28), and each
page key steps through a list of view indices for each track type (every
track, synth, FX, CV), kept in maps at +0x1a4 of the main screen. Core
hooks where the constructor has made its last view and not yet indexed
them (0x400c2370, resuming at 0x400c2378): for each `core_view_pages`
entry it makes a parameter page view as stock makes view 28 (operator
new(0x1c4), the view's constructor 0x4002522c(obj, page, key), the shared
pointer 0x40171460, the views map's slot 0x40171da8 at +0x1bc, 0x401713e0,
0x40144764) and appends its index to the key's list (0x40172bae, 0x40171b9e,
then stock's push_back or 0x40171558). Stock's loop after the hook gives
every listed view its key, place and length. The page key's step
(0x400c4202, resuming at 0x400c420a) skips a core view whose `shown`
answers 0, then selects as stock does (0x400bea38).

The current view is kept for each track type and saved: a signed byte
each in the project settings (storage +8..+0xA, written by 0x4000aabe on
project save and SysEx, and kept over power-off). The setter (0x400add08)
keeps it to 0..28 and the loader (0x4000a6d2) takes anything above 28 to 0,
PERFORMANCE. Core widens both to 0..44 (the moveqs at 0x400add22,
0x400add28, 0x4000a758, 0x4000a774, 0x4000a790); an index with no view in
a build falls back as stock's do (0x400c0470), and stock alone opens such a
project on PERFORMANCE. Nothing else in the OS bounds a view index.

The FX track already keeps its page when it is shown again, and each view
shows its own title, so core-ar2's FX fix and title push have no
counterpart.

In rytm-emu's A4 build (22 Oct 2026), core 0.2 alone (A402) drew every
screen checked (main, AMP, FILTERS, OSC1, an FX track's CHORUS, with
knobs turned) identical to stock's, pixel for pixel; with pages-check-a4
(PC56), AMP PAN read its value in hex (centre `40`), the bar counted each
step, and every other knob was stock's. `core_audio_load` read about 0x7a
(48%) there, an emulator figure. With the views, A402 drew all 90 screens
of every page key (pressed one to three times, on synth, FX and CV tracks)
as stock does; on PC56 the AMP key stepped AMP, then CHECK once PAN had
turned, with CHECK's title and its knob A working; CHECK came back after a
power cycle and through a SysEx settings dump. The same project opened on
MIXER with core alone (the index kept, with no view: stock's own fallback,
until a page key is pressed) and on PERFORMANCE with stock. The OS uses a
view's page id only through page_info and to pick the parameter set
(0x400a647c, which singles out pages 1, 3, 4, 5 and 47), so mod pages from
48 are safe.

On a unit (OS 1.56, 23 Oct 2026) [hw], PA56 (core 0.2 with pages-check-a4
and analoghealth's remote keys) passed the same plan as in the emulator,
every screen checked identical to the emulator's: every synth, FX and CV
page as on stock; AMP staying on AMP until PAN turned; PAN in hex and the
bar growing; CHECK with knob A as PAN and the others hatched; AMP, CHECK,
AMP. A project saved on CHECK opened on CHECK after loading another
project and loading it back, and after a power cycle (with the bar
cleared, as it is not saved); stock 1.56 opened it on PERFORMANCE. On fast
turns the bar grows once per accelerated turn update, not once per value
step; on slow turns they match. The SysEx settings round trip and core
alone (A402) were not run on the unit.

## Mod storage

Mods declare fields in their mod.json (`storage`) and read and write them
with `core_store_get` and `core_store_set`, which mask interrupts while they
do; elekloader places the fields from the build's lockfile.
`elekloader/sdk/include/analog-four-mk2/store.h` is the mod writer's header,
docs/ADAPTING.md ("Mod storage") the guide (core-ar2's design), and
`store.c`'s opening comment has the byte layout.

Every byte used is one the stock OS never reads, kept in stored bytes it
never writes or only ever writes as a copy of the live home or as 0; no
stock function gives up space:

| scope | live, while in use | saved in |
|---|---|---|
| sound | live sound +0xd4, +0xd5, +0xe0, +0xe1 (32 bits) | stored sound +0xdc, +0xdd, +0xe8, +0xe9 (stock's raw copy of the live bytes), the layout ID at +0xea, +0xeb, +0xf1, +0xf2 and the generation at +0xf3 |
| kit | live kit +0x950..+0x95f (128 bits) | stored kit +0x5b0..+0x5bf (its FX block's +0x18, never saved or loaded), the ID at +0x5c0, the generation at +0x5c4 |
| global | `core_store_globals` (8 bytes a slot) | stored slot: bytes +3..+7 of its first three 16-byte entries (+0xd1), which stock writes as 0 and never reads, with the ID and generation after the data |
| pattern track | live track +0x2ea (64 bytes) | the tagged store |
| pattern | live pattern +0x16eda (64 bytes) | the tagged store |
| project | the tagged store | the tagged store |

The tagged store is byte +0 of each of the 256 four-byte entries of the 17
songs in a project image (the working song and 16), 4,352 bytes, as on the
Rytm: the song save and load use bytes +1..+3 only. The stock project file
save serializes the whole project into a buffer, so core hooks the file
save and load and the save to a buffer, and carries the store across.

On the A4 the stored project (0x4045c974, which the OS keeps across power
cycles) takes stock's own edits part by part: an edited track, a pattern's
settings and locks (after a pattern edit in grid recording), each parameter
of the current kit (straight into the stored copy of that kit), a global
slot's settings. SAVE PROJECT AS writes a project file from the live
project and the stored project's name only. So:

- **Per-part saves**: each of stock's saves of a track, a pattern's settings,
  one lock or all locks also writes the pattern's track and pattern fields
  into the store, as core-ar2 0.3 does.
- **The current kit**: `core_store_set` writes a field of the current kit or
  of one of its sounds through to the stored copy of that kit, with its ID
  and generation, as stock writes each parameter edit there. Other kits and
  the pool reach the stored project through kit saves.
- **SAVE PROJECT AS**: stock writes only the project's name into the stored
  project, its own data being there already. After a whole save of the live
  project into another image (the file, a buffer), core copies the bytes it
  owns from that image into the stored project, part by part as the save
  wrote them (global slots, kits and their sounds, the pool, the tagged
  store), so a power cycle straight after the save keeps the mods' fields as
  it keeps stock's. Only core's bytes are written.

The 28 storage sites, by `jmp` unless marked, each with its displaced
instructions in `store.s`:

| site | A4 1.56 | why | Rytm 1.74 |
|---|---|---|---|
| pattern save / load | 0x4000a160 / 0x40009f60 | pattern tracks, patterns, and the project region with the working pattern | 0x4000937c / 0x40009146 |
| sound save / load | 0x400091bc / 0x40008808 | the sound's ID and generation; its 4 bytes | 0x4000843a / 0x400081f6 |
| kit save / load | 0x4000962a / 0x40008f2a | the kit's 16 bytes, ID and generation | 0x400088d6 / 0x4000863e |
| global save / load | 0x4000837c / 0x40007bc6 | the slot's 8 bytes, ID and generation | 0x40007a5c / 0x4000763c |
| pattern init, track init | 0x40015d70, 0x40015b74 | clear the fields (stock leaves the spare bytes as they were) | 0x40011ab8, 0x400118e8 |
| sound init, kit init | 0x40016890, 0x40015a58 | the same (the Rytm has no kit init site) | 0x400122b6, — |
| project init | 0x40016242 | CREATE NEW and the project reset: a fresh store | 0x40011d38 |
| project load | 0x40011054 | after any load: drop scopes another layout wrote, write this build's header | 0x4000cd2e |
| buffer, file and background saves (`keep2`) | 0x40020772, 0x400a77e4, 0x400a503c | which image is being saved | 0x40034d86, 0x400b0870 |
| file load (`keep2`) | 0x400209ec | which image is being loaded | 0x40035000 |
| SysEx song copy, write, swap (`keep2`) | 0x40154444, 0x40154d56, 0x40154f74 | copy whole stored songs but leave the target's store bytes | 0x4018b8b8, 0x4018c266, 0x4018c488 |
| SysEx pattern copy, write, swap (`keep2`) | 0x401547e8, 0x401553ea, 0x40155618 | clear the target pattern slot's fields, which can't follow the pattern | 0x4018bc5c, 0x4018c902, 0x4018cb34 |
| track save (`keep2`) | 0x400b40ee | an edited track saved by itself (0x40009b9c): the pattern's track and pattern fields too | 0x400bf818 |
| pattern settings save (`keep2`) | 0x400a19f6 | the same, for the settings (0x40009834) | 0x400aacc4 |
| locks save, one / all (`keep2`) | 0x400a2fda / 0x400a2ff6 | the same, for the parameter locks (0x4000a1e6, 0x4000a02c) | 0x400ac2ce / 0x400ac2ec |

After CREATE NEW and after every project load, core calls the mods'
`ev_project` handlers with what happened and which scopes came in as zeros.

What differs from core-ar2 0.3:

- **No velocity routes** (`route.s`): the A4's sound has spare bytes of its
  own, so its 32 bits need no stock field's unused low bytes.
- **Global slots carry their own layout ID**, as sounds and kits do: a
  global slot sent and received as SysEx keeps its mod data in the same
  layout (the Rytm's slot has no room for one and drops it).
- **The kit keeps 128 bits**, and a kit init site clears them.
- **The current kit's write-through, and the copy into the stored project
  after SAVE PROJECT AS** (above).
- **Six tracks a pattern**: the pattern-track scope is 129 x 6 items, so a
  track field of up to 2 bytes each fits the default split and 3 bytes fit
  too (the Rytm's 13 tracks don't).

What it does not do:

- **Fields of other kits and of the pool** set by a mod reach the stored
  project at the next stock save of that kit or sound, or the next SAVE
  PROJECT AS, not at once; the current kit's reach it at once.
- **Track and pattern fields** reach the stored project at the next stock
  save of that pattern's track, settings or locks, or the next SAVE PROJECT
  AS, as on the Rytm.
- **Patterns copied by the SysEx project commands** don't take their
  pattern-track and pattern fields with them; core clears the target slot's.
- The spare sound and FX words reach the engine through stock's parameter
  pushes, into the slot every unused parameter goes to; that nothing reads
  that slot is inferred from the code, and on a unit (21 Oct 2026) a
  pattern sounded the same by ear before and after store-check-a4 set
  every sound's markers.

## Checked so far

On a unit (OS 1.56, 20 Oct 2026) [hw]: build HC56 (core-a4 with
[hooks-check-a4](../../examples/hooks-check-a4)) booted and passed every
check: the tick, draw, key, encoder and SETTINGS events and the render's
entry and exit. Build SC56 (core-a4 with
[store-check-a4](../../examples/store-check-a4), 21 Oct 2026) read every
marker back after SAVE PROJECT AS and a power cycle, loading another
project (zeros, as it should) and loading the project back, a power cycle,
and a Transfer round trip of the project file; CREATE NEW cleared every
field, and on the new project, marking and a power cycle without saving
kept the project fields. With every marker set, a pattern on all four
synth tracks and the effects sounded the same by ear as before. With
[ddr-canary-a4](../../examples/ddr-canary-a4) (CA56), no word changed in the
mods' area after the mods' data or in the two unused gaps, through a session
of normal use (Overbridge included) and after a power cycle (21 Oct 2026).
Recovery (the STARTUP menu, TRIG 4, the stock file over MIDI DIN) brought
the unit back to stock 1.56 (21 Oct 2026). With core 0.2 and
[pages-check-a4](../../examples/pages-check-a4) (PA56, 23 Oct 2026), the
parameter routines and the views passed every check ("Parameter pages
(0.2)" above).

In an emulator (rytm-emu, with a local change that accepts modified A4
images; 20 Oct 2026):

- **The hook bus** [emu]: with core alone (A401) the unit boots as stock, with
  the same frames and a final screen identical to stock's.
  [hello-marker-a4](../../examples/hello-marker-a4) (HM56) differs from stock
  only at its 3x3 square, on the main page, the OSC1 page, after edits and
  in the settings menu. [hooks-check-a4](../../examples/hooks-check-a4)
  (HC56): every event fired; the tick at about 20 Hz, keys and encoder
  detents counted, the render's entry and exit counts never apart, and the
  HOOK TEST row listed after SYSTEM and counted when selected.
- **Mod storage** [emu], with [store-check-a4](../../examples/store-check-a4)
  marking every field of every item:
  - after marking, every marker read back;
  - SAVE PROJECT AS, then a power cycle straight away: every marker read
    back (before the copy into the stored project, only the project scope
    did);
  - SAVE PROJECT AS, then loading another project: every scope dropped
    (zeros; the project event reported the track, pattern and project
    scopes dropped);
  - loading the project back: every marker read back, nothing dropped;
  - a power cycle after that: every marker read back;
  - CREATE NEW: every field zero, as a new-project event;
  - a power cycle after marking without saving: the project scope, the
    current kit and its 4 sounds read back (the write-through), other kits
    and the pool did not (stock's own unsaved edits to them don't either);
  - trigs placed in grid recording after marking (stock's track save, 4
    calls): the pattern's 6 track fields reached the stored project and came
    back after a power cycle; an unedited pattern's did not;
  - `ev_project` came once at boot (a load; with all four scopes dropped on
    the first boot), once per project load, and as `CORE_PROJECT_NEW` after
    CREATE NEW.
- **The live homes** [emu]: a watch over the live track homes (tracks 1 and
  6), the pattern home, the kit home and a pool sound's bytes through the
  save, the load and a session of playback, song mode, chain, pattern keys
  and encoder edits saw no stock access to them other than core's own and a
  memcpy of a whole pool sound.
- Statically, against the stock image (python, no toolchain): every site's
  stock bytes, that each covers whole instructions, that each body in
  `store.s` jumps back to its site plus the 8 bytes it displaced, and that
  the serializers never name the bytes mod storage uses
  (tests/test_analog_four_mk2.py).

## Building

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/store-check-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.lint examples/store-check-a4/out/store-check-1.0.elemod \
    --stock Analog-Four_MKII_OS1.56.syx --with mods/core-a4/out/core-0.2.elemod
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4/out/core-0.2.elemod --mod examples/store-check-a4/out/store-check-1.0.elemod \
    --out a4-store.syx --version SC56 --lockfile a4-store.lock.json
```
