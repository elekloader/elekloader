# core-ar2

The boot copier, the hook bus and mod storage for the Analog Rytm MKII, OS
1.73 and 1.74. It builds the Digitakt mk1's `core.s`, `settings.s` and
`render.s` with the Rytm's addresses, which `mod.json` gives (`defsym`,
`sites`), and from 0.2 its own `store.c` and `store.s` (Mod storage, below).
On its own it changes nothing the unit does.

The Rytm runs on the Digitakt mk1's platform (a ColdFire MCF5441x), and its OS
entry, bootstrap loader and UI loop are the Digitakt's, so each site below is
the counterpart of one the Digitakt mk1 core hooks, found by matching the two
images and checked by the code around it
(`elekloader/devices/analog_rytm_mk2.py` has the platform evidence).

| event | Rytm 1.73 | Rytm 1.74 | Digitakt 1.54 | note |
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
+0x1cc), each found again by its own bytes; `mod.json`'s `ports` gives
them. A core built for one release is refused with the other's stock file.

The routines `core_additem` calls are the Digitakt's, the same code with the
addresses in it masked: `OP_NEW` 0x400829d4, `ITEM_CTOR` 0x40072f50,
`MENU_ADD` 0x40072786 and `FN_MGR` 0x40193f4e (the manager for the same
function-pointer type, by its typeinfo's name).

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

## Mod storage (0.2)

Mods declare fields in their mod.json (`storage`) and read and write them
with `core_store_get` and `core_store_set`, which mask interrupts while they
do (one byte can hold several mods' fields); elekloader places the fields
from the build's lockfile. docs/ADAPTING.md ("Mod storage (core-ar2 0.2,
Analog Rytm MKII)") is the mod writer's guide; `store.c`'s opening comment
has the byte layout.

Every byte used is one the stock OS never reads or writes; no stock
function gives up space:

| scope | live, while in use | saved in |
|---|---|---|
| sound | live sound +0x6d..+0x6f | stored sound +0x70..+0x72, with the layout ID (+0x73) and the sound generation (+0x77) |
| kit | live kit +0x796..+0x79d (FX words 38-41) | stored kit +0x7c6+0x50..+0x57, with the layout ID (+0x58) and the kit generation (+0x5c) |
| global | `core_store_globals` (5 bytes a slot) | stored global slot +0x4b..+0x4f |
| pattern track | live track +785 (64 bytes) | the tagged store |
| pattern | live pattern +85696 (64 bytes) | the tagged store |
| project | the tagged store | the tagged store |

The tagged store is byte +2 of each of the 256 entries of the 17 songs in a
project image (4,352 bytes). The stock OS leaves those bytes out of a
project file, so core hooks the file save and load and the save to a
buffer in RAM, and carries the store across.

The 22 sites, by `jmp` unless marked, each with its displaced instructions
in `store.s`:

| site | 1.73 | 1.74 | why |
|---|---|---|---|
| pattern save / load | 0x4000937c / 0x40009146 | the same | pattern tracks, patterns, and the project region with the working pattern |
| sound save / load | 0x4000843a / 0x400081f6 | the same | the sound's 3 bytes, ID and generation |
| kit save / load | 0x400088d6 / 0x4000863e | the same | the kit's 8 bytes, ID and generation |
| global save / load | 0x40007a5c / 0x4000763c | the same | the slot's 5 bytes |
| pattern init, track init | 0x40011ab8, 0x400118e8 | the same | clear the fields (stock leaves the spare bytes as they were) |
| sound init | 0x400122b6 | the same | the same |
| project init | 0x40011d38 | the same | CREATE NEW and the project reset: a fresh store |
| project load | 0x4000cd2e | the same | after any load: drop scopes another layout wrote, write this build's header |
| buffer save (`keep2`) | 0x40034d86 | the same | which image is being saved |
| file save (`keep2`) | 0x400b0804 | 0x400b0870 | the same, for a project file |
| file load (`keep2`) | 0x40035000 | the same | which image is being loaded |
| SysEx song copy, write, swap (`keep2`) | 0x4018b6ec, 0x4018c09a, 0x4018c2bc | 0x4018b8b8, 0x4018c266, 0x4018c488 | copy whole stored songs but leave the target's store bytes |
| SysEx pattern copy, write, swap (`keep2`) | 0x4018ba90, 0x4018c736, 0x4018c968 | 0x4018bc5c, 0x4018c902, 0x4018cb34 | clear the target pattern slot's fields, which can't follow the pattern |

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

## Building

For 1.74, give `Analog-Rytm_MKII_OS1.74.syx` instead: the SDK then writes
`core-0.2-os1.74.elemod` and `hello-marker-1.0-os1.74.elemod`.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.73.syx
python -m elekloader.sdk.build examples/hello-marker-ar2 --stock Analog-Rytm_MKII_OS1.73.syx
python -m elekloader.lint examples/hello-marker-ar2/out/hello-marker-1.0.elemod \
    --stock Analog-Rytm_MKII_OS1.73.syx --with mods/core-ar2/out/core-0.2.elemod
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.73.syx \
    --mod mods/core-ar2/out/core-0.2.elemod --mod examples/hello-marker-ar2/out/hello-marker-1.0.elemod \
    --out rytm-hello.syx --version HM01
```
