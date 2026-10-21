# core-ar2

The boot copier, the hook bus and mod storage for the Analog Rytm MKII. From
0.3 it is for OS 1.74 only; 0.2 is the last core with 1.73 (below). It builds the Digitakt mk1's `core.s`, `settings.s` and
`render.s` with the Rytm's addresses, which `mod.json` gives (`defsym`,
`sites`), and from 0.2 its own `store.c` and `store.s` (Mod storage, below),
from 0.3 with `route.s`. On its own it changes nothing the unit does.

The Rytm runs on the Digitakt mk1's platform (a ColdFire MCF5441x), and its OS
entry, bootstrap loader and UI loop are the Digitakt's, so each site below is
the counterpart of one the Digitakt mk1 core hooks, found by matching the two
images and checked by the code around it
(`elekloader/devices/analog_rytm_mk2.py` has the platform evidence).

| event | Rytm 1.73 (to 0.2) | Rytm 1.74 | Digitakt 1.54 | note |
|---|---|---|---|---|
| boot | 0x400008c0 | 0x400008c0 | 0x40000538 | the OS entry's third call, VBR := 0x40000000 |
| `ev_tick` | 0x400a2bee | 0x400a2c5a | 0x4000a770 | about 20 Hz (the Digitakt's 30) |
| `ev_draw` | 0x400a2c56 | 0x400a2cc2 | 0x4000a7d6 | drawAll(ctrl, bmp), 128 x 64 |
| `ev_key` | 0x400a47cc | 0x400a4838 | 0x4000b770 | |
| `ev_enc` | 0x400a4816 | 0x400a4882 | 0x4000b7ba | |
| `ev_settings` | 0x400db87e | 0x400db8ea | 0x40058800 | the SETTINGS menu's builder, 0x400db174 (1.73); rejoins 6 bytes on |
| `ev_render_in` | 0x4011fb02 | 0x4011fb6e | 0x40077428 | the audio interrupt (vector 191), 32 frames a block |
| `ev_render_out` | 0x40121656 | 0x401216c2 | 0x400784c8 | its only exit; the frame is 0xb4 (the Digitakt's 0xa8) |

1.74's main OS is 1.73's with code added for Outbox 8's main-output
routing, so its sites and routines are 1.73's moved (+0x6c, `FN_MGR`
+0x1cc), each found again by its own bytes. A core built for one release
is refused with the other's stock file.

**OS versions.** New core versions drop old OS support: 0.3 builds for
1.74 only (`mod.json`'s `os`, with no `ports`), and is refused with a 1.73
stock file. 0.1 and 0.2 build for 1.73 and 1.74 (1.73 their `os`, 1.74 their
port) and keep their 1.73 support as released; 0.2 is the last core with
1.73. A build takes the newest core it has for the stock OS
([CORES.md](../../docs/CORES.md#how-a-build-picks-one)), so a 1.73 build
takes 0.2, and a mod that needs 0.3 (`"core": "0.3"` in `resources`) is
for 1.74 only. Core 0.2 for 1.73 is built from the tree before 0.3
(commit 92a99c9: `git worktree add ../core-ar2-0.2 92a99c9`, then build its
`mods/core-ar2` with the 1.73 stock file).

The routines `core_additem` calls are the Digitakt's, the same code with the
addresses in it masked: `OP_NEW` 0x40082a40, `ITEM_CTOR` 0x40072fbc,
`MENU_ADD` 0x400727f2 and `FN_MGR` 0x4019411a on 1.74 (the manager for the
same function-pointer type, by its typeinfo's name).

What differs from the Digitakt mk1 at these sites:

- **The tick.** The UI routine passes the view controller from a pointer at
  +0x20 of its object and skips the frame when it is null; the Digitakt passes
  the controller embedded at +0x40. `core_tick` sees the same argument either
  way, and its target (the dirty flag at +0x20) is the Digitakt's byte for byte.
- **The render.** The Rytm's drum voices are analog. Its audio interrupt is
  the digital path: the sample voices, the delay and the reverb. In order:
  `ev_render_in`; the analog voice controls; the PRE and POST effect returns
  mixed from the previous block's delay output (0x80007c44) and reverb buffer
  (0x80007b44); the voices received from the ADC; then 0x4010e650 builds the
  sends and runs the delay (0x4010d9b8) and the reverb (0x4010d3c8); then
  `ev_render_out`. Two other handlers (0x4013cc08, 0x4013e9b8) can take vector
  191, as on the Digitakt, whose core hooks only the main one too.
- **No machines.** The Digitakt's SRC machine slots (`machines.s`) and its
  firmware exports (`fw.s`) are not ported.

## Mod storage (0.2, 0.3)

Mods declare fields in their mod.json (`storage`) and read and write them
with `core_store_get` and `core_store_set`, which mask interrupts while they
do (one byte can hold several mods' fields); elekloader places the fields
from the build's lockfile. docs/ADAPTING.md ("Mod storage (core-ar2 0.2,
Analog Rytm MKII)") is the mod writer's guide; `store.c`'s opening comment
has the byte layout.

Every byte used is one the stock OS never reads or writes, or (from 0.3)
one it only ever holds at 0 and reads, where it matters, through a site
core hooks; no stock function gives up space:

| scope | live, while in use | saved in |
|---|---|---|
| sound | live sound +0x6d..+0x6f; from 0.3 also the 4 velocity routes' amount low bytes, +0x71, +0x74, +0x77, +0x7a (56 bits in all; 24 with 0.2) | stored sound +0x70..+0x72 and (0.3) +0x78..+0x7b, with the layout ID (+0x73) and the sound generation (+0x77) |
| kit | live kit +0x796..+0x79d (FX words 38-41) | stored kit +0x7c6+0x50..+0x57, with the layout ID (+0x58) and the kit generation (+0x5c) |
| global | `core_store_globals` (5 bytes a slot) | stored global slot +0x4b..+0x4f |
| pattern track | live track +785 (64 bytes) | the tagged store |
| pattern | live pattern +85696 (64 bytes) | the tagged store |
| project | the tagged store | the tagged store |

The tagged store is byte +2 of each of the 256 entries of the 17 songs in a
project image (4,352 bytes). The stock OS leaves those bytes out of a
project file, so core hooks the file save and load and the save to a
buffer in RAM, and carries the store across.

**The velocity routes (0.3).** A live sound's four velocity routes (+0x70,
3 bytes each) hold an amount word, then a destination. The amount is the
word's high byte: stock writes the low byte only as 0 (the route load's
byte << 8, the defaults, the menu's whole steps) and saves only the high
byte, so the four low bytes carry the sound scope's bits 24..55. Where stock
reads them in a way that matters (the push to the engine, the VELOCITY MOD
menu's getAmount) they read as 0 again; the menu's setAmount and setRoute
(paste, undo) keep them, and so does its reset (FUNC+PLAY). Both menus'
setRoute store only the amount's high byte, so a route pasted from velocity
into aftertouch keeps the aftertouch low byte at 0, as stock has it. The
aftertouch routes' low bytes are not used: stock holds them at 0 and core
leaves them so, which needs no other aftertouch site. Kept in `route.s`.

**Per-part saves (0.3).** After a front-panel edit and on a pattern change,
stock saves each edited track, a pattern's settings and its parameter locks
into the stored project one part at a time, not through the pattern
serializer, so 0.2 kept a pattern-track or pattern field only when the whole
pattern was saved (SAVE PROJECT, a project file): a field a mod set was lost
at a power cycle. From 0.3 each of those saves also writes the pattern's
track and pattern fields into the store. No stock routine loads one of
those parts by itself: a pattern comes back whole, through the pattern
load. A field a mod sets from its own page reaches the stored project at
the next stock save of that pattern's track, settings or locks, or the next
whole save, as the stock data does.

The 32 sites, by `jmp` unless marked, each with its displaced instructions
in `store.s` or `route.s` (0.3 hooks them on 1.74; the 1.73 column is
0.2's, which has the first 22):

| site | 1.74 | 1.73 (0.2) | why |
|---|---|---|---|
| pattern save / load | 0x4000937c / 0x40009146 | 0x4000937c / 0x40009146 | pattern tracks, patterns, and the project region with the working pattern |
| sound save / load | 0x4000843a / 0x400081f6 | 0x4000843a / 0x400081f6 | the sound's 3 bytes, ID and generation |
| kit save / load | 0x400088d6 / 0x4000863e | 0x400088d6 / 0x4000863e | the kit's 8 bytes, ID and generation |
| global save / load | 0x40007a5c / 0x4000763c | 0x40007a5c / 0x4000763c | the slot's 5 bytes |
| pattern init, track init | 0x40011ab8, 0x400118e8 | 0x40011ab8, 0x400118e8 | clear the fields (stock leaves the spare bytes as they were) |
| sound init | 0x400122b6 | 0x400122b6 | the same |
| project init | 0x40011d38 | 0x40011d38 | CREATE NEW and the project reset: a fresh store |
| project load | 0x4000cd2e | 0x4000cd2e | after any load: drop scopes another layout wrote, write this build's header |
| buffer save (`keep2`) | 0x40034d86 | 0x40034d86 | which image is being saved |
| file save (`keep2`) | 0x400b0870 | 0x400b0804 | the same, for a project file |
| file load (`keep2`) | 0x40035000 | 0x40035000 | which image is being loaded |
| SysEx song copy, write, swap (`keep2`) | 0x4018b8b8, 0x4018c266, 0x4018c488 | 0x4018b6ec, 0x4018c09a, 0x4018c2bc | copy whole stored songs but leave the target's store bytes |
| SysEx pattern copy, write, swap (`keep2`) | 0x4018bc5c, 0x4018c902, 0x4018cb34 | 0x4018ba90, 0x4018c736, 0x4018c968 | clear the target pattern slot's fields, which can't follow the pattern |
| track save (`keep2`, 0.3) | 0x400bf818 | — | an edited track saved by itself (0x40008dc8): the pattern's track and pattern fields too |
| pattern settings save (`keep2`, 0.3) | 0x400aacc4 | — | the same, for the settings (0x40008b06) |
| locks save, one parameter / all (`keep2`, 0.3) | 0x400ac2ce / 0x400ac2ec | — | the same, for the parameter locks (0x4000940c, 0x40009216) |
| velocity push to the engine (0.3) | 0x4011f6a8 | — | the engine gets the amount with its low byte 0 |
| VELOCITY MOD getAmount (0.3) | 0x400c5326 | — | the menu reads the amount with its low byte 0 |
| VELOCITY MOD setAmount (0.3) | 0x400c52b2 | — | an edit keeps the low byte (interrupts masked from its read to the write) |
| setRoute, velocity / aftertouch (0.3) | 0x400c51b4 / 0x4003391e | — | a pasted or undone route stores only the amount's high byte |
| VELOCITY MOD reset (`keep2`, 0.3) | 0x400c542c | — | the defaults (0x4001224e) keep the low bytes (interrupts masked meanwhile) |

After CREATE NEW and after every project load (the project init and
project load sites), core calls the mods' `ev_project` handlers with what
happened and which scopes came in as zeros.

What it does not do:

- **Global SysEx dumps** have no room for a layout ID, so a global slot
  received as SysEx drops its mod data, even from the same build; its
  stock settings load as usual.
- **Freed bits** (a field a build dropped) are not cleared on save; no other
  field is placed there until the scope's generation is bumped, which turns
  the old data to zeros anyway, and the field takes them back, with its
  values, if its mod returns.
- **Pattern tracks** are limited to 16 bits each: three bytes a track would
  not fit in the store.
- **Sounds**: 56 bits, set by the stored sound's spare bytes. The four
  aftertouch routes' low bytes are free in a live sound too, but there is
  no stored byte left to keep them in.
- **voice-dac** (rytm2_mods) hooks the same velocity-route sites itself
  and so does not combine with 0.3; moved onto mod storage, its route bytes
  are sound fields.
- **Projects saved before OS 1.70** are not supported.
- **Patterns copied by the SysEx project commands** between stored patterns
  (in one project or two), swapped, or written into one from a received
  record don't take their pattern-track and pattern fields with them: those
  are kept by pattern slot in the tagged store. Core clears the target
  slot's fields instead, so the pattern loads with its mods' defaults, as a
  pattern received as a SysEx dump does. A target project saved with another
  mod layout keeps its store as it is.

## Checked so far

On a unit (OS 1.73). With test images built by rytm-mods-src (its `elemod`
branch):

- **The boot site**, and the DDR area the linker uses: mod 0016 filled
  0x47BE0000-0x47C00000 from 0x400008c0 at boot, and a readout showed no word
  of it changed after a long, hard session and a power cycle.
- **Both render sites**: mod 0017 armed a flag at `ev_render_in` and cleared
  the effect returns from `ev_render_out` on alternate 0.7 s stretches. The
  delay and the reverb pulsed, each on its own setting, with the reverb return
  PRE or POST.

- **This core**, with [hello-marker-ar2](../../examples/hello-marker-ar2), built
  and patched by elekloader (11 Oct 2026): the unit booted the build, reported
  its version (HM01), and drew the 3x3 square on every screen. So the boot
  copier, the linked image in DDR and `ev_draw` work.

- **Every event**, with [hooks-check-ar2](../../examples/hooks-check-ar2)
  (11 Oct 2026, build HC01): `ev_tick` blinked its square and requested
  redraws, `ev_render_in` advanced its bar steadily and never disagreed with
  `ev_render_out`, `ev_key` and `ev_enc` counted every key and encoder event
  while the unit acted on each as stock does, and `ev_settings` listed the
  HOOK TEST row through `core_additem`, which counted each selection while
  the other rows worked as stock. The same through a short session with a
  pattern playing into the delay and reverb.

On a unit (OS 1.74, 17 Oct 2026), sent with Transfer over USB: the 1.74
builds with hello-marker-ar2 (HM74) and hooks-check-ar2 (HC74) work as the
1.73 ones do.

Statically, against the stock image (python, no toolchain): every site's
stock bytes, and that each covers whole instructions.

- **Mod storage (0.2)**, in an emulator (OS 1.74 and 1.73, 19 Oct 2026), with
  [store-check-ar2](../../examples/store-check-ar2) marking every field of
  every item: after save, loading another project (all zeros), loading the
  project back, and a power cycle, every marker read back. A build with an
  ancestor layout read all of it and rewrote the header; a build with the
  kit generation bumped read zeros for kits only; a build with a fresh
  layout read zeros everywhere; CREATE NEW cleared everything. A global
  slot received as SysEx came in with its mod data cleared, and the other
  three slots and every other scope kept theirs. `ev_project` came once
  at boot (a load; on the first boot with all four scopes dropped, after a
  power cycle with none), once per project load, not on save, and as
  `CORE_PROJECT_NEW` after CREATE NEW. On 1.73 the same chain, CREATE
  NEW, the SysEx global and the events gave the same results; the ancestor,
  kit generation and other-layout builds ran on 1.74 only.

- **Mod storage (0.2) on a unit** (19 Oct 2026), sent with Transfer over
  USB, with store-check (builds SC74, OTHER74 and SC73: two layouts, both
  OS versions). On 1.74: every column stayed empty after marking, saving,
  loading the project back and a power cycle, and was full after loading a
  stock-saved project (with all four drop flags); the project exported as
  .arprj and imported into another slot kept everything, as did a Total
  Recall sync and recall; CREATE NEW cleared everything, as a new-project
  event. The same mods built with another lockfile read none of the saved
  project (all four drop flags), and the first build read all of it again
  afterwards. On 1.73 the project saved on 1.74 read in full, and marking,
  saving, loading another project, loading back and a power cycle passed.
  One flash (OTHER74) stopped at SUCCESS: REBOOTING and booted normally
  after a power cycle.

- **Mod storage (0.3)**, in an emulator (OS 1.74, 20 Oct 2026; before 0.3
  dropped 1.73, the same checks also passed with a 1.73 build of it):
  with 0.2, a track and a pattern field put in a live pattern, then a trig
  placed on that track (stock's track save), read 0 after a power cycle;
  with 0.3 they read back, and the stock track record was the same as with
  0.2. A parameter lock alone (the locks save) and a SCALE menu edit alone
  (the settings save) each carried them too. store-check with a 24-bit and
  a 32-bit sound field: marking, SAVE PROJECT AS, loading it back and a
  power cycle kept every marker. With velocity amounts' low bytes set, the
  VELOCITY MOD menu (open, edit all four amounts, negative ones too) drew
  every frame as stock did from the same amounts, and the edited amounts
  and the engine's copies were stock's, with the low bytes kept in the
  sound and 0 in the engine (with 0.2 they reached the engine, and the menu
  drew negative amounts one step nearer 0); copy, clear (FUNC+PLAY) and
  paste kept them; SAVE KIT and RELOAD brought them back. A 0.2 build's
  lockfile built with 0.3 read the 0.2 project's sound fields in place
  (checked on 1.73 only, with that 1.73 build).

## Building

Core 0.3 builds with `Analog-Rytm_MKII_OS1.74.syx` only: the SDK writes
`core-0.3.elemod` (1.74 is its `os`) and, for hello-marker-ar2 (whose `os`
is 1.73, with a 1.74 port), `hello-marker-1.0-os1.74.elemod`. For 1.73,
build core 0.2 from commit 92a99c9 (OS versions, above).

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/hello-marker-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.lint examples/hello-marker-ar2/out/hello-marker-1.0-os1.74.elemod \
    --stock Analog-Rytm_MKII_OS1.74.syx --with mods/core-ar2/out/core-0.3.elemod
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.3.elemod --mod examples/hello-marker-ar2/out/hello-marker-1.0-os1.74.elemod \
    --out rytm-hello.syx --version HM74
```
