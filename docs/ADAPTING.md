# How to adapt your mod to elekloader

This guide is for people and for coding agents. Every step has a command
and the output that means it worked; the last sections map every refusal to
its fix and give a definition of done. The format itself is
[FORMAT.md](FORMAT.md); the Digitakt mk1 addresses below are OS 1.53's, the
Digitone mk1's 1.43's. Building for another OS version (1.54, 1.44) is 3.10.

## 0. Pick a path

| you have | do | result |
|---|---|---|
| code that should run next to other mods | **Path B**: a linkable mod (format 2), built with the SDK | combines with any other mod that does not conflict |
| a byte or two to change (a constant, a limit) | **Path B** with `bytes` sites and no sources | the same; no compiler needed |
| a custom firmware you already build as one piece (one `.syx`) | **Path A**: a whole-build mod (format 1), made with `mkmod` | quick, but it cannot be combined with anything |
| an octabam module (Octatrack) | `elekloader.sdk.octabam` converts it to Path B (section 4b) | a linkable mod, checked against octabam's own account of its bytes |

Path B is the one to aim for. Path A is a stopgap for an existing
monolithic build; section 4 says how to go from A to B.

## 1. What you need

- Python 3.9 or newer, and this repository (no dependencies).
- The stock OS file for the device and release you target, exactly as the
  manufacturer publishes it. elekloader knows it by hash: the README lists
  the supported releases, [INSTALL.md](INSTALL.md#the-stock-os-file) where
  to get each file, and `devices/` holds their profiles.
- For code (Path B with sources): the device's cross toolchain, from its
  profile's `toolchain`. For the Digitakt mk1 that is m68k binutils and
  gcc: on Debian or Ubuntu, `apt install binutils-m68k-linux-gnu
  gcc-m68k-linux-gnu`; on Windows, inside WSL; on a Mac, with Homebrew,
  `brew install m68k-elf-binutils m68k-elf-gcc`, then
  `export ELEKLOADER_CROSS=m68k-elf-`. Set `ELEKLOADER_CROSS` whenever
  your tools have another prefix.
- For linkable mods: the **core** mod for your device (`core-*.elemod`).
  It owns the shared hook sites, copies every mod's code into RAM at boot,
  and turns the shared sites into events your mod subscribes to. Every
  format-2 mod set needs exactly one core. `mods/core/core.s` is its one
  source (with `settings.s`, `render.s` and `fast.s` where the device
  uses them); `mods/core` builds it for the Digitakt mk1
  (`python -m elekloader.sdk.build mods/core --stock Digitakt_OS1.53.syx`),
  `mods/core-dn1` for the Digitone mk1, `mods/core-dt2` for the
  Digitakt II, `mods/core-dn2` for the Digitone II (experimental) and
  `mods/core-ar2` for the Analog Rytm MKII. The
  Octatrack's core, `mods/core-ot`, has a hook bus of its own from 0.2, with the Octatrack's events ("The hook bus on the
  Octatrack" below).
- For a SHARC mod on the Digitone II: its compiled code, or the tools the
  sine example uses to make it ("SHARC mods (core-dn2 0.2, Digitone II)"
  below).

## 2. The rules

The loader checks all of these before it builds anything; each rule says
what you see when it is broken (section 5 has the fixes).

1. **Only the main OS changes**, and on the Digitone II 1.12 the DSP's
   section 7 for a SHARC mod ("SHARC mods" below). The bootloader and the
   other sections stay stock, so the stock OS file always recovers the
   device.
   Some main OS bytes stay stock too: the Octatrack's copy of its
   bootloader, and 16 zero bytes after a table that its stock DELAY reads
   (docs/DEVICES.md, `protected` and `keep`). *"protected on the ..."*,
   *"stays stock on the ..."*.
2. **Ship no firmware bytes.** Call firmware routines by address; do not
   copy stock code into your sources. When your bytes do repeat stock bytes
   (the instruction a hook displaced, say), runs of 8 bytes or more are
   stored as references and filled in from the user's own file.
3. **Patch whole instructions.** A code site covers whole stock
   instructions, and so do its new bytes: typically a 6-byte `jsr`/`jmp`
   over one or more instructions, and the routine it calls does their work
   itself. *"ends mid-instruction"*, *"sweeps land on the start"*.
4. **Do not patch the shared sites: subscribe.** On the Digitakt mk1 the
   core mod owns the sites in the table below. Subscribe to their events
   instead of hooking them. *"... overlap"*.
5. **One owner per byte.** No two mods may patch overlapping bytes, or bytes
   another mod's table entries claim. *"... overlap"*.
6. **Let the linker place you.** Your code and data go in sections; never
   hard-code the address of your own code or data. Budgets are shared by
   every mod: on the Digitakt mk1, 128 KB of RAM (`.run` + `.bss`), or
   256 KB with core 4.0, and 2304 bytes of fast SRAM (`.fast`); on the
   Digitone mk1 128 KB, or 1.25 MB with core-dn1 4.0 ("The mods' RAM"
   below); on the Analog Rytm MKII 128 KB, and no fast SRAM, so a mod with
   `.fast` code is refused there (*"has .fast code, which needs the mod that
   declares a .fast copy table"*). *"the mods need RAM
   to ..."*.
7. **Only relocatable references.** The linker relocates 32-bit absolute and
   32/16-bit PC-relative references. Use the device's compiler flags (the
   SDK does) and 32-bit addressing for your own symbols. Firmware addresses
   are plain numbers and need no relocation. *"relocation type N"*.
8. **One namespace.** Every global symbol is visible to every mod: prefix
   yours with your mod's id (`hello_draw`), and make internals `static`.
   *"both export ..."*.
9. **Claim what you use.** List named resources in `resources.names`: SysEx
   device ids (`sysex:0x7d`), SETTINGS rows (`settings:MY ROW`), +Drive
   paths (`drive:/cfw/mine.bin`), key combinations (`ui:SRC page:hold YES`).
   Memory you use outside the linker's areas goes in `resources.regions`.
   *"both claim ..."*.
10. **Inside another mod's copied block, be absolute.** FAST AUDIO copies the
    render block 0x400716c0-0x4007629a to SRAM at run time. A site of yours
    in that block runs from the copy too, so it may not be PC-relative and
    may not touch the bytes the copy's fix-ups rewrite. *"PC-relative,
    inside ... copied block"*.
11. **Say what you need.** `requires` names the mods yours depends on (always
    `core` for format 2); `conflicts` names the ones it cannot live with.
    *"X requires Y"*.
12. **One file, one release.** A mod targets one stock release by hash.
    Porting to another OS version means finding every address again.

### The hook bus (core's events): the Digitakt mk1, the Digitone mk1, the Digitakt II, the Digitone II, the Analog Rytm MKII and the Analog Four MKII

Handlers are C functions (arguments on the stack; d0-d1/a0-a1 free,
everything else kept; the result in d0). `order` sorts handlers of one
event (lower first; the shipped mods use 10-90).

| event | when | C prototype | notes |
|---|---|---|---|
| `ev_tick` | 30 times a second, UI task | `void f(void *ctrl)` | set `*((unsigned char *)ctrl + 0x20) = 1` to redraw |
| `ev_draw` | after each frame is drawn | `void f(void *bmp, void *ctrl)` | draw over the frame; y = 0 is the bottom row |
| `ev_key` | each key event | `int f(void *brain, void *ev)` | return nonzero to take it: nothing later sees it. `ev+12` key id, `ev+16` flags (1 pressed, 8 repeat, 2 FUNC held; measured on the Digitone mk1: 8 comes once, about 0.4 s into a key held on its own, 4 marks a double press, 0x10 a release) |
| `ev_enc` | each encoder turn | `int f(void *brain, void *ev)` | `ev+12` encoder 1-8 (A-H), `ev+16` delta |
| `ev_settings` | the SETTINGS menu is built | `void f(void *menu)` | add a row with `core_additem(menu, row)`; `row` is four callbacks (label, select, draw, change) in the firmware's MenuItem conventions, which this guide does not document yet. On the Digitone, from core-dn1 4.1.1, the rows take none of the heap (mods/core-dn1/rows.s): with 4.1 and before, two rows could stall a first boot |
| `ev_render_in` | audio render entry, 1500 times a second | `void f(void)` | interrupt level: keep it short |
| `ev_render_out` | audio render exit | `void f(void)` | the same |
| `ev_hold` | Digitone mk1 only (core-dn1 2.2): a track key held on its own, UI task | `int f(void *brain, void *event, int track)` | return nonzero to take it; when none does, core opens the Mod Menu ("The Mod Menu" below) |
| `ev_personalize` | Digitakt II (core-dt2) and Digitone II (core-dn2 0.1): SETTINGS > PERSONALIZE is built | `void f(void *menu)` | add a row with `core_additem(menu, row)`, after the last row (TRK SELECT on the Digitakt II, NOTE EDIT MENU on the Digitone II); a row redraws the menu with `View::invalidate(menu + 0x38)`, as from SETTINGS; a checkbox is drawn as mods/core-dt2/README.md describes |
| `ev_voice_on` | Digitone mk1 only (core-dn1 2.1): a voice starts a note, in the render | `void f(int voice, int track, void *event)` | interrupt level. The voice's pitch word (`0x41391f80` + 4 x voice, the note << 16) is already written and may be changed: the render reads it every block. The voice's sound and the step's locks load after this, so read the voice's parameters from `ev_render_out` |
| `ev_midi_cc` | Digitone mk1 only (core-dn1 3.2): each MIDI CC the unit receives on a track's channel or the auto channel, before the stock applies it; the MIDI task | `int f(int track, int cc, int value, int flags)` | `track` 0-8 (with MIDI CONFIG's default channels, channels 1-9); a CC on the auto channel comes as the active track, with `flags` bit 0 (`CORE_MIDI_CC_AUTO`) set. Return nonzero to take it: the stock does not apply it. Only what MIDI CONFIG lets in arrives (PORT CONFIG > INPUT FROM). A mod that patches the CC router's entry, 0x400ed94e (Tone+FX), sees a CC before core |
| `ev_midi_pc` | Digitone mk1 only (core-dn1 4.0): each program change the unit receives, on any channel, before the stock applies it; the MIDI task | `int f(int channel, int program, int flags)` | `channel` 0-15, `program` 0-127; `flags` bit 0 (`CORE_MIDI_CC_AUTO`): it came on the auto channel; bit 1 (`CORE_MIDI_PC_STOCK`): the stock would apply it (MIDI CONFIG receives program changes, and it came on PROG CH IN's channel). Return nonzero to take it: the stock does not change the pattern. Only what MIDI CONFIG lets in arrives (PORT CONFIG > INPUT FROM) |
| `ev_key_leds` | Digitone mk1 only (core-dn1 3.3): the key-LED frame, every UI frame, after the frame clears the LEDs and before the stock views set theirs; UI task | `void f(void *mgr)` | `mgr` the LedManager. The first setter of an LED wins, so what a handler sets stays over the stock view's: a mod shows its state on the keys while the stock page keeps the rest |
| `ev_render_params` | Digitone mk1 only (core-dn1 3.3): each block's sound parameter words, before the controller stage and the LFOs, in the render | `void f(int16_t *words)` | interrupt level, ~1500 times a second. Voice v's slot k at `words[79 * v + k]` (8.8, `CORE_PARAMS_VOICE`), the global FX at record 8; the smoother writes them again every block, so a change lasts one block and the kit is untouched. The words of a block are rendered in the next two; `fw_voice_track[v]` is voice v's track ("Each block's sound parameters" below) |

The events, their prototypes and their conventions are the same on every
device; only the sites differ, and `ev_voice_on`, `ev_midi_cc`, `ev_key_leds`,
`ev_render_params` and `ev_midi_pc` exist on the Digitone only. The Digitakt II's core (mods/core-dt2 1.0) has `ev_tick`, `ev_draw`,
`ev_key`, `ev_enc`, `ev_settings` and its own `ev_personalize`, and no
render events: its audio
renders on the DSP. It copies `.fast` code into SRAM itself, on the first
`ev_tick` (`core_fast`): subscribe only `.run` code to `ev_key` and
`ev_enc` there, since a key can come before that tick (`ev_draw` cannot:
the tick site runs first).

The Digitone II's core (mods/core-dn2, from 0.1) has the same six UI events as the
Digitakt II's, at the Digitone II's sites, and no render events (its audio
renders on the DSP). It has no `.fast`: SRAM is not qualified there, so a
mod with `.fast` code is refused. Its tick rate has not been measured.

### Remote panel input and SysEx (core-dn2 0.1, Digitone II)

Include `digitone-mk2/core.h` and require `resources.core: "0.1"`.
On the UI task, `core_keys_set(group, synthetic_mask)` merges a group's
synthetic buttons with the physical panel; `core_keys_clear()` releases
only synthetic ownership. The result is `CORE_KEYS_OK`, `CORE_KEYS_INVALID`,
`CORE_KEYS_BUSY` or `CORE_KEYS_OVERLAP`. Refuse overlap, retry a busy release on a later tick, and
bound every hold using unsigned `core_millis()` differences. Its resolution
is 8/9 ms from the stock periodic callback; it leaves DTIM0 stopped.
`core_encoder_turn(encoder, detents)` accepts encoder 1..9 and nonzero
-30..30 detents, subject to queue bounds. Driver acceptance does not prove
that a view handled the input: read back the resulting state.

`ev_sysex` is `void f(const unsigned char *message, unsigned length,
unsigned source)` on the MIDI task, before stock routing. Every observer
must validate its header, lengths and source (2/4 USB, 8/16 DIN). It cannot
take a message: stock routing continues. `fw_sysex_send(data, length,
abort, port)` is the stock sender; port 2 selects USB. Keep UI actions out
of the MIDI task; queue bounded work for `ev_tick`. The DigiHealth DN2
port is an example of using all of this without owning stock sites.

The Analog Rytm MKII's core (mods/core-ar2) has `ev_tick`, `ev_draw`,
`ev_key`, `ev_enc`, `ev_settings`, `ev_render_in` and `ev_render_out`, and
from 0.2 mod storage and the event `ev_project` ("Mod storage (core-ar2 0.2,
Analog Rytm MKII)"). What differs there:
- `ev_tick` comes about 20 times a second, not 30.
- The render events cover the audio interrupt's digital path: the sample
  voices, the delay and the reverb. The drum voices are analog and are not
  in it.
- From 0.4, `core_audio_load` (a `uint32_t`, `analog-rytm-mk2/load.h`) is
  the last audio block's time as a share of the block, in 256ths, timed by
  core from its own `ev_render_in` (order 0) to its `ev_render_out` (order
  100). Stock alone peaks near 84% while a pattern plays. Work a mod can
  skip without changing the sound (an estimate, a meter) should stand aside
  while it is high, so the audio and the UI keep their time; never thin the
  sound itself for it. comp-preview stands aside above 95% and comes back
  below 88%.
- A SETTINGS row is listed after SYSTEM.
- The key and encoder events are built as the Digitakt mk1's are, by
  constructors with the same fields, so the table's `ev` offsets hold
  (`ev+12` the key or encoder, `ev+16` the flags or the delta; read in the
  1.74 image). The key ids are the Rytm's own.
- core-ar2 exports no firmware locations (`fw_*`): a mod names each
  firmware address itself, for each OS (3.10).

The Analog Four MKII's core (mods/core-a4 0.1, OS 1.56) is core-ar2 0.3 at
the A4's sites: the same events, `ev_project` and mod storage ("Mod storage
(core-a4 0.1, Analog Four MKII)"), with the same differences from the
Digitakt mk1. On the A4, the SETTINGS menu is the GLOBAL key's, and its rows
come after SYSTEM; the render events cover the audio interrupt's digital
path (the effects and the inputs), since the voices are analog. core-a4
exports no firmware locations either. From 0.2 it has core-ar2 0.4's
parameter routines and `core_audio_load` ("Parameter pages (core-a4 0.2,
Analog Four MKII)"), and from 0.3 core-ar2 0.6's `core_page_fns`. `examples/hello-marker-a4`, `hooks-check-a4`,
`store-check-a4`, `ddr-canary-a4` and `pages-check-a4` are mods for it.

The sites core owns (do not patch them):
- Digitakt mk1 1.53 and 1.54: 0x40000538, 0x4000a770, 0x4000a7d6,
  0x4000b770, 0x4000b7ba, 0x40058800, 0x40077428, 0x400784c8;
- Digitone mk1 1.43: 0x40000538, 0x4001900c, 0x40019072, 0x40019d9c,
  0x40019de4, 0x40072a34, 0x4009d108, 0x4009e51c, from core-dn1 2.1
  0x4009e928, from 3.1 0x4009e078 and 0x400207b8, and from 3.2
  0x4009e07e, 0x4009e146 and 0x400ed96e, from 3.3 0x40018332 and
  0x4009db80, and from 4.0 0x400ea1a8;
- Digitone mk1 1.44: the same, but 0x40072a54, 0x4009d128 and 0x4009e53c
  for the last three of the eight, 0x4009e948, 0x4009e098, 0x400207b8,
  0x4009e09e, 0x4009e166, 0x400edbe2, 0x40018332, 0x4009dba0 and
  0x400ea41c;
- Digitakt II 1.17: 0x40000538, 0x40032ad4, 0x40032b3a, 0x40033d94,
  0x40033dde, 0x400a5eac, 0x4009e2a2;
- Digitone II 1.12: 0x40000538, 0x4002e464, 0x4002e4ca, 0x4002f20c,
  0x4002f256, 0x4009ff88, 0x400987a2, 0x4011fc8c, 0x4011f8c4,
  0x4012197a;
- Analog Rytm MKII 1.73: 0x400008c0, 0x400a2bee, 0x400a2c56, 0x400a47cc,
  0x400a4816, 0x400db87e, 0x4011fb02, 0x40121656;
- Analog Rytm MKII 1.74: 0x400008c0, 0x400a2c5a, 0x400a2cc2, 0x400a4838,
  0x400a4882, 0x400db8ea, 0x4011fb6e, 0x401216c2;
- from core-ar2 0.2, on both Rytm releases, 22 more for mod storage, and
  from 0.3, which is for 1.74 only (0.2 is the last core with 1.73), 10
  more (32 in all; listed under "Mod storage");
- Analog Four MKII 1.56: 0x40001180, 0x40098898, 0x40098900, 0x400999f8,
  0x40099a40, 0x400d05d2, 0x401013c6, 0x401040f6, and 28 more for mod
  storage (mods/core-a4/README.md lists them); from core-a4 0.2, 0x400f6212,
  0x40023d54, 0x400232e2 and 0x4009ea68 (the page routines), 0x400c2370
  and 0x400c4202 (the views), and the bytes at 0x400add22, 0x400add28,
  0x4000a758, 0x4000a774 and 0x4000a790 (the current view's range).

A firmware routine your mod calls has its own address on each device and
each OS version: look it up for the release you target, and build one
`.elemod` per device and OS (a mod's `device` and `os`, or one of its
`ports`, pick the stock file: 3.10).

### SHARC mods (core-dn2 0.2, Digitone II)

Experimental. On the Digitone II 1.12 a mod can run code on the DSP, at the
master output. Core-dn2 0.2's bridge calls it once a block with 32 mono
samples at 48 kHz, which it may replace or change, and then copies them to
both channels. A build has at most one such mod. It is not a machine you
pick for a sound, an effect on a track or a polyphonic voice. The
[callback contract](SHARC-DN2-ABI.md) says what its code must do: the
registers it keeps, the NOP guard, the pitch and the gate, and its stack (at
most 428 bytes declared; more code or data adds none).

**Start from the [sine example](../examples/sharc-sine/README.md).** After a
one-time setup of the tools and your stock file, `mise run build-c` or
`mise run build-asm`, in `examples/sharc-sine`, compiles the current source,
makes the artifact, builds core and the mod, lints them and writes verified
firmware. Both use the 32 KiB layout, so you choose no addresses, and
neither uses the maintainers' reference scripts or the checked-in artifact.
Set `"resources": {"core": "0.2"}` in your mod.json, as the examples do, so
the loader picks a core with the bridge.

**Any compiler will do.** elekloader takes the compiled code as an artifact
([FORMAT.md](FORMAT.md#digitone-ii-sharc-mod)); its SDK only packages it.
Link the code for the mod's area, loader `[0x20090000,0x20098000)`, and give
offsets from its start: changing the `abi` string does not move code linked
for another address. Your code, data and callback offsets may change from
build to build with nothing added to elekloader; only core's bridge is
pinned. With an artifact in hand you need no SHARC compiler:

```sh
python -m elekloader.sdk.build examples/sharc-sine --stock "$DN2_STOCK"
python -m elekloader.lint examples/sharc-sine/out/sharc-sine-0.1.elemod --stock "$DN2_STOCK" --with "$DN2_CORE" --json
python -m elekloader.patch --stock "$DN2_STOCK" --mod "$DN2_CORE" --mod examples/sharc-sine/out/sharc-sine-0.1.elemod --out sine.syx --version SK02
```

The build prints `BUILT`, lint reports `"ok": true`, and patch prints
`verified: sections 2, 4, 5, 8, byte for byte`. These package the
checked-in artifact; they do not compile an edited source. As for every
format-2 mod, you need a matching core `.elemod`, and building it from
source needs the ColdFire tools. The
[assembly example](../examples/sharc-sine-asm/README.md) shares the core, the
note range and the gain ramp; pick one of the two per build. The
`rebuild.py` scripts rebuild the reference artifacts exactly, for
elekloader's own development.

The checks cover the packaging: the guard, the 32 KiB covered in full, the
stack and the firmware's size limits. They cannot prove what the
instructions do, what they write, or that they meet the audio deadline.
[SHARC-DN2.md](SHARC-DN2.md) has what the emulator and a unit showed for
the sine variants, and the limits that remain.

### The hook bus on the Octatrack (core-ot 0.2)

The Octatrack's firmware is not the Digitakt's, so its events carry what
its own routines have. The conventions are the same: C handlers, `order`,
and `subscribe` in mod.json. A mod that subscribes to one of these is
refused with core-ot 0.1 (*"adds to table ev_tick, which no given mod
declares"*).

| event | when | C prototype | notes |
|---|---|---|---|
| `ev_tick` | 60 times a second, sys task | `void f(void)` | the task `ev_key`, `ev_enc` and (nearly always) `ev_draw` run in, so their handlers never interrupt one another |
| `ev_draw` | each composed frame, before the bytes that changed go to the LCD | `void f(unsigned char *frame)` | 1024 bytes: 128 rows of 8 bytes, one per screen column x (0 at the left); line y (0 at the top) is bit 63 - y of the row, counting from its first byte's most significant bit. The frame is cleared and composed again each time: what you draw stays one frame. `examples/hello-marker-ot` |
| `ev_key` | each key press and release | `int f(int code, int pressed)` | the firmware's key code (PLAY 0x28, YES 0x31, NO 0x32, UP 0x33: octabam's docs/firmware/PANEL.md); `pressed` nonzero on a press, 0 on a release. Return nonzero to take it: the firmware never sees it. Auto-repeat does not come here |
| `ev_enc` | each encoder turn | `int f(int encoder, int delta)` | the firmware's encoder index, 0-7; return nonzero to take it |
| `ev_midi` | each MIDI message in, MIDI thread | `int f(const unsigned char *msg)` | `msg[0]` the status, then its data bytes; return nonzero to take it: the firmware's handler for it is not called |
| `ev_frame` | the frame interrupt, every 16 samples (2,756 a second) | `void f(void)` | interrupt level: keep it short. It runs before the interrupted task's EMAC state is put back, so it may use the EMAC |
| `ev_frame_end` (0.4) | the frame interrupt's last point, every 16 samples | `void f(void)` | after `ev_frame`, TUNER's counter store and USB audio's tail. The interrupt's EMAC restore runs again after the handlers, so it may use the EMAC too |
| `ev_midi_out` (0.4) | each MIDI message the firmware sends (midi_send) | `void f(const unsigned char *msg, unsigned len)` | before stock sends it; it runs where the sender runs (a task or an interrupt): keep it short |
| `ev_midi_out_rt` (0.4) | each realtime byte the firmware sends (the priority sender) | `void f(unsigned byte)` | the same |
| `ev_frame_begin` (0.5) | the frame interrupt's first point, every 16 samples | `void f(void)` | before the interrupt saves the interrupted task's EMAC state: a handler must not touch the EMAC. Core's frame meter stamps first, so a handler's time counts in the meter |
| `ev_transport` (0.5) | the sequencer starts, resumes or stops | `void f(unsigned what, unsigned playing)` | `what` is `OT_TRANSPORT_START` (0), `_RESUME` (1) or `_STOP` (2); `playing` the sequencer's word as stored (1, 1, 2). It runs in the task that called the transport routine (the sys task for the panel's keys; MIDI start, continue and stop come in another), right after the store: do not call into the sequencer or send MIDI from it |
| `ev_boot_done` (0.5) | once, when main has made its last init call | `void f(void)` | the OS's tasks exist, no project is loaded yet. In the main task |
| `ev_idle` (0.5) | main's idle loop, over and over | `void f(void)` | the lowest priority: runs when nothing else does. It must not wait for anything: if main blocks, nothing is left to run. A build with an `ev_idle` handler runs main's idle loop in the core; without one main parks in the stock `bras .` |
| `ev_tick120` (0.5) | the key-repeat task, 120 times a second | `void f(void)` | after the key-repeat step; that task has 2 KB of stack. Core's paced MIDI drains first |
| `ev_project` (0.6) | a whole project was loaded (LOAD PROJECT, the power-up's load, RELOAD) or made (CREATE EMPTY PROJECT), when the firmware's job has ended | `void f(int how, unsigned dropped)` | as core-ar2's and core-a4's: `how` is `CORE_PROJECT_NEW` (0) or `CORE_PROJECT_LOADED` (1); `dropped` has `OT_DROPPED_PROJECT` when the project's blocks hold zeros, `OT_DROPPED_CARD` (the first event only) when the card's do. In the engine task, which may use the file calls. One load can bring it more than once |
| `ev_banks_load` (0.6) | a load of banks | `void f(unsigned pre, unsigned mask)` | 1 before, 0 after; `mask` bit n is bank n. Engine task |
| `ev_banks_write` (0.6) | the bank writer (background saves, SAVE PROJECT, SAVE AS, a new project) | `void f(unsigned pre, unsigned mask)` | 1 before the first bank file, after the project folder is made; 0 after. Engine task |
| `ev_project_store` (0.6) | SAVE PROJECT's copy of each `.work` file to `.strd` | `void f(unsigned pre, unsigned unused)` | 1 before, 0 after. Engine task |
| `ev_project_reload` (0.6) | RELOAD's copy of each `.strd` back to `.work` | `void f(unsigned pre, unsigned unused)` | 1 before, 0 after; `ev_project` follows. Engine task |
| `ev_cs1_bank` (0.6) | the current bank's working copy into battery RAM, or back from it at power-up | `void f(unsigned to, unsigned bank)` | `to` 1: before stock copies it in; 0: after it came back |
| `ev_part_saved`, `ev_part_cleared` (0.6) | Part Save, and Part Clear | `void f(unsigned part, unsigned unused)` | the current bank. Part Clear ends in Part Save, so a clear brings both |
| `ev_voice_start` (0.7) | a DSP command was posted for a track this frame (not 0) | `unsigned f(unsigned trk, unsigned cmd)` | in the frame interrupt. Returns the command the next handler, the packer and the DSP get |
| `ev_voice_release` (0.7) | the AMP release of a track | `void f(unsigned trk, unsigned word)` | frame interrupt; the mailbox word, bit 6 set |
| `ev_voice_kill` (0.7) | a voice was reset (STOP, a pattern change, the loaders, the preview) | `void f(unsigned trk)` | every interrupt masked, from a task or the frame interrupt |
| `ev_voice_bind` (0.7) | the binder is about to write a voice's arena pointers | `int f(unsigned trk, void *voice)` | frame interrupt; nonzero leaves them (the voice stays unbound and is reset once or twice a frame) |
| `ev_voice_seek` (0.7) | the binder is about to count a (re)start of a voice | `int f(unsigned trk, void *voice, unsigned same)` | frame interrupt; nonzero leaves the counter, so the DSP seeks the voice |
| `ev_trig_apply` (0.7) | a trig's locks reached the track's lane | `void f(unsigned trk, unsigned flags)` | frame interrupt |
| `ev_dsp_trig` (0.7) | a track's per-trig flag bits are about to be built into its DSP word | `int f(unsigned trk, unsigned flags)` | frame interrupt; nonzero builds the word as for a frame with no trig |
| `ev_dsp_record` (0.7) | a track's 64-byte record for the DSP has its FX ids | `void f(unsigned trk, uint16_t *rec)` | frame interrupt, 8 times a frame; a store here lands last |
| `ev_clip` (0.8) | a pattern or an audio track goes between the bank and the clipboard or the undo buffer: COPY, SNAPSHOT, PASTE, UNDO | `int f(unsigned pre, const struct ot_clip *c)` | sys task; 1 before, 0 after. Before a PASTE or an UNDO that stock asked for, nonzero refuses it (below) |
| `ev_part_saving` (0.8) | Part Save is about to copy a Part to its saved copy | `void f(unsigned part, unsigned unused)` | sys task; `ev_part_saved` follows |
| `ev_part_reload` (0.8) | Part Reload (RELOAD, FUNC+CUE) | `void f(unsigned how, unsigned part)` | sys task; `how` 1 before; after, 0 when it reloaded, 2 when the Part had no saved copy |
| `ev_part_select` (0.8) | the panel's Part change | `void f(unsigned phase, const struct ot_part_select *s)` | sys task, when the Part does change: `OT_PSEL_BEGIN`, `_APPLIED`, `_DONE` |
| `ev_part_init` (0.8) | a Part is set to its defaults (the bank init, Part Clear) | `void f(unsigned pre, unsigned part)` | `part` a pointer to it, in any bank's RAM; 1 before, 0 after |
| `ev_project_init` (0.8) | every bank is about to be emptied: the start of LOAD PROJECT and of a new project | `void f(unsigned job, unsigned unused)` | engine task; `job` is the stock job (4 LOAD PROJECT; 9, 10, 13 a new project; 12) |
| `ev_voice_reset` (0.8) | a track's voice is about to be reset (the routine `ev_voice_kill` fires in, at its entry) | `int f(unsigned trk)` | a task or the frame interrupt, interrupts not yet masked; nonzero keeps the voice, and no `ev_voice_kill` follows |
| `ev_boot_early` (0.8) | once, at boot, before the OS | `void f(void)` | core's code and data are in place and the on-chip SRAM has its stock values; no task, interrupt or OS call yet |
| `ev_track_icon` (0.8) | an audio track's box on the screen | `unsigned f(unsigned trk, unsigned state)` | returns the state the box shows (chained). Stock's: bit 0 the track plays, bits 1-2 its recorder |
| `ev_track_icon_draw` (0.8) | a box in a state stock has no glyph for (above 3) | `int f(unsigned trk, unsigned state, int x, int y)` | draw it (`ot_blit`) and return nonzero; 0 leaves the box empty |
| `ev_setup_dial` (0.8) | a page-2 knob of AMP, FX1 or FX2 SETUP is about to be drawn | `void f(struct ot_dial *d)` | change the value, the widget's flags or the widget (chained) |

The sites core-ot owns (Octatrack 1.40C): the boot site 0x4000050c, the 28
arena writes listed in its `mod.json`, 0x40061e94, 0x40013cae, 0x40061dc8,
0x40061e00, 0x40005572 and 0x4000d94e; from 0.4 0x4000d9a6, 0x40010bd0 and
0x400108c4; from 0.5 0x4000aad0, 0x4009c3d4, 0x4009c4d4, 0x4009f5c6,
0x4001fc96 and 0x40055966; from 0.6 the engine task's loop (0x4008484e,
0x4008485e) and the routines its jobs call (0x40090504, 0x400905d4,
0x400917c8, 0x400918aa, 0x4008ee74, 0x4008f180, 0x4000faf0, 0x40025808,
0x4004a9c4, 0x4004a9d0); from 0.7 the voice (0x4000c63c, 0x4000b4f6,
0x40006862, 0x4000f4dc, 0x4000f82c, 0x4000c59e, 0x40004c72, 0x40004d40); from
0.8 the clipboard's and the undo buffer's routines (0x40026eb0, 0x40026ef0,
0x4002b9b0, 0x40029754, 0x4002b654), the Parts (0x4004a908, 0x4004aab4,
0x400621da, 0x40062216, 0x40005638, 0x40005a38), the empty project
(0x400909d8), the voice reset's entry (0x40006820), the boot (0x40000512),
the track icons (0x4004bee2, 0x4004c00c) and the SETUP knobs (0x4003786a,
0x40037c06, 0x4003690c); and its draw gate at 0x400c46ea-0x400c4702. They keep clear of every byte that the
converted octabam modules (4b) patch.

The transport events are the sequencer's own start, resume and STOP routines
(0x4009b964 and 0x4009f5bc): the keys, MIDI start/continue/stop and the
`--sequencer` start all come through them. The sequencer's timer interrupt
(0x400a1e0c) also sets its word to 1 at three places in its own state machine
(0x400a220a, 0x400a24d0 and 0x400a27e2; the first sends MIDI start): starts
the sequencer makes itself, which are not events in 0.5.

#### What core-ot 0.5 exports (`octatrack/core.h`)

```c
#include "octatrack/core.h"
```

- **Where the panel is.** `OT_UI_TRACK` (0x100b14cc), `OT_PART`
  (0x80000003), `OT_PATTERN` (0x80000004), `OT_BANK`; `OT_PLAYING_BANK(t)`
  and `OT_PLAYING_PART(t)` (0x8000182a, 0x80001832: the bank and Part each
  track plays, 0xff while it is not); `OT_PLAYING` (0x800065b8: 0 stopped,
  1 playing, 2 what the STOP routine leaves it at). The edits you make with
  `ot_page2_write` go to `OT_PART`, which is not the Part a track plays when
  a pattern change is queued; `OT_PART_FX1(p, t)`, `OT_PART_FX2(p, t)` and
  `OT_PART_MACHINE(p, t)` read what is on a track in a Part.
- **The frame budget meter.** `ot_meter` (`struct ot_frame_meter`) is kept
  in every build: each frame interrupt's length in DMA timer 3 counts
  (132 MHz; a frame is 47,891), from `ev_frame_begin`'s first instruction to
  `ev_frame_end`'s last, so it includes every handler and the DMA interrupt
  nested in it. `last`, `worst`, `total`/`frames` (the mean), `overruns`
  (frames longer than a frame), `late` (frames that began 1.5 frames after
  the one before) and `tpf` (the counts a frame takes, measured over the last
  1,024). `ot_meter_reset()` starts the counts again. OT HEALTH can read it
  instead of stamping the timer itself.
- **`ot_page2_write(trk, page, slot, val)`** writes a page-2 knob the way the
  panel's editor does: the Part's byte, its battery-backed copy, the dirty
  flags and the engine's live lane, with the value clamped to the knob's
  range. `page` is `OT_P2_PLAYBACK`, `_AMP`, `_FX1` or `_FX2`; it returns the
  value stored, or `OT_P2_REFUSED`. CC MAP, TEMPO BUS and MODE DEFAULTS in
  octabam each make these stores by hand.
- **`ot_boot_info`** (`struct ot_boot_info`: `state`, `want`, `got`) is what
  `.boot` found when it checked its copy of the RAM image (core-ot 0.7.1).
  The linker computes `want` (`__run_hash`) from the image; `.boot` hashes what
  it copied into the reserve (`got`) and halts, before any mod code runs, if
  they differ. A mod that runs sees `state == OT_BOOT_OK` and `want == got`; the
  halted state, `OT_BOOT_FAIL`, is for an emulator or a debugger.
- **`ot_midi_send_paced(msg, len)`** queues a MIDI message (1-255 bytes) and
  core sends one per 120 Hz tick, while the DIN ring has room and the engine
  is idle. The firmware's `midi_send` has no flow control: a burst larger than
  its 4 KB ring hangs the unit (it waits with interrupts masked for an
  interrupt to empty the ring), and a controller that gets hundreds of
  messages a second locks up. Returns 0 when the queue (512 bytes) is full.

#### Project data on the Octatrack (core-ot 0.6)

A mod keeps its own bytes with the project, or on the card for every
project, with the Digitone's descriptor (its code builds for both), in
`octatrack/core.h`:

```c
static uint32_t my_bytes[16];
static void my_loaded(int32_t found) { /* found 0: zeros, set your defaults */ }
const struct core_projdata my_block = { OT_TAG('M', 'I', 'N', 'E'), sizeof my_bytes, my_bytes, my_loaded };
/* after a change: */ core_projdata_dirty(my_block.tag);
```

```json
"contribute": [{"to": "core_projdata", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_block", 0]]}],
"resources": {"core": "0.6", "names": ["projdata:MINE"]}
```

- **Where.** `core_projdata` blocks go in `<set>/<project>/elekloader.work`
  and `.strd`, and `core_carddata` blocks in `/ELEKLOADER/card.work` and
  `.strd`. The stock OS ignores these files.
- **Loading.** After every project load core puts each block's bytes in
  `data` and calls `loaded(1)`. When there are none (a new project, a
  project saved without the mod, or a block of another size), `data` holds
  zeros and core calls `loaded(0)`. This runs in the engine task, before
  `ev_project`.
- **Saving.** `.work` is written from the engine task about 2 s after the
  last `core_projdata_dirty` call, and at SAVE PROJECT. `.strd` is copied
  where stock copies its own `.work` files to `.strd` (SAVE PROJECT), and
  copied back on RELOAD. After a SAVE AS, the next bank write puts the file
  in the new folder.
- **Damaged files.** A file that does not read back (its CRCs) is never
  written over. Core uses `.strd` when `.work` is damaged. When neither
  reads back, core asks at the next encoder turn: NO keeps the file (mods
  run without it until the next load), YES starts fresh, with the old file
  copied to `.bak` first.
- **Other mods' records.** Records of mods that are not in the build are
  kept, and written back.
- **Battery RAM.** A mod contributes an `ot_sram` claim (`{tag, size,
  &pointer}`, claimed as `cs1:<tag>`). At main's end, before
  `ev_boot_done`, core writes the claim's address in battery RAM to the
  pointer (0x100f859c..0x100fff00, a range stock never touches; 0 when it is
  full). The bytes survive a power cycle while the build keeps the same
  claims in the same order; a claim that moved is zeroed. Use it for state
  that should outlive a power cycle without a save, as stock's own working
  copy does, and for settings needed before the card mounts. Whether CS1
  holds with the power off is to be measured on a unit.
- **Card work of your own.** `ot_engine_post(fn, arg)` runs `fn(arg)` in the
  firmware's engine task, between its own jobs. The file calls
  (`ot_file_open`, `ot_file_read`, `ot_file_write`, `ot_file_close`,
  `ot_file_copy`, `ot_mkdir`, `ot_project_dir`) are for there.
- **Settings.** Core declares the `ot_settings` table (`struct ot_setting`)
  for the settings companion (below).

#### Settings on the Octatrack (settings 0.1)

A mod declares a setting by contributing a pointer to a `struct ot_setting`
to core's `ot_settings` table. The struct holds:
- its tag and key, the identity under which the value is stored;
- its kind: binary, option or number;
- its scope: the card, or the project, optionally `| OT_SET_EARLY`;
- its heading and label;
- its range and option labels;
- the mod's variable and its default;
- an optional `changed()`.

The mod reads its variable as it likes. With `mods/settings-ot` (id
`settings`) in the build:
- the MAIN MENU gets a fifth category, MODS, listing every mod's settings
  under its heading, with the value at the right;
- YES steps a value and an encoder turns it;
- values are kept through core's card and project data;
- early ones are also kept in battery RAM and set at `ev_boot_done`.

Without the companion the variable keeps its default. mods/settings-ot/README.md has
an example.

A choice the firmware makes early in the boot needs an early setting
(`OT_SET_CARD | OT_SET_EARLY`). A card setting that is not early reaches
its variable after the project load, once the card is read; an early one is
set from battery RAM at `ev_boot_done`, before it. USB's descriptors are
composed at the controller's init, which the firmware runs in its system
task. In ot_emu that is some 440,000 instructions after main's end, so a USB
function reads an early setting there (`mods/usb-audio-ot`'s layout is one).
It takes effect at the next boot, and the value reaches battery RAM when it
is edited or loaded.

A stock routine that returns a pointer returns it in d0. GCC's m68k ABI
reads a returned pointer from a0, so declare such a routine as returning an
integer, and cast.

#### Voice events on the Octatrack (core-ot 0.7)

The frame interrupt's builder turns each track's mailbox word into the DSP
command byte, the live lanes and the 64-byte record the DSP receives, and the
binder ties a track's voice to its sample. Octabam's synth machines, MUTE
MODES, TEMPO SYNC, PLOCKS P2 and the recorder fixes each detour a few of
those places, and only one module can. The events (`octatrack/core.h` has
their arguments' locations, `OT_DSP_CMD`, `OT_MAILBOX` and `OT_VOICE`):

```c
#include "octatrack/core.h"

/* a synth track's START is always the panel key's clean form */
unsigned my_start(unsigned trk, unsigned cmd)
{
    return (cmd & 0x30) && my_is_synth(trk) ? 0x30 : cmd;
}
```

```json
"subscribe": [{"event": "ev_voice_start", "fn": "my_start", "order": 50}],
"resources": {"core": "0.7"}
```

| event | the call | what a handler may do |
|---|---|---|
| `ev_voice_start(trk, cmd)` | the builder has a DSP command for the track, `cmd` (not 0): a trig the sequencer posted (0x10 plus the sub-frame position), a panel key (0x30), a MIDI note | return the command to use; the next handler gets it, and so do the packer and the DSP. It cannot start a voice (no command, no call) |
| `ev_voice_release(trk, word)` | the track's mailbox word has bit 6, the AMP release (the last key up, a note-off, whatever the sequencer posts); bit 2 is a START in the same word | read; the release bytes 0x80001828/9 are read after the handlers, so they can be changed |
| `ev_voice_kill(trk)` | the reset routine 0x40006820 has cleared the voice's byte in the CF voice array: STOP, a pattern change, the loaders, the preview, a voice that has played to its end, a track with no sample | read. Every interrupt is masked, and the caller may be a task or the frame interrupt |
| `ev_voice_bind(trk, voice)` | the binder is about to write the voice's arena pointers | return nonzero to leave them; the voice is then unbound, and the renderer resets it once or twice a frame |
| `ev_voice_seek(trk, voice, same)` | the binder is about to count a (re)start (`OT_V_STARTS`); `same`: the voice already has this sample | return nonzero to leave the counter, so the frame builder does not send the voice as new (the DSP seeks it) |
| `ev_trig_apply(trk, flags)` | a trig's locks reached the lane; `flags` bit 31 no apply, bit 6 no restore, bit 2 keep the record | read, and write the lane (0x80000810 + 72 t) |
| `ev_dsp_trig(trk, flags)` | the writer is about to build the track's per-trig flag bits into its DSP word | return nonzero to build the word as for a frame with no trig |
| `ev_dsp_record(trk, rec)` | the track's 64-byte DSP record has its FX ids | write it: a store lands last in the frame |

Which octabam module needs what:

| module | its detours | the events |
|---|---|---|
| CX OSC, SYNTH | the start rewrite (`po_retrig`), the release (`po_rel`), the kill (`po_kill`), `po_assign`, `po_machwin`, `po_machlist` | `ev_voice_start`, `ev_voice_release`, `ev_voice_kill`; the last three are machines-ot's (below) |
| OCTA303 | the start rewrite (`o3_retrig`) | `ev_voice_start` |
| BLACKPYRAMIDS-303 | the live release (0x4000b4f6) | `ev_voice_release` |
| PLOCKS P2 | the join of the restore and apply paths | `ev_trig_apply` (its other places are the step events') |
| MUTE MODES | the pointer stores (`mt_rebind`), the DSP word's flags (`trigflag`) and, in its other hooks, the reset's body | `ev_voice_bind`, `ev_dsp_trig`; not the reset's body (it would have to unwind two saved registers, and ordinary trigs do not pass through it) |
| TEMPO SYNC | the FX id store | `ev_dsp_record` |
| RECORDER LOOP FIX | the counter (FLEX SEEKBIND), with its same-sample test (0x4000f8cc) | `ev_voice_seek`; the same-sample test is still its own |

**Not here: `ev_machine_set`.** The three places where a track's machine or
slot is committed (0x400795ba, 0x40079816, 0x4005a848) belong to machines-ot,
which owns the chooser's and SRC SETUP's commits right after them and, from
0.2, the slot assigner's (0x40079520). It decides there whether a commit
happens and what the final byte is, and reads a track's machine from the
Part's signature. An event at the stock writes would run before that
decision, with a byte machines-ot may still refuse. machines-ot 0.2 fires
`ev_machine_set(part, track, from, to)` after the commit (below).

**The sites are not octabam's.** SYNTH (octabam2elemod v1.0) detours 0x4000c634,
0x4000b51a, 0x4000685c, 0x400795ba, 0x40079816 and 0x4005a848, and FLEX
SEEKBIND 0x4000f834. Each event is on the instruction beside the one they
take, so those files link with core-ot 0.7 and a build with both runs both
(SYNTH's `jmp` back into the builder ends at an event's stub).

#### Edits, Parts and the screen on the Octatrack (core-ot 0.8)

A mod that keeps data of its own beside a pattern, a track or a Part has to
follow what the firmware does to them: a copy, a paste, an undo, a Part
saved, reloaded, changed or reset. Octabam's KITS, PLOCKS P2, VARISPEED, CX
OSC, MIDI SCENES and BATCH BUGFIXES each detour those routines, and only one
module can take a place. Core-ot 0.8 gives each place an event, and does the
same for four more places several modules want: the voice reset's entry, the
boot before the OS, the track icons and the SETUP windows' knobs. The
structures are in `octatrack/core.h`.

A mod that keeps a byte per pattern, and follows the clipboard:

```c
#include "octatrack/core.h"

static uint8_t mine[16][16];            /* [bank][pattern] */
static uint8_t on_clip, on_undo;        /* what the clipboard and the undo buffer hold of it */

int my_clip(unsigned pre, const struct ot_clip *c)
{
    if (pre || c->what != OT_CLIP_PATTERN)
        return 0;
    uint8_t *b = &mine[c->bank][c->pattern];
    switch (c->how) {
    case OT_CLIP_COPY:     on_clip = *b; break;
    case OT_CLIP_SNAPSHOT: on_undo = *b; break;
    case OT_CLIP_PASTE:    *b = on_clip; break;
    case OT_CLIP_UNDO:     *b = on_undo; break;
    }
    return 0;
}
```

```json
"subscribe": [{"event": "ev_clip", "fn": "my_clip", "order": 50}],
"resources": {"core": "0.8"}
```

- **The clipboard.** `ev_clip` comes for the five routines that move a whole
  pattern or audio track: the copy (FUNC+REC), the snapshot a paste or a
  clear takes first, and the two stores that paste and undo. `op` says why a
  snapshot was taken (the number stock keeps with its undo buffer: 0x11 a
  pattern paste, 0x12 a pattern clear, 7 a track paste). MIDI tracks and the
  other clipboard kinds (trigs, Parts, slots) do not come here.
- **Refusing a paste.** Before a PASTE or an UNDO, a handler may return
  nonzero: the bank is left as it was, no later handler is asked, no "after"
  call comes, and stock goes on as when there is nothing to paste or undo
  (an undo that finds nothing pastes the clipboard instead, and that paste
  comes here too). Only stock's own paste and undo can be refused: a refusal
  of a call another mod made is ignored. A handler must not call the copy
  and store routines itself.
- **The Parts.** `ev_part_saving` comes before Part Save (SAVE, SAVE ALL,
  and the end of Part Clear) and `ev_part_saved` (0.6) after it.
  `ev_part_reload` says whether the Part had a saved copy. `ev_part_select`
  comes in three steps, only when the Part changes: BEGIN before anything
  moves; APPLIED when every track has gone to the new Part, with the old
  Part's machine bytes; DONE after the screen and the machine page.
  `ev_part_init` comes for each Part set to its defaults (264 of them in a
  power-up's load), with a pointer to it.
- **The project's start.** `ev_project_init` comes in the engine task before
  every bank is emptied, at the start of LOAD PROJECT and of a new project:
  the place to drop what a mod holds of the old project. `ev_project` (0.6)
  comes when the job has ended.
- **The voice reset.** `ev_voice_reset` comes at the entry of the routine
  `ev_voice_kill` fires in, before it masks the interrupts: a handler may keep
  the voice (MUTE MODES keeps a muted track's). The call that resets every
  track comes once for each.
- **The boot.** `ev_boot_early` comes once, after the on-chip SRAM's init and
  before stock clears its RAM above 0x46025de0: no task, interrupt or OS call
  exists yet. Core's reserve is below that RAM, so a mod's own data set here
  stays.
- **The track icons.** `ev_track_icon` may change the state an audio track's
  box shows; stock draws it again when it changes. For a state above 3
  `ev_track_icon_draw` draws it: `ot_blit(&bitmap, OT_SCREEN_PLANE, x, y)`
  with a `struct ot_bitmap` (a long a column, the top pixel at bit 31).
  REC THRESHOLD blinks an armed track's box and REC TRIG MUTE shows a muted
  one with its own glyph: as two handlers they no longer exclude each other.
- **The SETUP knobs.** `ev_setup_dial` comes for each page-2 knob AMP, FX1 and
  FX2 SETUP draw (the knob's enable bit set). A handler may show another
  value (a signed byte: SCENES P2 shows a held scene's lock, PLOCKS P2 a held
  step's), mark it with the widget's flags, or draw it with another widget
  (VARISPEED's quantised dial).

Which octabam module needs what:

| module | its detours | the events |
|---|---|---|
| KITS | the pattern copy, snapshot and store | `ev_clip`; its FUNC+PART and FUNC+CUE handlers replace stock's and stay its own |
| PLOCKS P2 | 21 memcpy calls, told apart by their length | `ev_clip` for patterns and audio tracks; `ev_setup_dial` for its mark |
| VARISPEED | the track store, Part Reload, three SETUP knobs | `ev_clip`, `ev_part_reload`, `ev_setup_dial` |
| CX OSC | its guard before a track paste or undo | `ev_clip`'s refusal |
| BATCH BUGFIXES | the Part change's start and its tail | `ev_part_select`: BEGIN and APPLIED |
| MIDI SCENES | after the Part change (the newest); SAVE, SAVE ALL and RELOAD's calls (octabam2elemod v1.0's) | `ev_part_select` DONE; `ev_part_saving`, `ev_part_reload` |
| MIDI PART STATE | the Part initialiser, the boot after core's | `ev_part_init`, `ev_boot_early` |
| BRAIN, BUFFER SAVER, STORE | the Part initialiser's end; LOAD PROJECT's call of the empty project's RAM; the routine itself | `ev_part_init`, `ev_project_init` |
| MUTE MODES | the voice reset's entry | `ev_voice_reset` |
| BOOT TRACE | the boot after core's | `ev_boot_early` |
| REC THRESHOLD, REC TRIG MUTE | the track icons | `ev_track_icon`, `ev_track_icon_draw` |
| SCENES P2 | the FX SETUP knobs' Part byte | `ev_setup_dial` |

**Not in 0.8.** The ATTRIBUTES page and the playback rate (REPITCH in
octabam2elemod v1.0 patches those bytes), the TEMPO window's opener (TUNER),
the pattern switch (DIRECT JUMP) and the MIDI step builder's end (MIDI SCENES
v1.0) are places where a shop file holds the bytes, so core leaves them. The
OS's entry (REMIX SWITCH's chainloader), the scheduler (BRAIN), MIDI injected
as input (MIDI LOOPBACK) and the bank pointer's stores (MIDI SCENES v1.0) are
one module's each.

### The DSP bus on the Octatrack (dspbus 0.1)

`mods/dspbus-ot` is a second, optional bus, for code that runs on DSP core
0 ("DSP code" in 3.4). It frees SPATIALIZER's 261 words of DSP memory for
mods' DSP code, and takes SPATIALIZER off both FX menus to do it
(mods/dspbus-ot/README.md). A mod with DSP code requires it:
`"requires": ["core", "dspbus"]`. Start from `examples/dsp-tone-ot`.

| event | when | handler | notes |
|---|---|---|---|
| `ev_dsp_rx` | DSP core 0, the head of every audio frame (2,756 a second), before anything reads the current RX block | DSP code, reached by `jsr`, ending in `rts` | the RX block is at `x:>$202`: 16 samples x 4 slots (2 input A, 3 input B, 0 and 1 C and D). Use `a`, `x1`, `r0`, `r1` and the condition codes; keep every other register. Subscribe with `subscribe_dsp` |

### Intercepts on the Octatrack (intercepts 0.1)

`mods/intercepts-ot` is a companion beside core-ot 0.3.1 for places in the
firmware a mod intercepts, one mod at a time: MIDI notes on a listening
track, the CHROMATIC keyboard, p-locks and knobs, the settings and the
project file (19 events; the table is in mods/intercepts-ot/README.md).
Modwerk's Octatrack base has the same events under the same names (its
"take" events), so a record means the same thing on both.

Instead of patching one of these sites, a mod requires intercepts and adds a
record `{claims, entry}` (8 bytes) to the event's table. The first record
whose `claims` is 0 or returns nonzero for the call (`int claims(unsigned
arg)`) intercepts it: its `entry` runs with every register, the condition
codes and the stack as at the site, as a detour's `jmp` (a jmp event) or
`jsr` (a jsr event) would have entered it, does what the site's stock
instructions did, and goes back to stock itself. When no record intercepts
the call, or its entry is 0, stock runs.

```json
"requires": ["core", "intercepts"],
"contribute": [{"to": "ev_knob_store", "order": 50, "data": "0000000000000000",
                "relocs": [[4, "abs32", "sym:my_knob", 0]]}]
```

A detour you already have converts unchanged: its label is the entry, and
claims 0. `sdk.octabam --intercepts` does this for octabam's modules (4b).

### Machines on the Octatrack (machines 0.2)

`mods/machines-ot` (id `machines`) is a companion beside core-ot 0.3.1 for
source machines: a machine mod adds a row to the track's machine chooser
and to SRC SETUP, below PICKUP, with its own name, page, defaults and
sound. The companion owns the places every source machine would otherwise
patch (the chooser, SRC SETUP, the page resolver, the Part validator and the
FLEX renderer), so machines combine; past six rows the chooser scrolls.

A machine's track stays a FLEX track. What makes it the machine is its
signature in the Part (three bytes at Part + 60 + 30 × track: two letters
and a version, the form octabam's machines write), and its parameters are
FLEX's 12 bytes of the track. The machine contributes a pointer to its
record, `struct ot_machine` (`elekloader/sdk/include/octatrack/machines.h`),
which has the layout and meaning of version 1 of Modwerk's record of the
same name:

```c
#include "octatrack/machines.h"

const struct ot_machine my_machine = {
    .abi = OT_MACHINE_ABI, .sig = {'M', 'Y'}, .sig_lo = 1, .sig_hi = 1,
    .label = "MINE", .flags = OT_MACHINE_PROTECT,
    .defaults = my_defaults,        /* the 12 FLEX bytes a track takes when it becomes MINE */
    .render = my_render,            /* calls ot_stock_render, then changes the samples it shipped */
};
```

```json
"contribute": [{"to": "ot_machines", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_machine", 0]]}],
"requires": ["core", "machines"]
```

- **`render(track, ping, start, end)`** is called for the machine's tracks
  in place of the FLEX renderer, once a 16-sample frame (twice when a trig
  splits it). Call `ot_stock_render` with the same arguments and hand back
  its result; then `ot_render_header()` (read before the stock call) says
  how many samples stock shipped (byte 3), from +16 each as two longs, L
  and R, the 16-bit sample in the high half. The stock call needs a sample
  in the track's FLEX slot to ship any.
- **`PROTECT`** keeps stock's Part validator from clamping the 12 bytes to
  FLEX's ranges, when they mean something else to the machine.
- The other fields (`admit_track`, `assign`, `leave`, `chooser_type`,
  `ui_tick`, `page`, `track_page`, `validate`) are optional; the table in
  [mods/machines-ot/README.md](../mods/machines-ot/README.md) says what
  each does, and which 0.1 does not serve yet (pools, DSP voices).

Start from [examples/crush-machine-ot](../examples/crush-machine-ot): CRUSH
plays its FLEX slot's sample through a 5-bit crusher. octabam's OCTA303
converts onto the companion with `sdk.octabam --machines` (section 4b).

From machines 0.2:
- **`ui_tick`** runs in the UI task's tick body (0x4005221e), before the
  screen's builders read the page table, where octabam's machines publish
  their page. A machine with `track_page` has the current track's page
  published there too, so SRC SETUP and the parameter names follow a page
  that differs by track.
- **`OT_MACHINE_LIST_FIRST`** (elekloader's flag, not Modwerk's): the machine
  window opens on the machine list for the machine's tracks, not on the
  sample-slot list, for a machine that does not play a slot's sample.
- **A sample slot picked in a list** that changes the track's machine (a
  STATIC slot on a machine's track) goes through the companion too: the
  machine's `leave` runs before stock resets the track.
- **`ev_machine_set`**: a track's machine changed, after the commit (the
  machine byte and its battery copy hold the new one), from the chooser,
  SRC SETUP or a slot pick. `from` and `to` are rows: 0-4 stock's, 5 on a
  machine's.

```c
void my_machine_set(volatile uint8_t *part, unsigned track, unsigned from, unsigned to);
```

```json
"subscribe": [{"event": "ev_machine_set", "fn": "my_machine_set", "order": 50}],
"requires": ["core", "machines"]
```

### USB functions on the Octatrack (usb 0.2)

`mods/usb-ot` is a companion beside core-ot 0.3.1 that owns the stock USB
device stack's places once. A USB function (USB MIDI, USB audio, ...) is a
mod of its own, and several combine in one build (mods/usb-ot/README.md).
On its own usb-ot changes nothing: every reply is stock's. A function
contributes a pointer to its record and claims its endpoints:

```json
"contribute": [{"to": "ot_usb_functions", "order": 20, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_function", 0]]}],
"requires": ["core", "usb"],
"resources": {"names": ["usb:ep3-in"]}
```

The record is `struct ot_usb_function` (`#include "octatrack/usb.h"`):

| field | what |
|---|---|
| `abi` | `OT_USB_ABI` (2). A record built for `OT_USB_ABI_1` (usb 0.1's) ends before `enabled` and is always on |
| `interfaces` | how many interfaces it adds; usb-ot numbers them after mass storage's 0, in table order |
| `iad_class`, `iad_subclass`, `iad_protocol` | an interface association before them (class 0: none); with one in the build the device class becomes EF/02/01 |
| `endpoints` | its bits in the ENDPT* registers (`OT_USB_EP_OUT(n)`, `OT_USB_EP_IN(n)`): CLEAR_FEATURE and completions go by them |
| `descriptors(out, first, speed)` | writes its interface and endpoint descriptors (no association) for its first interface number `first`, at `speed` (`OT_USB_FS`, `OT_USB_HS`); returns their length. Called at the controller's init, once per configuration |
| `init()` | the controller's init, after the composition |
| `configured(speed)` | SET_CONFIGURATION: bring its endpoints up |
| `down()` | a bus reset, a session end (the cable pulled), and before `configured` |
| `set_interface(rel, alt)`, `get_interface(rel)` | its interface `rel` (0 = its first); a negative return STALLs. Without them alt 0 is the only one |
| `setup(pkt)` | a class or vendor request for it (by interface or endpoint, or any for the device): return >0 once it sent or ACKed |
| `irq(complete)` | its endpoints' completions, from the USB interrupt; clear the bits it takes in `OT_USB_ENDPTCOMPLETE` |
| `enabled()` | ABI 2. Once at the controller's init, before anything else of the function's: 0 leaves it out of this boot, with no interfaces, no association, no descriptors and none of the calls above; a null pointer is always on. Read an early setting here and latch what `descriptors` needs |

usb-ot exports `ot_usb_speed`, `ot_usb_first_interface`, `ot_usb_flush`
(a bounded flush, then the completions cleared), `ot_usb_ack`,
`ot_usb_stall`, `ot_usb_send(len, data)` (EP0 data, at most wLength) and
`ot_usb_irq_off` / `ot_usb_irq_restore`. Memory the controller reads or
writes must be reached through `OT_USB_UNCACHED(p)`. EP0 is usb-ot's and
EP1 is mass storage's. `mods/usb-midi-ot` is the example: two interfaces
under an association, EP2 both ways. `mods/usb-audio-ot` is an
isochronous one: a UAC2 AudioControl and AudioStreaming interface (alt 0
and alt 1, through `set_interface` and `get_interface`), its clock's
requests through `setup`, and EP3 IN fed from core-ot 0.4's
`ev_frame_end`. What its stream carries (MAIN, MAIN+CUE, MASTER, TRACKS,
TRACKS+MAIN+CUE, or OFF) is an early card setting, `enabled()` reads it at
the controller's init, and with OFF it adds no interfaces.

The USB files of irpina/octabam2elemod v1.0 patch these places themselves,
so the linker refuses usb-ot beside them.

### SRC machines (core 2.1, Digitakt mk1)

The Digitakt mk1 has four SRC machines: 0 ONESHOT, 1 WERP, 2 REPITCH and
3 SLICE, kept in a sound's byte +0x7E. From core 2.1 a mod can add one: it
contributes to the table `core_machines` a pointer to a descriptor of six
longs:

| offset | field | |
|---|---|---|
| +0 | id | its number, 4-127. Kits store it, so it is fixed for good: claim it as the resource `machine:<id>`. Taken so far: 4 NEIGHBOR, 5 DIGISLICER, 6 Digi Poly's POLY, 7 SOPHIE (also DT-FM 1.1.0 and older), 8 STRING, 9 DT-FM (from 1.2.0), 20-29 Digi Mono's (20-26 in use), 69 OB-1; pick another, and say which in your mod's README |
| +4 | name | its name in the machine menu and the SRC page's title (10 characters fit) |
| +8 | short | its 4-character name (the SRC page's `NAME: sample` title) |
| +12 | icon | an 11 x 7 Bitmap for the menu, in the stock icons' format, or 0 |
| +16 | params | the stock machine (0-3) whose 8 parameters it takes: their defaults on a switch, MIDI CC 16-23 and NRPN 0x80-0x87, the Randomize and Reload pages, the SAMP lookup of the sample browser |
| +20 | render | the machine it plays as: a stock one (0-3) in the audio render and in that machine's features elsewhere (SLICE's slice locks, its keyboard slice pages, its SRC page state); or its own id, for an empty voice window a mod fills |

```json
"contribute": [{"to": "core_machines", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_machine", 0]]}],
"resources": {"names": ["machine:30"]}
```

or, with the SDK, the same in one line: `"machines": [{"id": 30,
"descriptor": "my_machine"}]`.

With it, the menu lists the machine after the stock four (added ones by
id), with its name and icon; the setter takes it, and a loaded kit keeps
it (stock loads any machine past 3 as ONESHOT, and so does core for an id
no installed mod adds). The firmware gives any machine past 3 SLICE's SRC
page; from core 3.0 a machine describes its own instead ("Machine pages",
below). `core_track_machine[t]` (8 bytes) is each track's own machine as the
render last took it, for a mod whose machine renders as a stock one:
`core_machine(id)` returns an added machine's descriptor, or 0.

### Core 3.0 (Digitakt mk1)

Core 3.0 is core 2.1 and more: the same 39 sites with the same bytes, the
same events and tables, and every 2.1 symbol where 2.1 has it (2.1's code
is the start of 3.0's). A mod built for 2.1 links with 3.0 too, and builds
the same bytes it did with 2.1 apart from where the mods are placed.
`tests/test_sdk.py` checks this against the released core 2.1. What 3.0
adds patches nothing:
- **firmware locations as exports**, so a mod can name them instead of
  their addresses (below);
- **`core_machine_ui(id)`**, the page a machine describes (the
  machine-pages mod draws it: "Machine pages").

A mod that needs 3.0 says so in `resources`, which elekloader 0.4.0 and older
ignore:

```json
"resources": {"core": "3.0"}
```

The loader then refuses it beside an older core with *"needs core 3.0 or
newer"*, and an older loader with the imports it cannot resolve. Core 3.0's
files are on the [core-v3.0](https://github.com/irpina/elekloader/releases/tag/core-v3.0) pre-release and in the
mod shop.

**Which core a build takes.** A builder (the app, the web page, the kit)
takes the newest core it has for the stock OS, which keeps all of every
older one: core 4.0 on the Digitakt mk1, core-dn1 4.0 on the Digitone, for
a selection that needs nothing from 3.0 too. Of the cores new enough for
every selected mod's `resources.core` (the catalog's `needs_core`), it
takes the newest whose RAM holds the mods ("The mods' RAM", below;
[CORES.md](CORES.md), "How a build picks one"). A core chosen by hand that
is new enough and holds the mods is kept.

### Firmware locations (core 3.0, Digitakt mk1)

Core 3.0 exports the firmware data and routines machine mods use as
absolute symbols, with each OS's value in its port. A mod that names them,
and patches no site of its own, builds for another OS version with no
change but a port entry with nothing in it. In C, `#include
"digitakt-mk1/core3.h"` (the SDK has it on the include path); in assembly
use the names as they are.

| name | 1.53 | |
|---|---|---|
| `fw_track_blocks` | 0x80001a18 | the render's block of each track: 32 Q31 samples, 128 bytes a track |
| `fw_render_machine` | 0x800018bc | the machine each track renders as, a byte a track |
| `fw_voice_params` | 0x80002794 | the machine's 8 parameters A-H, 8.8, 106 bytes a track |
| `fw_voice_note`, `fw_voice_vel` | 0x80001f28, 0x80001f18 | the trig's note (16.16) and velocity (8.8) |
| `fw_voice_start` | 0x80001228 | bit t: track t starts a voice this block |
| `fw_amp_env` | 0x4199df54 | the AMP envelope: phase +0, level +4, 12 bytes a track |
| `fw_pitch_tab` | 0x4019b1c0 | 2^((i - 10752) / 2048) in Q29 |
| `fw_active_track` | 0x4197b6b4 | the active track, 0-7 |
| `fw_kit` | 0x4199dc44 | -> the UI kit: track t's sound at +0x20 + 0xa2 t, its machine at +0x7e |
| `fw_slice_layout` | 0x4197cf5c | SLICE's SRC page layout |
| `fw_set_param` | 0x400771e8 | `set_param(value, track, slot)`: a knob's path into the engine |
| `fw_bitmap_vt` | 0x401b73b4 | a Bitmap's vtable (an icon's first long) |
| `fw_fillrect`, `fw_framerect`, `fw_textf`, `fw_font5`, `fw_blit` | | drawing, as `mods/core/fw.s` lists them |
| `fw_op_new` | 0x400d4180 | the firmware's heap |

Each 1.54 value was found by the code that uses it (the render's
locations are the same in both; the rest moved), and is in
`mods/core/mod.json`'s port.

### Firmware locations (core-dn1 3.0, Digitone mk1)

core-dn1 3.0 is core-dn1 2.3 and the firmware locations Digitone mods use,
as exports. It has the same code, sites, tables and symbols, so every mod
built for 2.x links with it as before (`tests/test_sdk.py` checks it
against the released 2.3). A mod that names only these and core-dn1's
tables, and patches no site of its own, builds for 1.43 and 1.44 with an
empty port entry, `"ports": {"1.44": {}}`; it says it needs 3.0 with
`"resources": {"core": "3.0"}`. In C, `#include "digitone-mk1/core3.h"`. It
also declares core-dn1's descriptors (`struct core_param`, `core_page`,
`core_projdata`, `core_menu_item`) and `core_page_open` and
`core_page_shown`. In assembly, use the names as they are. A build takes
the newest core-dn1 for every selection ("Core 3.0" above), so a mod built
for 2.x builds with it too.

| name | 1.43 | 1.44 | |
|---|---|---|---|
| `fw_voice_pitch` | 0x41391f80 | 0x41392f80 | each voice's pitch word, the note << 16 (`ev_voice_on`) |
| `fw_voice_params` | 0x800034c4 | the same | the render's copy of each voice's sound: slot k of voice v at +18 + 158 v + 2 k, 8.8 |
| `fw_voice_len` | 0x80003f14 | the same | each voice's length left, in timeline units (0: none) |
| `fw_gate_off` | 0x80001f74 | the same | bit v: voice v was released in the last block |
| `fw_timeline` | 0x80004614 | the same | the render's timeline: 5,400,000 units a 16th, at any tempo |
| `fw_transpose` | 0x80003fc4 | the same | the transposition, then each track's (+4 + 4 t) |
| `fw_lfo_state` | 0x419ea314 | 0x419eb314 | each voice's LFOs, 0x50 bytes a voice: the destination slot at +0x40, the modulation at +0x44 |
| `fw_ev_alloc`, `fw_ev_free`, `fw_ev_queue` | 0x400ffd7e, 0x400ffdb4, 0x400fff04 | 0x400ffff2, 0x40100028, 0x40100178 | the note queue: an event, freeing one, queueing one at a time on the timeline |
| `fw_lock_alloc`, `fw_locks_free`, `fw_nodes_free` | 0x400ffd2e, 0x419ed230, 0x419ed234 | 0x400fffa2, 0x419ee230, 0x419ee234 | its p-lock lists, and the free lists of those and of its times |
| `fw_kit` | 0x4138e220 | 0x4138f220 | -> the active kit: track t's sound slot k at +0x2c + 326 t + 2 k |
| `fw_slot_ids` | 0x40528644 | 0x40529644 | each sound slot's parameter id |
| `fw_str_amp`, `fw_str_empty` | 0x401d63c3, 0x401ddbcd | 0x401d6793, 0x401ddf9d | "Amp", and the empty string a parameter record's +60 names |
| `fw_fillrect`, `fw_framerect`, `fw_textf`, `fw_font5`, `fw_blit` | | | drawing, as `mods/core-dn1/fw.s` lists them |
| `fw_op_new` | 0x400e944c | 0x400e96c0 | the firmware's heap |
| `fw_voices` (3.1) | 0x80004110 | the same | the render's copy of the DSP's eight voices this block: 32 samples a voice, Q1.31, 128 bytes apart (`ev_render_voices`) |
| `fw_voice_track` (3.1) | 0x80003f8c | the same | each voice's track (an idle voice reads 0) |
| `fw_gate_on` (3.1) | 0x80001f70 | the same | bit v: voice v started in the last block |
| `fw_active_track` (3.1) | 0x41367ce0 | 0x41368ce0 | the active track, 0-3 the synth tracks |
| `fw_params`, `fw_uirecs` (3.1) | 0x4018d104, 0x4136b9fc | 0x4018d454, 0x4136c9fc | the parameter records (60 bytes an id) and their UI records (84 bytes an id) |
| `fw_tempo` (3.2) | 0x40241c94 | 0x40242094 | the tempo the sequencer runs at, x 120 (87.0 BPM: 10440); the timeline moves twice this every block |
| `fw_font_label`, `fw_font_title` (3.2) | 0x4022eb58, 0x4022dc88 | 0x4022ef58, 0x4022e088 | the stock parameter pages' fonts, for `fw_textf`: their labels (FREQ, RESO; capitals 4 x 5) and their title bar (6 pixels, upper and lower case); `fw_font5` is the 3 x 5 one their value boxes use |
| `fw_memcpy`, `fw_memset`, `fw_memchr` (3.3) | 0x400fb864, 0x400fb8ac, 0x4013bfc4 | 0x400fbad8, 0x400fbb20, 0x4013c238 | the firmware's newlib: `memcpy(dst, src, n)`, `memset(p, c, n)`, `memchr(p, c, n)` |
| `fw_strcmp`, `fw_strncmp`, `fw_strchr`, `fw_strncpy` (3.3) | 0x4013cb44, 0x4013cffc, 0x4013c9d8, 0x4013d0ec | 0x4013cdb8, 0x4013d270, 0x4013cc4c, 0x4013d360 | and its strings' |
| `fw_crc32` (3.3) | 0x4010388c | 0x40103b00 | zlib's `crc32(crc, p, n)` |
| `__ashldi3`, `__lshrdi3`, `__ashrdi3` (3.3) | 0x40136ff8, 0x40136fa8, 0x40137048 | 0x4013726c, 0x4013721c, 0x401372bc | libgcc's 64-bit shifts, under their own names (below) |
| `fw_textbox` (3.3) | 0x400de0b8 | 0x400de32c | `textbox(bmp, font, x, y, align, deco, template, fmt, ...)`: clears a box as wide as `template`, then draws `fmt` left (0), centred (1) or right (2); what the stock menus draw values with |
| `fw_transport` (3.3) | 0x4138e208 | 0x4138f208 | the transport, a long: 0 stopped, 1 playing, 2 paused |
| `fw_pattern` (3.3) | 0x4138e214 | 0x4138f214 | -> the active pattern's data: track t at +976 t, its step words at +2 s (bit 0 a note trig, bit 1 a lock trig), NOTE +0x3c0, the per-track length +0x3c5 (big-endian), the keyboard's ROOT +0x3cb and SCALE +0x3ce (`core3.h`'s `FW_TRACK` macros) |
| `fw_project` (3.3) | 0x419efde0 | 0x419f0de0 | -> the project |

A name marked 3.1, 3.2, 3.3, 4.0 or 4.1 needs that core-dn1 (`"resources": {"core": "3.1"}`,
`"3.2"`, `"3.3"`, `"4.0"` or `"4.1"`).

gcc calls libgcc's `__ashldi3`, `__lshrdi3` and `__ashrdi3` for a 64-bit value shifted by a
variable count, and a mod links no libgcc. From core-dn1 3.3, core exports the firmware's
copies under those names, so such calls link with nothing more in the mod. A mod that needs that
core-dn1 must not define these names itself: every global a mod defines is an export, and the linker refuses two
exports of one name (*"... both export __ashldi3"*). With an older core, a mod still brings its
own.

Each 1.44 value was found by the code that uses it, which is the same in
both versions but for the addresses in it. The routines marked 3.3 were also called
with random arguments on both versions' images, against Python's (`memcpy`
to `crc32`, 300-2304 calls each). A routine was found by its own
code and compared to its end; data was found through every piece of code
that names it. The render's SRAM is where it was, and the RAM moved by
0x1000. The values are in `mods/core-dn1/mod.json` and its port. The names
are the Digitakt's (above) where they mean the same.

### Machine pages (core 3.0 and machine-pages, Digitakt mk1)

Every machine mod used to hook the SRC page itself (its layout, labels,
value texts, knob graphics and ranges), the LFO page's names and the
render point after playback, so no two could be in one build. The
`machine-pages` mod (`mods/machine-pages`) owns those places once and
answers each from what a machine describes. A descriptor goes on past
its six longs with a tagged tail, which core 2.1 does not read:

| offset | field | |
|---|---|---|
| +24 | tag | 0x55493330, `"UI30"` |
| +28 | ui | its page, a `struct cm_ui` |

The page (`struct cm_ui`, 336 bytes; `digitakt-mk1/core3.h`):

| offset | field | |
|---|---|---|
| +0 | abi | `cm_ui_v3`, which machine-pages exports: the machine links only beside it; `cm_ui_v31` for a page with a knob that draws itself (`CM_DRAW`), which only machine-pages 1.1 and newer export; `cm_ui_v32` for a page with extra values (below), machine-pages 1.2 and newer; `cm_ui_v33` for one whose extra values have MIDI CCs, 1.4 and newer |
| +4 | page_from | the stock machine whose SRC page it copies (byte): 3 SLICE, 0 ONESHOT |
| +5, +6 | | 0 |
| +8 | group | the LFO page's group for its parameters, or 0 for the stock one |
| +12 | on_switch | `void f(int track, int from, unsigned char *sound)`, called on the UI task when a track of the kit turns to the machine (the machine menu switches as its cursor moves), or 0 |
| +16 | knob[8] | A-H, 40 bytes each |

A knob (`struct cm_knob`); a 0 field is the stock one:

| offset | field | |
|---|---|---|
| +0, +4 | name, lname | its label on the SRC page, and its long name (the pop-up) |
| +8, +12 | lfo, lfo_long | its names on the LFO page: the DEST box's, and the DEST list's (0: name and lname; a blank knob reads "Unused") |
| +16 | look | the parameter id whose UI record and graphic it borrows (byte): how a turn moves it, its knob |
| +17 | flags | 1 blank (it shows and turns nothing), 2 the range min-max, 4 the default def, 8 knob D is not a sample (turning it does not open the sample list; from machine-pages 1.3, SRC keeps a one-page machine's page instead of stepping to the empty waveform view), 16 the machine draws the knob's graphic (`draw`; a `cm_ui_v31` page: on a `cm_ui_v3` page the stock graphic is drawn, and +36 is not called) |
| +18 | cc | on an extra value of a `cm_ui_v33` page: the MIDI CC that sets it (below); else 0 |
| +19 | | 0 |
| +20, +24, +28 | min, max, def | 8.8 |
| +32 | fmt | `int f(char *buf, int value, int ctx, int machine)`: its value's text into buf, returning nonzero (0: the stock text). ctx 0 is the value under a turning knob, about 5 characters; 1 the pop-up, up to 15 |
| +36 | gfx | `int f(int value)`: the value its graphic shows, from the one the firmware gives it (both 8.8) |
| +36 | draw | with flag 16 on a `cm_ui_v31` page, in gfx's place: `int f(void *bmp, int x, int y, int value, int flag)` draws the knob's graphic into the page's Bitmap, in x + 1 to x + 17 and y to y + 16 (y = 0 is the bottom row), from its value (8.8), and returns nonzero; 0 draws the stock graphic. flag is the firmware's, for a stock drawer it calls |

**A graphic that reads another knob (machine-pages 1.5).** Inside a
knob's `gfx` or `draw`, `int cm_src_shown(int k)` gives value k (0-7 the
knobs, 8-14 the extras) of the sound the page shows, 8.8, as the page
shows it: a held step's lock, a sound lock, or the sound's own. That's the
value the sibling knob's own graphic is drawn with. It gives `CM_NO_VALUE`
outside a graphic call, for a k the page has no value for, or when no page
view draws the knob; draw a default then. A machine that calls it links
only beside machine-pages 1.5 and newer. `examples/linked-knobs` draws its
SHAPE knob as a ramp or a pulse by its FORM knob:

```c
static int32_t draw_shape(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    int32_t form = cm_src_shown(2);                     /* knob C, as the page shows it */
    if (form == CM_NO_VALUE)
        return 0;                                       /* the stock graphic */
    ...
}
```

In assembly `digitakt-mk1/core3.inc` has `CM_MACHINE`, `CM_UI` and
`CM_KNOB`; digineighbor 0.7, digislicer 2.3 and SOPHIE's core 3.0 build use
them. `examples/sine-machine` is a whole machine in C to start from: a sine
the trig's note plays and a SHAPE knob folds, its page, its icon, and no
firmware address, so its 1.54 port is empty. A machine with a page needs
core 3.0 and machine-pages:

```json
"machines": [{"id": 30, "descriptor": "my_machine"}],
"requires": ["core", "machine-pages"],
"resources": {"core": "3.0"}
```

Which machine a site answers for: the SRC page's sites, the machine whose
layout the page asked for last (it asks with its track's machine before it
draws or turns a knob); a range, the machine of the sound the parameter
belongs to; the LFO page, the active track's machine. A parameter id is a
knob by the ids of the stock machine the page copies (`0x6c + 8 page_from`
onwards, A-H), or else of its params machine (the ids MIDI, the LFOs and
Randomize use).

**The render event.** machine-pages declares `ev_render_voices`,
`void f(int *blocks)` with `blocks` = `fw_track_blocks`, called after
playback has written every track's block and before the overdrive
(0x40077fba): order 10-49 for a machine that makes a track's sound
(SOPHIE: 20), 50-89 for one that takes or changes sound already made
(NEIGHBOR: 60).

**Extra values on a second page (machine-pages 1.2).** A page that names
`cm_ui_v32` is a `struct cm_ui32` (624 bytes): the `struct cm_ui` above, then
`extra[7]` (+336, knobs as above; each needs flags 2 and 4, min 0, max
0x7F00 and a `look` below 164), `page_count` (+616, 1 or 2) and `pages`
(+620, `struct cm_src_page[page_count]`, 16 bytes each: a title, eight
bytes, each a value 0-14, 8-14 being `extra[0-6]`, or 255 for a blank knob,
then `stock`, a byte whose bit k (`CM_SRC_STOCK(k)`) makes byte k a stock
parameter's id instead, and three zero bytes). A stock knob is one of the
sound's own parameters, the filter's (ids 30-43), the amp's (44-51) or the
LFOs' (52-69), shown on this page too: the firmware's own knob, with its
label, range, locks, LFO and saving; the page's copy, clear and paste take
it with the rest. Any other id refuses the machine's page. SRC flips
the track's page. Only a machine whose params machine is 3 (SLICE's bank)
can have them. They are the sound's slots 46-52, the seven the firmware
leaves unregistered, as parameter ids 164-170: they lock, the LFOs reach
them from their stock DEST list, Randomize and copy/clear/paste take them,
and a sound with them saves as version 4 of the 160-byte record (machine-pages
packs it; the firmware alone cannot read it back). In the render task
`cm_src_value(track, k)` is value k after locks and LFOs. machine-pages
claims `soundslot:46` to `soundslot:52` for them.

**MIDI CCs for the extra values (machine-pages 1.4).** A page that names
`cm_ui_v33` is a `struct cm_ui32` whose extra values may each give `cc`
(+18), the CC that sets the value on the machine's tracks, on the track's
channel. It is clamped to the value's range, as knobs A-H are from 1.3.
Knobs A-H keep their stock CCs, 16-23, and give none. A CC given twice,
or one that MIDI or a stock parameter of a track already uses, refuses
the page. Those left are 8, 9, 11, 12, 24-31, 35-37, 39-59, 64-69, 89-92
and 111. FX, the MIDI tracks and other machines keep the stock answer.
NRPNs and sending a value's CC out are not given. The PARAM fixture
(`tests/fixtures/digitakt-src-parameters`) gives its extras CC 24-30:

```c
.extra = { { .name = "P9", .look = 134, .flags = CM_RANGE | CM_DEFAULT,
             .max = 0x7f00, .cc = 24 }, ... },
```

In assembly, `CM_KNOB cc=24`.

**Sound slots on the Digitakt mk1.** A sound has 53 slots (+0x14, s16);
stock parameters use 1-45. Slot 0 is no parameter's (an LFO's "no
destination" is index 0) and is kept for a machine's own id: no extra
value goes there, and version 4 carries it raw. Slots 46-52 are
machine-pages' (above). No other slot is free.

What it took, and what each port was checked against, is in
`mods/machine-pages/README.md`. A machine that keeps hooking the page
itself (core 2.1's way) still links with core 3.0, but not beside
machine-pages, whose sites it overlaps.

### Parameter slots (core-dn1 2.1, Digitone mk1)

A Digitone mk1 parameter is an id (0-181) with a 60-byte record; the knob
code, its pop-up, its value text, its p-locks, copy and paste all go through
the id. From core-dn1 2.1 a mod can add ids 182, 183 and 184: core raises the
firmware's id bounds (58 of them) and puts the records right after the stock
ones, so the stock UI handles them as its own. The mod contributes to the
table `core_params` a pointer to a descriptor:

| offset | field | |
|---|---|---|
| +0 | id | 182, 183 or 184: claim it as the resource `param:<id>` |
| +4 | group | the page group: 1 Amp, 2 Filter, ... (a sound page's, for a sound parameter) |
| +8 | slot | the sound parameter it edits (0-78): a slot no stock parameter of that kind uses, e.g. 26 on a synth track. The linker claims it for the mod as `soundslot:<slot>` (below) |
| +12, +16, +20 | min, max, default | 8.8 fixed point, as the slot holds them |
| +24 | flags | 0 |
| +28 | cc | MIDI CC (MSB << 16 \| LSB), or -1 for none |
| +32, +36, +40 | | -1, -1, 0 |
| +44, +48, +52 | name, group name, short name | the pop-up's `Name=value`, and the knob's label |
| +56 | format | `void f(int value, char *buf)`: the value's text |
| +60 | | the firmware's empty string: `fw_str_empty` (core-dn1 3.0), `0x401ddbcd` in 1.43 |
| +64 | look | the stock parameter whose knob it borrows (graphic, how a turn moves the value, scale), or 0 for PTIM's (24): a plain knob in whole steps |

To put it on a stock page, replace the instruction of the page's static
constructor that writes that knob's id (or give it a page of its own: "Mod
pages", below): a page's eight ids are written with
immediates, not read from a table: AMP page 1's knob F, empty, is cleared
by `clrl 0x4136b6d4` at `0x4016affe`, so a `jsr` there to a routine that
writes 182 puts id 182 on it.

**Sound slots are scarce.** A Digitone sound is 79 slots, and only three hold
no stock parameter: 0, 26 and 65 (no knob, 0 in every factory sound, no effect
on the sound). Slot 0 is also where an LFO with no destination points. The
machines mod keeps a sound's machine in slot 0; digitables' TBL and SPD are 26
and 65. A mod that keeps anything in a sound slot claims it as the resource
`soundslot:<slot>`, and the linker refuses two mods that claim the same one. A
parameter slot claims its own: the linker reads the slot from the descriptor
(+8, from the mod's bytes or, where the file shares them with the firmware, the
stock image), so a mod whose file says nothing, such as digitables 1.3, claims
26 and 65 all the same. The device profile names the table and the offset
(`slot_claims`).

Core-dn1 2.1 owns these sites too (`mods/core-dn1/mod.json`): the boot call
at 0x40000538 now goes to `core_dn_boot`, which moves two small tables from
0x4018fbac (and 0x4000820a, 0x4000821a and 0x4000b154 follow them) and
installs the records; 0x400899c8 and 0x400899e6, the UI record lookups; and
every `cmpi.l #182`, `cmpi.l #181` and `cmpa.l` id bound but the three loops
over the stock records (0x400078c2, 0x40030f10, 0x40082d6e).

### Mod pages (core-dn1 2.1, Digitone mk1)

A Digitone mk1 parameter page (SYN1's two, AMP's two, ...) is a 44-byte
record: a short and a long title (char pointers; the long one heads the
screen), its eight knobs' parameter ids (0 is an empty box) and a kind (9 for
a sound's page). The firmware has 26, indexed 1-26 and found through one
routine (0x40089a2c); each page key's view keeps the indices of its pages in
a list and steps through them when the key is pressed again. From core-dn1
2.1 a mod can add pages 27-30: it contributes to the table `core_pages` a
pointer to a descriptor, in writable data:

| offset | field | |
|---|---|---|
| +0 | idx | 27-30: claim it as the resource `page:<idx>` |
| +4 | after | the stock page it follows in its key's list: 9 (AMP's second page) makes it AMP's third |
| +8, +12 | short title, title | the title heads the screen, with the page count ("Table (3/3)") |
| +16 | ids | the eight knobs' parameter ids, A to H; 0 is an empty box. Stock ids or a mod's parameter slots |
| +48 | kind | 9 |
| +52 | view | 0: core writes the key's view here when the firmware builds it |
| +56 | pos | core writes the page's place in that view's list |

The stock UI then shows the page, with its key's other pages, and draws,
turns and p-locks its knobs as its own. `int core_page_open(void *brain,
void *event, int key, descriptor *page)`, called from an `ev_key` handler
with its brain and event, opens it: it presses and releases the page's key
(AMP is 24) through the stock key dispatcher until that key's view shows the
page, and returns 0 if the view was never built: from a Mod Menu entry,
say (below). `int core_page_shown(void *brain, descriptor *page)` is 1
while the page is on screen with nothing (a menu, a browser) over it.

Core-dn1 2.1 owns two more sites, by jmp at their entries: 0x40089a2c, the
page record lookup (`core_page_rec`), and 0x40044490, the routine each
view's constructor gives its page list to (`core_view_init`).

### Project data (core-dn1 2.1, Digitone mk1)

A Digitone mk1 project is saved as one block, `projectStorage_v14_t` (2782212
bytes): the OS serializes the project in RAM into a storage buffer and
writes that buffer to the project's slot on the card, to the temp area, or
keeps it as its working copy; a load reads a slot into a buffer and
deserializes it. The block starts with a 32-byte header (0xBEEFBACE, version
14, the name, ...) and its first record is at +0x200: the 480 bytes between
are written and read with the rest, and the OS neither fills nor reads them.
From core-dn1 2.1 mods keep data there. A mod contributes to the table
`core_projdata` a pointer to a descriptor:

| offset | field | |
|---|---|---|
| +0 | tag | four characters: claim it as the resource `projdata:<tag>` |
| +4 | size | bytes, a multiple of 4 |
| +8 | data | the mod's copy in RAM, which it reads and writes as it likes |
| +12 | loaded | `void loaded(int found)`, or 0: called after a project's data is in RAM; `found` is 0 when the project has none for this tag (saved without the mod, or a new project), and data then holds zeros for the mod to fill with its defaults |

Core puts every block in the gap each time the project is serialized, and
takes them out when one is deserialized or a new one is made: the gap holds
`ELKP`, then per block its tag, its size and its bytes, then a zero tag. A
project saved this way loads on the stock OS, which ignores the gap, and the
stock OS keeps a gap it loaded when it saves. The macro mod keeps its 16
tables there (`TBL1`, 320 bytes).

Core-dn1 2.1 owns five more sites: 0x4000eb10, the serializer's entry (by
jmp: every save, to a slot, to the temp area or the working copy);
0x400acc40, the deserialize in the load of a slot, and 0x40010cfa, the one in
the load of a block already in RAM (at boot, and from SysEx), both by jsr;
0x4001570e, the new project's builder (CREATE NEW, by jmp), and 0x400acbf4,
the template copy of slot -1's load (by jsr).

### Mod storage (core-ar2 0.2, Analog Rytm MKII)

From core-ar2 0.2 a Rytm mod can keep its own values with the project:
per sound, kit, global slot, pattern track, pattern or project. Core-ar2
0.3 is for OS 1.74 only; 0.2 is the last core with 1.73, and stays as it
was released. A 1.73 build takes 0.2, the newest core for that OS, with
0.2's room and saves; a mod that needs 0.3 (`"core": "0.3"` in
`resources`) builds for 1.74 only. They live in
bytes the stock OS never reads or writes (from 0.3 also in bytes it only
ever holds at 0, read where it matters through sites core hooks), so a
project saved with mods still loads on the stock OS, and no stock function
gives up any space. A mod
declares each field in its mod.json:

```json
"storage": [{"sym": "my_accent", "scope": "track", "bits": 4}]
```

`scope` is one of `sound`, `kit`, `global`, `track`, `pattern` and
`project`; `bits` is 1 to 32; `sym` is a C name. Two fields may not share a
`sym`. The mod reads and writes the field through core, with
`elekloader/sdk/include/analog-rytm-mk2/store.h`:

```c
#include "analog-rytm-mk2/store.h"
STORE_FIELD(my_accent);
unsigned v = core_store_get(STORE(my_accent), live_pattern, track);
core_store_set(STORE(my_accent), live_pattern, track, v + 1);
```

| scope | item | index | room per item | kept in |
|---|---|---|---|---|
| `sound` | the live sound (a kit's, or the pool's) | unused | 56 bits (24 with core-ar2 0.2, so on 1.73) | the sound's own spare bytes, so it travels with the sound (library, SysEx, copy and paste); bits 24..55 sit in the velocity routes' amount low bytes, which stock holds at 0 |
| `kit` | the live kit | unused | 64 bits | the kit's own spare bytes, so it travels with the kit |
| `global` | unused | the slot, 0-3 | 40 bits | the global slot's spare bytes |
| `track` | the live pattern | the track, 0-12 | 8 x T bits | the project's tagged store |
| `pattern` | the live pattern | unused | 8 x P bits | the project's tagged store |
| `project` | unused | unused | what is left | the project's tagged store |

A sound's room depends on the core: 0.3 keeps 56 bits, 0.2 24, and the
allocator gives a build what its core keeps (a mod that needs more than 24
says `"core": "0.3"` in `resources`). Bits 0..23 are the same bytes with
either core, so a 0.2 build's lockfile keeps its places with 0.3, and the
sounds it saved load there with the new bits at 0.

Zero is always the default: a field reads 0 in a new project, in a project
saved by another layout, and after its scope was reset (below). Core exports
no firmware locations, so a mod names the live objects it passes as `item`
itself, for each OS. While an item is live its fields sit inside the stock
object, so copy, paste, undo and pattern changes carry them with no extra
code. One byte can hold fields of several mods, so `core_store_get` and
`core_store_set` mask interrupts while they read or write a field: call them
from any context, the audio interrupt too.

**The tagged store.** One byte of each of the 256 entries of the 17 songs in
a project image is never used by the stock OS: 4,352 bytes. Core keeps
there a 10-byte header (the layout ID, then one generation per scope), then
T bytes for each of the 129 x 13 pattern tracks, then P bytes for each of
the 129 patterns, then the project's own bytes. T and P start at 2 and 4,
and grow when the mods need it (T can't reach 3: the store would not fit,
so `track` is limited to 16 bits a track). P growing moves the patterns'
and the project's regions, so their data in older projects loads as zeros;
the pattern tracks come first and keep theirs. The stock OS does not keep these
bytes in a project file, so core also hooks the file save and load
(Transfer's `.arprj` and the card) and Total Recall's buffer.

**When a field reaches the project.** A live item's fields go into the
stored project when stock saves that item there. A sound's or kit's go with
the stock save of that sound or kit. Pattern-track and pattern fields go
whenever stock saves any part of that pattern (from core-ar2 0.3: an edited
track, the pattern's settings, its parameter locks; with 0.2, so on 1.73,
only the whole pattern, at SAVE PROJECT or into a project file), and then the whole
pattern's. A field a mod sets from its own page with no stock edit is
kept by the next of those saves, as stock's own data is; until then a power
cycle loses it, as it loses an unsaved stock edit.

**The lockfile.** Where each field sits is decided once and then kept, in a
JSON file: `elekloader.patch --lockfile FILE` (default:
`analog-rytm-mk2.lock.json` in the output's folder, so every build written
there shares it); the desktop app keeps one per profile; the web bridge
takes it as `store` and returns the new one. A build that adds a field puts
it in bits no field ever used. A field whose mod leaves the build keeps its
bits, and no other field is given them: when the mod comes back with the
field unchanged (the same `sym`, `scope` and `bits`), the field takes its
old place, with the values it had. A field whose scope or width changes
starts again from zero. Each layout has a 4-byte ID, a hash of the layout,
and a generation per scope: the same mods built without a lockfile always
get the same layout and ID, and two layouts share an ID only when they
place every field alike. Every change gives a new ID, and the build keeps
the last 15 IDs as ancestors, so a project saved by an older layout still
loads. When a scope is full the allocator bumps its generation and packs
that scope's fields again from bit 0: the scope's data in projects saved
before then loads as zeros. Keep the lockfile: a build made without it packs
the fields afresh, which reads the old data only when the mods are the same
as when it was first made. Copies go in the build's manifest (`store`) and in the build
itself, after the mods' image (`ELKSTORE`, its length, then the lockfile's
text; about 1-2 KB, never copied into RAM), so a lost lockfile comes back
from the last .syx: `elekloader.patch --lockfile-from OLD.syx`. The unit
itself can't give it back: it never sends its OS out.

**What is checked.** Sounds and kits carry the layout ID and their
generation in their own spare bytes and are checked on every load. Pattern
tracks, patterns, the project and globals are checked against the header of
the image they come from. Data that fails the check loads as zeros. A
global slot received as SysEx has no room for an ID, so its mod data is
always dropped, even from the same build: only its stock settings load.
Projects saved before OS 1.70 are not supported.

**When a project changes.** A mod that keeps its own copy of a field, or
sets things up per project, subscribes to `ev_project`:

| event | when | C prototype | notes |
|---|---|---|---|
| `ev_project` | Analog Rytm MKII (core-ar2 from 0.2) and Analog Four MKII (core-a4 0.1): after CREATE NEW or the project reset (`how` 0, `CORE_PROJECT_NEW`), and after every project load (`how` 1, `CORE_PROJECT_LOADED`: at boot too); the stored project and its mod data are in place | `void f(int how, unsigned dropped)` | `dropped` has `STORE_DROPPED(scope)` set for each of `STORE_GLOBAL`, `STORE_TRACK`, `STORE_PATTERN` and `STORE_PROJECT` that loaded as zeros: saved by another layout, or with no mods. Sounds and kits are checked one by one as they load, so they are not in it. It comes from inside the load: keep it short, and don't start another load or save from it |

Core-ar2 0.2 owns 22 more sites, the same on 1.73 and 1.74 except the file
save (0x400b0804 on 1.73, 0x400b0870 on 1.74) and the SysEx copies: 0x4000937c and 0x40009146
(the pattern serializers), 0x4000843a and 0x400081f6 (sounds), 0x400088d6
and 0x4000863e (kits), 0x40007a5c and 0x4000763c (global slots), 0x40011ab8
and 0x400118e8 (pattern and track init), 0x400122b6 (sound init),
0x40011d38 (CREATE NEW and the project reset), 0x4000cd2e (every project
load), 0x40034d86 (Total Recall's buffer save), the file save, and
0x40035000 (the file load), and the three calls through which the SysEx
project commands copy, write and swap whole stored songs (0x4018b6ec,
0x4018c09a, 0x4018c2bc on 1.73; 0x4018b8b8, 0x4018c266, 0x4018c488 on 1.74)
and whole stored patterns (0x4018ba90, 0x4018c736, 0x4018c968 on 1.73;
0x4018bc5c, 0x4018c902, 0x4018cb34 on 1.74).
Core-ar2 0.3, on 1.74 only, owns those 22 at their 1.74 addresses and 10
more: the calls through which stock saves an edited pattern track, a
pattern's settings and its parameter locks by themselves (0x400bf818,
0x400aacc4, 0x400ac2ce and 0x400ac2ec), and the velocity routes' push to
the engine (0x4011f6a8), the VELOCITY MOD menu's getAmount (0x400c5326),
setAmount (0x400c52b2), setRoute (0x400c51b4) and reset (0x400c542c), and
the aftertouch menu's setRoute (0x4003391e).
`examples/store-check-ar2` uses a field in each
scope and shows which no longer read back.

### Mod storage (core-a4 0.1, Analog Four MKII)

core-a4 has the Rytm's mod storage, above, with the A4's own objects: the
same six scopes, `storage` declarations, lockfile, tagged store, checks and
`ev_project`. A mod includes `elekloader/sdk/include/analog-four-mk2/store.h`
instead, and its lockfile defaults to `analog-four-mk2.lock.json`. What
differs:

| scope | item | index | room per item | kept in |
|---|---|---|---|---|
| `sound` | the live sound (a kit's, or the pool's) | unused | 32 bits | the sound's own spare bytes, with its copy of the layout ID and generation |
| `kit` | the live kit | unused | 128 bits | the kit's own spare bytes (its effects block's unused words) |
| `global` | unused | the slot, 0-3 | 64 bits | the global slot's spare bytes, with its own copy of the ID: a slot sent and received as SysEx by the same layout keeps its mod data |
| `track` | the live pattern | the track, 0-5 (four synth tracks, FX, CV) | 8 x T bits | the project's tagged store (129 x 6 pattern tracks) |
| `pattern` | the live pattern | unused | 8 x P bits | the project's tagged store |
| `project` | unused | unused | what is left | the project's tagged store |

- The A4 writes each parameter edit of the current kit straight into the
  stored project's copy of that kit, so `core_store_set` writes a field of
  the current kit or its sounds there too: like a stock edit, it survives a
  power cycle without a save. And as stock's own edits do, every mod field
  survives a power cycle straight after SAVE PROJECT AS: core copies what
  the save wrote of its own bytes back into the stored project.
- No velocity routes: the sound's room needs no extra sites.
- core-a4 owns 28 storage sites (mods/core-a4/README.md lists them), among
  them a kit init the Rytm's core does not need.
- Checked in rytm-emu's A4 build and on a unit (store-check-a4). The spare sound and
  effects words reach an engine slot the stock OS appears not to use; on
  a unit, a pattern sounded the same by ear with every field set.

`examples/store-check-a4` is store-check-ar2 for the A4.

### Parameter pages (core-ar2 0.4, Analog Rytm MKII)

From core-ar2 0.4 core owns the entries of the four stock routines every
parameter page goes through, and a mod adds a page, or changes how a stock
parameter turns or reads, through two tables instead of hooking them
itself. Before 0.4 each mod jumped from the first instructions of these
routines, so two mods with pages could not be in one build. The routines
(OS 1.74):

| routine | entry | what it does |
|---|---|---|
| page_info(page) | 0x400ff5e0 | a page's entry: its name, then the ids on knobs A..H |
| page_get_value(view, id, lock) | 0x40038462 | a knob's value, 8.8 |
| param_apply_delta(view, id, delta, func) | 0x400378a6 | a turn: delta 8.8, func 1 while FUNC is held |
| param_value_text(obj, id, value, buf) | 0x400a864c | the value's text |

A mod contributes to `core_pages` (one entry per page of its own) and to
`core_params` (one entry per range of ids it answers for, its own pages'
or stock's), with `elekloader/sdk/include/analog-rytm-mk2/pages.h`:

```json
"contribute": [
  {"to": "core_pages", "order": 14, "data": "00000000",
   "relocs": [[0, "abs32", "sym:my_page", 0]]},
  {"to": "core_params", "order": 50, "data": "00000000",
   "relocs": [[0, "abs32", "sym:my_params", 0]]}
]
```

```c
#include "analog-rytm-mk2/pages.h"
const struct core_page my_page = {14, "MY PAGE", {449, 450, 0, 0, 0, 0, 0, 0}};
const struct core_param my_params = {449, 450, my_get, my_delta, my_text};
```

- `get(id, &value)` returns 1 with the value in 8.8, or 0 to pass.
- `delta(view, id, delta, func)` returns 1 when it took the turn: core then
  redraws the view, and stock neither applies the turn nor makes a lock.
  0 passes.
- `text(t)` gets param_value_text's own arguments as a `struct core_text`.
  It returns 0 to pass, 1 when it wrote `t->buf`, or 2 to have stock format
  `t->id` and `t->value`, which it may have changed (a page knob shown as a
  stock parameter's text).

Any of the three may be 0. Entries are asked in `order`; the first that
answers wins, then stock.

**A page whose knobs follow the sound (core-ar2 0.6).** A page that leaves a
knob empty for some settings of the selected sound (voice-dac's DAC FILTER
has no RES for its 1-pole types, and neither FRQ nor RES with TYP OFF)
contributes to `core_page_fns` instead of `core_pages`:

```json
{"to": "core_page_fns", "order": 14, "data": "00000000",
 "relocs": [[0, "abs32", "sym:my_page_fn", 0]]}
```

```c
static const struct core_page *my_info(int32_t page)
{
    return my_type() ? &my_page : &my_page_short;
}
const struct core_page_fn my_page_fn = {14, my_info};
```

page_info asks `info(page)` on every call, before `core_pages`, and answers
the entry it returns (its own `page` field is not read); 0 passes to
`core_pages` and stock. Every reader of a page calls page_info afresh (the
knobs' draw, the knob -> id lookup of a turn), so the page follows at its
next redraw, and an empty knob draws stock's empty-knob mark and takes no
turn, as on stock's REVERB and DISTORTION pages. `info` runs in the UI
task, with the C convention (d0/d1/a0/a1 scratch). A page id must be one stock does not have
(0..11) and no other mod in the build uses (rytm2_mods use 12 and 13, the
AE ports 14 and 15). Core only passes calls on, so a build with no entries
behaves as stock.

**The page keys' lists.** A page key steps through its view's list of
pages. Core builds each list: stock's pages first, in stock's order, then
the pages mods add through `core_view_pages`, in `order` (ties by mod id),
so the first press of a page key lands where it does on stock and several
mods can add pages behind one key:

```json
{"to": "core_view_pages", "order": 30, "data": "00000000",
 "relocs": [[0, "abs32", "sym:my_view_page", 0]]}
```

```c
const struct core_view_page my_view_page = {CORE_VIEW_COMP, 14, 0};
```

The third field, `shown`, is 0 or `int32_t shown(void *view)`: the page key
asks it on each press and steps over the page while it answers 0 (voice-dac
hides DAC FILTER while DAC SUM is off). Core also does two things for every
view, so no mod needs to: it puts the new page's title in the header after
each step (stock showed the title of the page left), and a view keeps its
page when it is shown again (stock sent the FX track's views back to their
first page; the voice track's already kept theirs).

The views are `CORE_VIEW_SRC`, `SAMPLE`, `FILTER`, `AMP`, `LFO` (a voice
track's), `DELAY`, `REVERB`, `DIST`, `COMP` and `FX_LFO` (the FX track's).
A page may be a stock one listed again (comp-preview lists page 11 a
second time); a mod that does so tells its entry apart by the view's
selected index (+0x8c) and list (+0x7c), not by "past the first entry",
since other mods' pages can follow. Lower `order` sits closer to stock's
pages; rytm2_mods and the AE ports use 20 for a key's first added page and
30 after it. A view holds 16 pages at most. Deeper hooks inside these routines (a mod that rewrites
one value's text further in) still work: core resumes each routine where
its own jump ends.

### Parameter pages (core-a4 0.2, Analog Four MKII)

core-a4 0.2 has core-ar2 0.4's `core_pages` and `core_params` at the A4's
four routines, with the same calls and answers as above, and core-ar2's
`core_audio_load`. With `elekloader/sdk/include/analog-four-mk2/pages.h`
(and `load.h`):

| routine | entry (OS 1.56) |
|---|---|
| page_info(page) | 0x400f6212 |
| page_get_value(view, id, lock) | 0x40023d54 |
| param_apply_delta(view, id, delta, func) | 0x400232e2 |
| param_value_text(obj, id, value, buf) | 0x4009ea68 |

What differs from the Rytm:
- A page's entry has ten knobs, A..J: `struct core_page` is
  `{page, name, ids[10]}`. Stock's pages are 0..47, so a mod's own page id
  is 48 or more.
- `core_view_pages` is the A4's own. Each A4 page is a view of its own (29
  of them, indices 0..28, made by the main screen's constructor), and a
  page key steps through a list of views for each track type. An entry is
  `{type, key, page, shown}`: core makes a view for the page (a mod's own
  from `core_pages`, or a stock one), gives it the next index from 29, and
  appends it to that key's list for that track type, after stock's views
  and in `order`:

  ```c
  const struct core_view_page my_view_page = {CORE_TRACK_SYNTH, CORE_KEY_AMP, 50, 0};
  ```

  The types are `CORE_TRACK_ALL` (MIX, PERF, ARP and NOTE), `SYNTH`, `FX`
  and `CV`; the keys `CORE_KEY_PERF`, `ARP`, `NOTE`, `OSC1`, `OSC2` (not on
  the FX track), `FILTERS`, `AMP`, `ENV`, `LFO` and `MIX`. `shown` works as
  on the Rytm. A build holds 16 such views.
- The current view is saved with the project, one index for each track
  type. Core widens the range stock accepts for it, 0..28, to 0..44, so a
  mod's view comes back after a power cycle or a project load; a project
  saved on a view the build lacks opens on MIXER, stock's fallback for an
  index with no view (stock alone opens it on PERFORMANCE).
- Stock already keeps each track type's page when its view is shown again,
  and shows each view's own title, so there is nothing to fix there.
- A stock page's entry can be answered too (a `core_pages` entry with a
  stock id), to retitle a page a mod's view follows.
- From core-a4 0.3, `core_page_fns` works as core-ar2 0.6's ("A page whose
  knobs follow the sound", above): `struct core_page_fn` is `{page, info}`,
  and `info(page)` returns the `core_pages` entry to answer, or 0 to pass.
  The knobs' draw, a knob's touch and a turn each look the knob up through
  page_info afresh, so the knobs follow at the next redraw. A page's view
  reads the name once, when core makes it at boot, so every entry `info`
  returns should carry the same name.

`examples/pages-check-a4` is the smallest mod on it: AMP PAN's value as two
hex digits through `core_params`, and a CHECK page behind the AMP key. Its
0.1, for core-a4 0.2, was checked in rytm-emu's A4 build and on a unit (23
Oct 2026). From 0.2 (for core-a4 0.3) CHECK comes from `core_page_fns`, and
its knob B, AMP PAN again, is shown while the count of PAN's turn updates
(the bar) is odd and empty while it is even. Checked in rytm-emu's A4 build
and on a unit (24 Oct 2026).

### Machines (core-ar2 0.5, Analog Rytm MKII)

The Rytm has 53 machine slots. 34..52 hold hidden factory machines: stock
gives them a voice mask of 0, so no track may pick them, and nothing else
of theirs is used. From core-ar2 0.5 a mod takes one of slots 34..43 for a
machine of its own through the table `core_machines`, with
`elekloader/sdk/include/analog-rytm-mk2/machines.h`:

```json
"contribute": [
  {"to": "core_machines", "order": 50, "data": "00000000",
   "relocs": [[0, "abs32", "sym:my_machine", 0]]}
],
"resources": {"core": "0.5"}
```

```c
#include "analog-rytm-mk2/machines.h"
static void my_engine(int32_t voice, void *frame, uint16_t *blk, int32_t pitch)
{
    CORE_ENGINE_BD_CLASSIC(voice, frame, blk, pitch);
}
const struct core_machine my_machine = {
    36, my_engine, 0, CORE_VOICE_BD, 0, "BD COPY", "BDCP",
    {{"Level", "LEV", 0, 127, 100, 0}, {"Tune", "TUN", 32, 96, 58, 1}, /* ... */}};
```

At boot, before the OS reads them, core writes the entry's engine, home
track (the machine list files it under that track; -1 for none), voice
mask and names into the machine table, and each knob's name, label, range,
default and centring into the slot's parameter records. A record keeps
its id, its index in the sound (9..16, LEV first) and its CC and NRPN, so
sounds store the knobs and MIDI reaches them as any machine's. A knob
whose name is 0 keeps its record as stock has it. Slots 35..42 have all
eight records; 34 and 43 have five, and a knob with no record is ignored.
Each entry's slot is the resource `machine:<n>`, which elekloader names
for it, so a second mod with the same slot is refused at link. Slots
44..52 (the TEST machines) are not offered, since mods already use their
records as spare parameters.

The engine is called once per 32-frame block for each voice running the
machine, from the audio interrupt, as stock's are: `frame` is the analog
control frame and `blk` the track's parameter block (word k is the
sound's index 9 + k). It may call a stock engine (`CORE_ENGINE_*`) with a
block it fills itself, which is how the AE TX machines are built. A value's
text, or how a knob turns, can be changed through `core_params` (Parameter
pages, above).

A kit or sound that uses the machine, loaded on a build without the mod,
gets DISABLE on that track, as stock does for any machine its voice may
not run. `examples/machine-ar2` is the smallest machine: BD COPY, BD
CLASSIC under another name.

### The Mod Menu (core-dn1 2.2, Digitone mk1)

Holding a track key (T1-T4) on its own, about half a second, is core's: it
calls the handlers of `ev_hold` first, and when none takes it, it opens the
Mod Menu, a screen of what mods contributed to the table `core_menu`.
Each entry is a pointer to a descriptor:

| offset | field | |
|---|---|---|
| +0 | name | its label in the menu, 14 characters (core-dn1 2.2's list showed about 20) |
| +4 | open | `void open(void *brain, void *event, int track)`: picked. `event` is the key's that picked it, `track` the one held (0-3); with them a mod can open its page (`core_page_open`) or a screen of its own |
| +8 | tag | from core-dn1 2.3, optional: `0x49434F4E` ("ICON") when an icon follows |
| +12 | icon | a pointer to the icon: 16 rows of 16 pixels, an `unsigned short` each, the top row first, bit 15 the left pixel. 0 for core's own |

From core-dn1 2.3 the menu is a grid of tiles like an old PDA's launcher,
two across and two down with a scroll bar beside them. A tile has the
entry's icon over its label and, in its corner, the trig key that picks it;
the selected tile is drawn inverted, and a slot past the last entry shows
only its corners. An entry without an icon (a 2.2 descriptor of eight
bytes, or 0 at +12) gets core's own, a chip. Core 2.2 reads only +0 and
+4, so a descriptor with an icon works with either core; core 2.3 reads
the four bytes after a 2.2 descriptor too, and takes an icon only when
they are the exact tag. digitables' TABLES, in C:

```c
static const unsigned short digitables_icon[16] = {    /* table 1 as bars */
    0x0000, 0x0e00, 0x0e00, 0x0e00, 0x0e00, 0x0ee0, 0x0ee0, 0x0ee0,
    0x0eee, 0x0eee, 0x0eee, 0xeeee, 0xeeee, 0x0000, 0xffff, 0x0000
};

const struct {
    const char *name;
    void (*open)(void *brain, void *ev, int track);
    unsigned long tag;                  /* 0x49434F4E, "ICON": an icon follows */
    const unsigned short *icon;
} digitables_menu = { "TABLES", digitables_menu_open, 0x49434F4EUL, digitables_icon };
```

In the menu the arrows or any knob move the selection (UP and DOWN a row,
LEFT, RIGHT and the knobs a tile), YES or trig key N picks, NO or holding a
track key again closes it, and a page key (TRIG ... LFO) closes it and goes
on to its page; PLAY, STOP, FUNC and the track keys work as ever. A mod takes the hold from `ev_hold` when it has a better use
for it where it is: the macro mod lists TABLES (its TBL page) in the menu,
and opens its table editor when the hold comes with the TBL page on screen.

The hold is the stock key event's flags 0x9 (down, held), about 0.4 s in. A
key pressed meanwhile stops it, but one pressed before does not (MIDI held,
then a track key, selects a MIDI track) and neither does a knob, so core
counts a hold only when the track key went down with no other key down and
no knob turned since. The stock OS does nothing with 0x9 on a track key held
alone; a double press (the sound browser) and FUNC + a track key (mute) are
untouched.

On the Digitone the four UI sites (0x4001900c, 0x40019072, 0x40019d9c and
0x40019de4) go to core-dn1's own `core_dn_tick`, `core_dn_draw`,
`core_dn_key` and `core_dn_enc` (`menu.s`), which see to the menu and go on
to core.s's handlers, so core.s stays the Digitakt's.

**Submenus (core-dn1 3.1).** From an entry's `open()`, `void
core_menu_open(const void *const *list, int sel, void (*pick)(void *brain,
void *event, int track, int index))` shows another list in the same grid:
`list` holds descriptors like `core_menu`'s, ending in 0, and `sel` is the
one selected first. A pick then calls `pick` with the entry's index in place
of the entry's `open()`, after the menu has closed and gone back to its
root, so `pick` may open another list. NO closes a submenu like the menu, and
holding a track key always opens the root. The machines mod's MACHINES
entry opens one (below).

### Machines and stock parameters (core-dn1 3.1, Digitone mk1)

On the Digitone mk1 the DSP (a second ColdFire) renders the eight FM voices,
and the main CPU's audio render (vector 191) runs each through its
multimode filter, then mixes them with the tracks' levels, pans and effect
sends. Core-dn1 3.1 adds what a mod needs to play its own sound in a voice's
place and to show its own parameters on a sound's pages.

**`ev_render_voices`.** At 0x4009e078 (0x4009e098 in 1.44) the render calls
a routine (0x40098fa4) just before its loop over the voices. By then eDMA
channel 47 has copied the DSP's eight voices for the block to SRAM at
0x80004110 (`fw_voices`): 32 samples a voice, signed Q1.31, 128 bytes apart.
Core makes that call itself, then calls each handler, `void f(int32_t
*voices)` with `voices` = `fw_voices`. A handler may write any voice's
block: what it writes is the voice's sound from there on, through its filter
(the FLTR page), the mixer and the effects. The DSP applies the AMP page's
envelope before its output, so a block written here has none of its own.
Interrupt level, 1500 blocks a second, inside the render, which uses the
EMAC: a handler that uses it leaves MACSR as it found it. For a voice v,
`fw_voice_track[v]` is its track, `FW_VOICE_PARAM(v, k)` its sound's slot k
(the locked values of the trig it plays), `fw_voice_pitch[v]` its pitch,
and bit v of `fw_gate_on` and `fw_gate_off` says it started or was released
in the last block. `ev_voice_on` passes the note event, whose +0x10 is the
velocity << 8.

**`core_param_override`.** A mod may answer for a stock parameter while it
wants to (on its own tracks, say) by contributing to the table
`core_param_override` a pointer to a descriptor:

| offset | field | |
|---|---|---|
| +0 | ui | `void *ui(int id)`: a UI record (84 bytes, the stock layout) for the id, or 0 for the stock one |
| +4 | name | `const char *name(int id, int field)`: the label for field 0x30 (the knob's short name), or 0 |

Either may be 0; the first mod that answers wins. The value, its sound slot
and its range stay the stock parameter's. A UI record holds the knob's kind
at +0, how a turn moves the value at +4 to +0x10 (its stepper: ALGO steps
once every few detents, DTUN in fine steps), the value text's formatter at
+0x14 (`void f(int value, char *buf)`, the value 8.8) and three draw
delegates from +0x1c (the graphic, the value, the pop-up).
`core_param_ui_make(ui, id, look, fmt)` builds one: id's turn, the graphic
of the stock parameter look (0 for PTIM, a plain knob; -1 for id 0, the
firmware's 'none') and fmt as its text (0 keeps id's). A whole record copied
from another parameter brings that parameter's turn along: a plain knob's
over ALGO jumps two algorithms a detent. Core consults the table in the UI
record lookups (0x400899c8 and 0x400899e6) and replaces the routine that
makes a knob's label (0x400207b8, by jmp; the same address in 1.44). The
pop-up's long name, the LFO destination lists and a page's group headings
read the parameter records on their own and keep the stock names, and a
page view's own drawing stays (SYN1's algorithm diagram, SYN2's envelope
graphs).

**`core_sound_set`.** `void core_sound_set(int track, int slot, int value)`
sets the sound slot (0-78) of synth track `track` (0-3) to `value` (8.8)
as a stock edit does: in the kit, then in the voices playing the track (the
stock setter's last step, 0x4009c972; 0x4009c992 in 1.44). A write to the
kit alone (`FW_SOUND_SLOT`) is not enough: a voice keeps a pointer to the
sound it loaded last (0x80003f5c + 4 v) and loads it again only for another
sound, so a voice that played the track before keeps the old value until a
stock edit or another sound reloads it. From the UI task.

Core-dn1 3.1 owns two more sites: 0x4009e078 (`core_render_voices`, by jsr)
and 0x400207b8 (`core_param_short`, by jmp).

### Machines on the Digitone (machines, Digitone mk1)

The `machines` mod (`mods/machines-dn1`) makes core-dn1 3.1's pieces into
machines: sounds that play a machine mod's voice in place of FM. A sound's
machine is its slot 0, which no stock parameter uses: the machine's id << 8,
0 for FM. The voice loads it with the rest of the sound at each note.
MACHINES in the Mod Menu opens a submenu of FM and the machines in the
build; picking one sets the held track's sound slot 0 (with
`core_sound_set`) and its SYN knobs to the machine's defaults, or to FM's
for FM. On a machine track the SYN1 and SYN2 knobs are the machine's
(`core_param_override`): its labels and value texts, on plain knobs that
turn as the stock ones do. SYN1's and SYN2's second pages are FM's alone and
show no labels, unless the machine takes them (machines 0.2, `DNM_PAGES2`,
below). Each block (`ev_render_voices`) a voice playing a machine
gets the machine's render in place of the FM voice, then the AMP page's
envelope (ATK, DEC, SUS and REL, the voice's slots 66-69) and the note's
velocity.

A machine mod contributes to the machines mod's table `dn_machines` a
pointer to a `struct dnm_machine` (`digitone-mk1/machines.h`):

| offset | field | |
|---|---|---|
| +0 | id | 1-127: claim it as the resource `dnmachine:<id>` |
| +4 | name | its label in the submenu, 14 characters |
| +8 | icon | 16 rows of 16 pixels for its tile, or 0 |
| +12 | knobs | 16 `struct dnm_knob`: SYN1 A-H, then SYN2 A-H; 32 with `DNM_PAGES2`: then SYN1's second page A-H and SYN2's |
| +16 | render | `void f(int voice, int *block, const struct dnm_voice *v)`: the voice's 32 samples, Q1.31; `block` holds the FM voice's on entry |
| +20 | flags | 1 (`DNM_OWN_ENV`): the machine applies its own envelope; 2 (`DNM_PAGES2`, machines 0.2): it takes the second pages too |
| +24 | defaults | as many values as knobs (8.8; -1 leaves one) set when a track switches to it, or 0 |

A knob is a label (5 characters; 0 leaves the knob unused), a formatter for
its value text (0: the number) and a stock id whose graphic it borrows (0: a
plain knob). Its value is the stock parameter's, in that parameter's range:
SYN1 A ALGO 0-7, B-D the ratios (as indices), E HARM -26..26, F DTUN 0-127,
G FDBK 0-127, H MIX -63..63, and SYN2 A-H 0-127.

**The second pages (machines 0.2).** A Digitone sound has room for 79
parameters, and the only three no stock parameter uses are taken ("Sound
slots are scarce", above). A machine sound's FM parameters, though, mean
nothing while the machine plays: with `DNM_PAGES2` a machine also owns the
SYN pages' second pages, knobs 16-31, eleven more parameters, saved with the
sound and p-lockable like the rest. Their ranges are FM's, because the DSP
still renders FM with them: 16-19 SYN1 2/2 A-D the ratio offsets, 0-127.99
in 1/256 steps (64 the middle); 24 ADEL and 28 BDEL 0-127; 25 ATRG, 26 ARST,
29 BTRG and 30 BRST 0-1; 27 PHRT 0-4. Knobs 20-23 and 31 have no parameter
and stay empty. A switch away from such a machine sets them back to FM's
defaults. An older machines mod (0.1) reads 16 knobs only.

`dnm_knob(voice, k)` reads knob k (0-31) for a voice; `dnm_phase_inc(pitch)` and `dnm_sin(phase)` are a
phase step a sample at 48 kHz and a sine. A voice that peaks at `DNM_PEAK`
(0x08000000) is about as loud as an FM voice at the stock defaults. The
render runs at interrupt level for every voice playing the machine, every
block (667 us): in digikit's emulator SINE takes about 3,000 instructions a
voice a block.

`examples/dn-sine-machine` is a whole machine in C to start from: a sine at
the note that OCT shifts by octaves, FOLD folds, SUB doubles an octave down
and FINE (SYN1's second page, from 1.1) tunes by up to a semitone. It names
no firmware address, so its 1.44 port is empty:

```json
"contribute": [{"to": "dn_machines", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:sine_machine", 0]]}],
"requires": ["core", "machines"],
"resources": {"names": ["dnmachine:2"], "core": "3.1"}
```

The machines mod claims sound slot 0 as the resource `soundslot:0`; what it
does not do yet is in its README.

### Exclusive audio (core-dn1 3.2, Digitone mk1)

A mod can take the whole output: the audio inputs in, and everything the
Digitone plays out, with the synths silent. After the voices' filters and
their mix, the render (vector 191) calls its master stage (0x40096e14),
which mixes the voices and the inputs, runs chorus, delay, reverb and the
master drive, and writes the output and the USB block. In digikit's
emulator that stage takes a third of every block (31.9% of the 166,667
core cycles of a 667 us block at 250 MHz), and the voices' filters another
7.9%.

A mod contributes to the table `core_audio` a pointer to a record (`struct
core_audio_owner` in `digitone-mk1/core3.h`):

| offset | field | |
|---|---|---|
| +0 | on | nonzero: the mod has the output from the next block on; set and cleared from the UI task |
| +4 | render | `void render(int32_t *out, const int32_t *in)`: `in` is the block's input, 32 frames L,R in Q1.31 (the codec's 24 bits, left first on a Keys too); `render` writes all of `out`, 32 frames L,R in Q1.31 |
| +8 | flags | `CORE_AUDIO_MUTE_VOICES` (1): skip the voices' filters as well |

Each block, before the voices' filters, core takes the first record whose
`on` is set, in the table's order. While there is one, the master stage
does not run: core hands the owner the input, and sends what it returns to
the codec and to the USB block's main pair (the four effect buses silent,
the inputs where the master stage puts them). On a Keys it also clears the
Keys' second buffer at 0x80001000. With `CORE_AUDIO_MUTE_VOICES` the
voices' filter loop is skipped too: the second CPU still renders FM, but
nothing hears it and the main CPU does not filter it. The voice mix keeps
running (2.3% of a block): skipping it would leave a note that started
meanwhile silent after the owner lets go, until its next note. With no
record on, every block is stock. `render` runs at interrupt level inside
the render, which uses the EMAC: a mod that uses it leaves MACSR as it
found it. In the emulator, with the factory pattern playing, the stock
render takes 56.6% of a block and the render with THRU (below) owning the
output 17.9%, so an owner has about 64,000 core cycles a block at stock's
load. The emulator's tables assume zero-wait memory, so expect a unit to
run slower.

A build may hold several such mods, and their buffers can be large. The
Digitone profile has an area for them, `bulk` (0x44000000-0x47BE0000,
about 60 MB), which the OS never touches and never clears. A mod claims
its piece as a region, so the linker keeps two mods' buffers apart:

```json
"resources": {"core": "3.2",
              "regions": [{"name": "my buffers", "lo": "0x44000000", "hi": "0x44400000"}]}
```

It clears what it reads before it has written it. `fw_tempo` (3.2) is the
tempo the sequencer runs at, x 120: a mod in time with the Digitone takes
its grid from it and from `fw_timeline`.

`mods/digiswarm-dn1` (DigiSwarm) is a whole one: an effects engine for the
inputs, with eleven engines, a looper and its own page, buffers in the
bulk area, and MIDI CCs through `ev_midi_cc`; `mods/digiprism-dn1`
(DigiPrism) is another, a pedal with four reorderable modules of
effects, and DigiTide, DigiTriad, DigiReel, DigiHoard and DigiHalo
(`mods/digitide-dn1` and on) five more, sharing a pedal shell (`pedal.h`,
`pedal_dsp.inc`, `pedal_ui.inc`: the pages, SETUP, presets, TAP, BYPASS,
the CPU guard and the project data, copied into each). Each pedal's buffers
have a region of their own in the bulk area, so all seven share a build;
together they need core-dn1 4.0's RAM. `examples/dn-thru` is the smallest such mod. THRU in the Mod Menu takes
the output, and the inputs go straight to it at knob A's level; NO gives
it back. It names no firmware address, so its 1.44 port is empty:

```json
"contribute": [{"to": "core_audio", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:thru_owner", 0]]}],
"resources": {"core": "3.2"}
```

Core-dn1 3.2 owns two more sites: 0x4009e07e (`core_voices_gate`, by jmp
over the 14 bytes before the voices' filter loop) and 0x4009e146
(`core_render_master`, by jsr); in 1.44 they are 0x4009e09e and 0x4009e166.

### Insert audio (core-dn1 4.0, Digitone mk1)

An owner may instead take what the master stage made: an effect on
everything the Digitone plays. With `CORE_AUDIO_INSERT` (2) in its flags,
core runs the stock master stage as it is, with its own four arguments, and
then hands `render` what the stage wrote to the output: the voices, the
inputs as the mixer has them, chorus, delay, reverb and the master drive,
32 frames L,R in Q1.31. What `render` returns goes to the codec and to the
USB block's main pair in its place; the rest of the USB block (the effect
buses and the inputs) and a Keys' second buffer stay as the stage wrote
them. The voices always play: `CORE_AUDIO_MUTE_VOICES` is ignored with
`CORE_AUDIO_INSERT`, since the stage mixes what the voices' filters make.
The stock render keeps its own share of the block, so an insert has what
is left. In the emulator, with the factory pattern playing, the stock
render takes about 57% of a block (its master stage 53,240 of the block's
166,667 core cycles), and core's part of an insert, the conversion in and
out, about 600 cycles: an insert has a little under 40% of the block, less
what the UI task needs. TREMOLO's own render (below) takes 926 cycles.

Nothing else changes for the Digitone's own pages, so an insert may stay on
while they are on screen. `examples/dn-tremolo` is the smallest one: TREMOLO
in the Mod Menu turns a 4 Hz tremolo on everything on, and picking it again
turns it off. It takes no key and draws nothing, and like THRU it names no
firmware address, so its 1.44 port is empty:

```c
struct core_audio_owner trem_owner = { 0, trem_render, CORE_AUDIO_INSERT };
```

```json
"contribute": [{"to": "core_audio", "order": 70, "data": "00000000",
                "relocs": [[0, "abs32", "sym:trem_owner", 0]]}],
"resources": {"core": "4.0"}
```

The table's order still decides between owners: each block the first
record whose `on` is set has the output, so an exclusive mod that opens over
an insert that stays on contributes before it, with a lower `order`, and the
insert carries on once the exclusive one lets go. A mod may change its flags
from the UI task, as it may `on`: core reads them each block. A mod that
also runs on an older core-dn1 (3.2 or 3.3), as an exclusive owner there, can
tell whether the core takes inserts: 4.0 exports `core_audio_caps`, a long with
`CORE_AUDIO_INSERT` set, and a mod that lists it under `weak` in mod.json
reads 0 with an older core (the linker's `core_zero`). 4.0 owns no new
site: `core_render_master` (0x4009e146) runs the stage first for an
insert.

The addresses in these sections are OS 1.43's. In 1.44 the same
routines are there, unchanged but moved, and RAM is 0x1000 further on:
`mods/core-dn1/mod.json`'s `ports` has every one of core-dn1's sites for it.

The Digitakt mk1's core 2.1 owns these sites too (`mods/core/mod.json` says what each is;
in 1.54 they are at the same addresses, but 0x400a1706 is 0x400a1862):
0x40011322, 0x400225f0, 0x40022fe6, 0x40028f7e, 0x40029e9c, 0x4002a4e0,
0x4002a76e, 0x4002a9e8, 0x4002aee0, 0x4002ba7e, 0x40039dac, 0x40039dcc,
0x40039e80, 0x40039e86, 0x4003a43e, 0x4003afe4, 0x4003afee, 0x4003b448,
0x4003b452, 0x4003b49e, 0x4003b4ac, 0x4003b6d4, 0x4003b6de, 0x40077272,
0x40078f72, 0x40079124, 0x40079144, 0x400791d6, 0x40079240, 0x4007a2ea,
0x400a1706.

### Program changes (core-dn1 4.0, Digitone mk1)

`ev_midi_pc` gives a mod each program change the unit receives, on any
channel, before the stock applies it: a pedal can recall its presets, a
sequencer mod its scenes. The firmware's program change handler
(0x400ea19c) first tests MIDI CONFIG's "receive program changes", and with
it on takes the change that comes on PROG CH IN's channel (or the auto
channel's, when PROG CH IN is AUTO) and has the UI task change the pattern.
Core takes that test (0x400ea1a8, `tst.l` of the setting; 0x400ea41c in
1.44): each handler sees the change first, with `CORE_MIDI_PC_STOCK` in its
flags when the stock would apply it, and one that returns nonzero takes it,
so the pattern stays. With none taking it, core makes the same test and the
stock goes on. A mod that answers only while its page is open should return
0 when it is closed, so the unit's own program changes keep working.

`examples/dn-progchange` (PROG) is the smallest such mod: it shows each
program change in the screen's corner for two seconds, and takes the ones
on MIDI channel 16. Its handler:

```c
int32_t prog_pc(int32_t channel, int32_t program, int32_t flags)
{
    prog.channel = channel;
    prog.program = program;
    prog.flags = flags;
    prog.count++;
    prog.show = 60;
    if (channel != TAKE_CHANNEL)
        return 0;
    prog.taken++;
    return 1;
}
```

```json
"subscribe": [{"event": "ev_midi_pc", "fn": "prog_pc", "order": 50}, ...],
"resources": {"core": "4.0"}
```

### The note queue (core-dn1 4.1, Digitone mk1)

**`ev_note_queue`.** The sequencer fires a step about one step before it
plays: the fire builds the step's event (its note, or none for a lock trig,
and the step's p-locks) and queues it for the time the step plays,
microtiming and swing in. At that time the render starts the event's voices
with its locks, or puts a lock trig's locks into the track's voices. Every
note source queues its events through one routine (0x400fff04; 0x40100178 in
1.44): the sequencer's fires, a trig's or the keyboard's note, MIDI in, the
arpeggiator. Core takes its entry (by `jmp`, its first two instructions run
after the handlers) and calls each handler first, `void f(void *event,
uint32_t time)`, with the interrupts masked, as the routine then masks them:
`time` is on the timeline (`fw_timeline`'s units), and the event is 0x48
bytes (`core3.h`'s `FW_EVENT_*` macros):

| offset | | |
|---|---|---|
| +0 | kind | 0 a step's fire (its trig: a note or a lock trig), 1 the first step's, made as PLAY starts; 5 and 6 the arpeggiator's and the sequencer's own, not trigs |
| +8 | track | 0-3 the synth tracks |
| +0x28 | a fire's step word | bit 0 a note trig, bit 1 a lock trig |
| +0x38 | its p-lock list, or 0 | +8 the count, then from +0x14 `{u16 slot, s16 value}` 8 bytes apart |

The first step's event as PLAY starts (kind 1) has no p-lock list, and a
lock trig on that step makes no event then: the stock applies that step
another way, so a mod reads its locks from the lock table when the
transport starts. A mod that plays a track's locks itself (an effect's
p-locks, kept in a track's lock table: fxkit, "Effects" below) takes the
step's locks here and applies
them when `fw_timeline` reaches `time`: the step's own time, to within a
block. Interrupt level or
a task: keep it short, only read the event, and call no firmware.

Core-dn1 4.1 owns one more site, 0x400fff04 (`core_note_queue`, by jmp). It
is 4.0 and this, and `tests/test_sdk.py` checks it against the released 4.0.

### The mods' RAM (core-dn1 4.0, Digitone mk1; core 4.0, Digitakt mk1)

The linker lays every mod's `.run`, the tables and every `.bss` out in one
area, and core's boot copier puts them there before the OS starts. On the
Digitone it was the Digitakt mk1's, 0x47BE0000-0x47C00000, 128 KB: a few
effects mods of 25-40 KB each filled it. From core-dn1 4.0 the area is
0x43EC0000-0x44000000, 1.25 MB, between the OS's highest buffer and the
`bulk` area. The profile says so (`core_ddr`: from a core's version on, the
area in place of `ddr`), so a build with an older core keeps 128 KB, and
mods need no change: the linker relocates them wherever they go. A mod
that needs more than 128 KB on its own says `"core": "4.0"`. Mods that are
over 128 KB only together need say nothing: the app, the web page and the
kit take 4.0, the newest core, for every selection ([CORES.md](CORES.md),
"How a build picks one").

Why there: the OS's instruction operands in DDR end at a 384 KB ring,
0x43E472C0-0x43EA72C0, whose end it compares at 0x43EA72C8 and
0x43EA72C9 (0x4008d660-0x4008d724 in 1.43). Nothing of the OS's points
above it, in 1.43 or 1.44, and the area starts 64 KB further on. The mods'
code and tables travel in the OS file (their `.bss` does not), so the file
grows with them: the patcher refuses a build the bootloader could not
unpack in place, or one past the flash. With core-dn1 4.0, five effects
pedals, DigiSwarm and DigiPrism in one build take 243 KB of it, and the
file still unpacks in place with 427 KB to spare.

The Digitakt mk1 has less to give: its 64 MB sample pool and the 4 MB
audio input ring above it fill the DDR the OS does not use for itself. Its
core 4.0 (core 3.0's code, with no change but the version) lays the mods
out from 0x47BC0000 to 0x47C00000, 256 KB in place of 128 KB: the whole
gap between the pool's end (0x47BAF640 in 1.53, 0x47BBF640 in 1.54, which
the OS compares against) and the ring at 0x47C00000. No instruction operand
points into it, and in an emulator run nothing writes there. As on the
Digitone, a build takes 4.0, the newest core, for every selection.

### The master output and the master pages (core 4.1, Digitakt mk1)

Mods that work on the whole mix (a master EQ, a master effect suite) used to
patch the same places: the instruction that hands the master pair to the
outputs (0x400721e6 in 1.53 and 1.54) and the master view's draw, keys,
knobs and title in its vtables. Only one of them could be in a build. Core
4.1 owns those places once and offers them to any number of mods:

- **`ev_render_master`**: `void f(int32_t *pair)`, once a block in the
  render (interrupt level), with the block's master pair, 32 frames of
  {L, R} 32-bit samples, before it goes to the main outputs, the headphones
  and the 12-channel bus the USB stream is built from. A handler changes it
  in place, and the scope, spectrum and tuner show the change. Handlers run
  by their `order`, each on what the one before left. The EMAC is the
  stage's: MACSR is 0xA0 (fractional, saturating), and its loop right
  after the event accumulates into ACC0 and ACC1 from 0. A handler may use
  the EMAC: core puts MACSR back and clears ACC0 and ACC1 after the
  handlers. With none, core runs four instructions more than the stock
  (the call, a test, a branch and the return).
- **`core_mpages`**: pages of the master view (FUNC+LFO steps through
  them). Contribute a pointer to a `struct core_mpage` (`core3.h`): the stock
  page it follows (11 COMP, 12 internal mixer, 13 external mixer; any other
  value puts it last), its short name and title, and `draw`, `key`, `enc`
  and `clear` (any may be 0). Core puts the page in the view's list, as kind 0
  (the firmware's "None" page, which has no knobs), and while it is shown
  gives the title bar and the pop-up its names ("Master EQ (2/4)") and
  calls `draw(view, bmp)` after the stock draw (the title bar, and below
  it kind 0's eight empty knob boxes, which the page clears where it
  draws: `fw_fillrect(bmp, 0, 0, 127, 52, 0)` clears them all),
  `key(view, event)` and
  `enc(view, event)` before the stock handlers: nonzero takes the event,
  and `clear(view)` when the page is cleared (FUNC + a held LFO + PLAY,
  as on the stock pages), after the stock clear has shown its pop-up.
  Pages that follow the same stock page keep their `order`. A mod that
  reads keys or knobs itself (`ev_key`, `ev_enc`) asks
  `core_mpage_shown(view)` which page is up; `core_mview` is the master
  view and `core_mview_draws` counts its draws, so a tick that sees the
  count move knows the view is on the screen. `fw_invalidate(view)` has a
  page drawn again after a change.
- **`ev_fxmix`**: `void f(void *menu)` when SETTINGS > GLOBAL FX/MIX is
  built, after its last row: add rows with `core_additem(menu, row)`, as
  `ev_settings` handlers do.

The descriptor lives in `.data` (core writes its last two words, the view
and the page's place in its list). The smallest mod that uses all of it is
[examples/master-mono](../examples/master-mono): a page after the internal
mixer whose knob A sends the outputs stereo, mono or swapped. Digi EQ and
Tone+FX are built on it, and combine.

### Key LEDs and each block's sound parameters (core-dn1 3.3, Digitone mk1)

Two events for mods that perform over the stock pages rather than replace
them.

**`ev_key_leds`.** Each UI frame clears every key LED and then lets the
visible views set theirs (0x400e5d22, 0x400e5f96 in 1.44, called at
0x40018332 in both); the first setter of an LED wins. Core calls the
handlers, `void f(void *mgr)` with the LedManager, before that call, so a
handler's LED stays over the stock view's while the stock page shows the
rest: a mod's state on the trig keys, the track keys or the page keys.

**`ev_render_params`.** Each audio block the render (vector 191) writes the
sound parameter words from the smoother, which glides each toward its
target (`fw_voice_params`), then calls its controller stage (0x4009c058,
0x4009c078 in 1.44; called at 0x4009db80, 0x4009dba0), where the mod wheel,
breath, aftertouch, the LFOs, pitch, pan and gain ride on them; the second
CPU renders them. Core calls the handlers, `void f(int16_t *words)`, before
the controller stage: voice v's slot k is `words[79 * v + k]` (8.8, the
layout of `fw_voice_params` past its 18 bytes), the global FX's (chorus,
delay, reverb, master) are record 8. A change lasts this block: the
smoother writes the words again for the next, so nothing builds up and the
kit and the targets are untouched; to hold a value, write it every block.
The words of a block are rendered and filtered in the next two, so
`ev_render_voices` hears a change two blocks on. Interrupt level, inside
the render: a handler that uses the EMAC keeps MACSR as it found it.
`fw_voice_track[v]` is voice v's track. A mod that writes the targets
instead (`fw_voice_params`) changes what the smoother glides to, and the
glide with it.

With no handler each site costs a test and a jump: the stock call runs as
it was, by a tail jump with the stack as it came.

### Effects (fxkit, Digitone mk1)

An effect on the Digitone mk1 (a pedal on the audio inputs, say) is a whole
mod: its own Mod Menu entry opens its page, and while it is active it owns
the audio (`core_audio`, exclusive, the synths muted). The `fxkit` mod
(`mods/fxkit-dn1`, needs core-dn1 4.1) gives every such effect in the build
what it would otherwise each build itself: a mixer around it, two LFOs on its
knobs, p-locks that follow the pattern, and their pages. One effect is the
active one at a time; opening another's entry hands it the audio.

- **The audio.** fxkit owns the output for the active effect: the inputs
  through the mixer's input stage (IN L, IN R: their levels, on the stock
  mixer's law for its input levels, 16 (v / 127)², so 32 is unity and 127
  +24 dB; L PAN, R PAN: where each sits), the effect's `render`, then MIX
  (the dry against the effect, for an effect that leaves the dry to fxkit),
  WDTH, BAL and VOL (100 unity). A CPU guard (DMA timer 0) bypasses the effect when the render
  has no time left (`fx_overload`). An effect that plays as an insert of its
  own (DigiPrism with SOURCE DIGITONE) turns fxkit's owner off for itself
  (`fx_chain(e, 0)`) and keeps its values.
- **Its values**, each block at the render's entry, the effect's `values(val,
  flags)`: each knob by its slot (8.8), its own value, or the p-lock of the
  step that plays, with the LFOs on them; `FX_V_BYPASS` when BYP or the guard
  bypassed it (an effect with `FX_OWN_BYPASS` gets the call all the same, for
  its tails).
- **The LFOs** are the stock LFO page's eight controls (SPD, MULT, FADE,
  DEST, WAVE, SPH, MODE, DEP), run with the stock LFO's arithmetic: its
  phase step `tempo × (SPD − 64.0) × 2 >> (11 − MULT)` (14400 for the tempo
  from MULT 12, the free rates), a turn of 0x5265c000, the stock waves,
  fades and modes, a trig of the lock track as their TRIG. DEST lists only
  the effect's knobs and the mixer's, by their names.
- **The p-locks are the pattern's.** fxkit keeps them in one synth track's
  lock table (the lock track: T4 unless the effect's SETUP says otherwise,
  `fx_lock_track_set`), as the firmware's lock setter (0x40024236) does: the
  value at `fw_pattern + 0x1e80 + 10319 × track + 160 × step + 2 × slot`, the
  row's count of locks at +0x9e (as 0x40023ed2 counts them), the slot's used
  byte at +0x2800 (0x400241f6), and a lock trig on a step with no trig. So
  the stock saves, loads, copies and pastes them with the pattern, and the
  sequencer fires them: `ev_note_queue` hands fxkit each trig of the lock
  track with its time, and its locks hold until the track's next trig (the
  first step's, as PLAY starts, fxkit reads from the lock table). The lock
  track should play nothing of its own in a pattern where the effect has
  locks: its notes would take them too, and an arpeggiator on it turns its
  trigs into the arpeggiator's events (the default project's T4 has one).
- **LOCK mode.** `fx_lock_key` gives an effect's page the stock's way of
  locking: the trig keys are the steps of a 16-step page (PAGE the next,
  as many as the pattern has), a trig held and a knob turned
  (`fx_turned`) is that step's lock, FUNC + a trig clears the step's locks
  of the effect's, and while a trig is held `fx_view` shows its locks.
  `fx_lock_strip` draws the page's 16 steps for a title bar (the LEDs are the
  stock's).
- **The pages.** `fx_page_draw`, `fx_page_turn` and `fx_page_push` are the
  MIXER (page 0) and the LFOs (1, 2) as the stock FLTR, AMP and LFO pages are
  laid out: two rows of four controls over their labels; `fx_draw_controls`,
  `fx_knob_turn` and `fx_knob_push` the same for eight slots of the effect's
  own. The LFO pages are the stock LFO page: its own knob graphics (fxkit
  calls the stock's drawers from the parameters' UI records, `fw_uirecs` by
  `fw_slot_ids`), WAVE and SPH as one picture of the wave from its start
  phase, DEST's box and its list (a turn opens it, YES picks with
  "Confirmed!" in the title, NO closes it), SPD and DEP in the stock's fine
  steps, and the stock's defaults. On the MIXER and LFO pages a turned
  knob's value shows under it for a while, in the stock's text, as on the
  stock pages. While one of them is on screen an effect gives its keys to
  `fx_page_key` first (DEST's list takes YES, NO, UP and DOWN), adds
  `fx_page_state()` to what makes it draw again, and shows an LFO page's
  title as it is ("LFO (1/2)", as the stock's). The effect draws its title
  bar and its left column.
- **Saving.** `fx_pack` and `fx_unpack` put an effect's mixer and LFOs in
  `FX_PACK` (23) bytes of its own project data, last in its block (a block
  saved with fewer loads with the rest zero, which `fx_unpack` reads as the
  format before); pack again when `fx_serial(e)` (a count of the changes to
  its values) moves, not every tick: the UI task's time is the firmware's
  too. fxkit's own block (`FXKT`) holds the lock track. The locks are the
  pattern's.

An effect mod contributes to the table `fx_effects` a pointer to a `struct
fx_effect` (`digitone-mk1/fxkit.h`): its name, its knobs (each a slot, a
kind: `FX_KNOB` 0-127, `FX_BIP` -64..+63 or `FX_OPT` with its options'
texts, a default, a label and a long name), flags (`FX_OWN_MIX`: it mixes
the dry itself; `FX_OWN_BYPASS`: it bypasses itself, with its tails) and
three calls: `init()` once, before fxkit reads its knobs, `values()` and
`render(out, in)`, 32 frames L,R in Q1.31, at interrupt level. Its knobs'
slots: page 1 17-24 (`FX_S_P1`), page 2 27-34 (`FX_S_P2`), settings
50-53, 35-37, 25 and 38-40 (`FX_S_SET(0)`-`FX_S_SET(10)`), 54-64; the
mixer's are 66-76 and the LFOs' 1-16. From the UI task the effect calls
`fx_activate` from its entry's `open` and `fx_deactivate` on NO; `fx_get`
and `fx_set` reach its own values, and `fx_set_later` does from the MIDI
task. `examples/dn-fx-gain` (GAIN) is the smallest: a level, a tone and the
right side's phase, with its page (SYN1 its knobs, AMP the MIXER, LFO the
LFOs, RECORD the LOCK mode, MIDI the stock UI with GAIN playing, NO off). It
names no firmware address, so its 1.44 port is empty:

```c
static const struct fx_param gain_params[] = {
    { FX_S_P1, FX_KNOB, 0, 100, "GAIN", "Gain", 0 },
    { FX_S_P1 + 1, FX_KNOB, 0, 127, "TONE", "Tone", 0 },
    { FX_S_P1 + 2, FX_OPT, 2, 0, "PHAS", "Phase R", gain_phase },
    { 0 }
};
const struct fx_effect gain_effect = {
    FX_ABI, "GAIN", gain_params, 0, 0, gain_values, gain_render,
};
```

```json
"contribute": [{"to": "core_menu", "order": 39, "data": "00000000",
                "relocs": [[0, "abs32", "sym:gain_menu", 0]]},
               {"to": "fx_effects", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:gain_effect", 0]]}],
"requires": ["core", "fxkit"],
"resources": {"core": "4.1"}
```

DigiSwarm, DigiPrism and the five pedals (DigiTide, DigiTriad, DigiReel,
DigiHoard, DigiHalo) are effects on fxkit from their 0.2, with the same keys
as GAIN: SYN1, SYN2 and FLTR their three pages, AMP the MIXER, LFO the LFOs,
RECORD the LOCK mode, MIDI the stock UI with the effect playing, NO off; their
SETUP has the LOCK TRACK. The pedals share theirs through the pedal shell
(`pedal_ui.inc`); DigiSwarm's engine, Hold and Reverse lock too, and
DigiPrism's modules and their order. DigiPrism's SOURCE DIGITONE stays an
insert of its own (`fx_chain(e, 0)`), with fxkit's values all the same.

### Step-fire events (seq-events, Digitone mk1, Digitakt mk1 and Octatrack)

Mods that change what the sequencer plays (rhythm and melody generators,
performance effects) need the same instructions of the step fire, and two
mods may not patch one site. The `seq-events` mod (`mods/seq-events-dn1` on
the Digitone mk1, `mods/seq-events-dt1` on the Digitakt mk1,
`mods/seq-events-ot` on the Octatrack) owns the fire's sites, audio and MIDI
tracks, once, and offers each stage of it as tables mods subscribe to. The
names are one set for every device seq-events serves (`seq-events.h`); a mod
includes its device's header, `digitone-mk1/seq.h`, `digitakt-mk1/seq.h` or
`octatrack/seq.h`, which gives the sizes and includes it. Every handler runs
at interrupt level, in the fire (the Octatrack's: the sequencer's tick): keep
it short and call no firmware. `track` counts the audio tracks first
(`SQ_AUDIO`: the Digitone's 0-3, its synth tracks; the Digitakt's and the
Octatrack's 0-7), then the MIDI tracks (`SQ_TRACKS` in all: 4-7; 8-15).

| | Digitone mk1 | Digitakt mk1 | Octatrack |
|---|---|---|---|
| `SQ_AUDIO`, `SQ_TRACKS` | 4, 8 | 8, 16 | 8, 16 |
| `SQ_NOTES` | 8 | 4; an audio track plays `notes[0]` only | 4, the MIDI tracks' NOTE and NOT2-NOT4; no notes on the audio tracks |
| `SQ_SLOTS` | 79 | 53 | 30: the page-1 knobs, in order (AMP VOL is 15); the byte is the value >> 8 |
| `SQ_TRIG`, `SQ_LOCK` | the step word's bits 0 and 1 | bit 0 with the SYN trig on, and without (FUNC + trig's lock trig) | a trigger or one-shot trig; a trigless or lock trig |
| velocity | every track | every track | the MIDI tracks' VEL |
| the key (`sq_track_info`) | the keyboard's ROOT and SCALE | none: `SQ_ROOT_NONE`, `SQ_SCALE_NONE` | none |
| when it runs | at the step | at the step | a step ahead of the sound |
| a limit | | a step word with 0x0004 set never fires, whatever a handler answers | a trig a handler adds does not end a slide |

| event | prototype | |
|---|---|---|
| `ev_fire_step` | `int32_t f(int32_t track, int32_t step, int32_t len)` | the step the fire reads: return it; one outside 0..len-1 is ignored |
| `ev_gen_trig` | `void f(int32_t track, int32_t step, int32_t first, uint32_t *trig)` | does the step fire, `*trig` `SQ_TRIG` (note trig) and `SQ_LOCK` (lock trig), the stored step's to start with |
| `ev_fire_trig` | the same | every track, after every `ev_gen_trig` handler |
| `ev_fire_cond` | `void f(int32_t track, int32_t step, int32_t *pass)` | a trig condition's or probability's outcome: 1, 0, or -1 the probability decides |
| `ev_gen_notes` | `void f(int32_t track, int32_t step, int32_t generated, int8_t notes[SQ_NOTES])` | the notes a fired note trig plays; `notes[0]` the note (-1 none), the rest intervals added to it (a chord; `SQ_NOTES` 8 on the Digitone); the stored notes to start with, or the track's NOTE for a `generated` trig (it fires with no stored note trig) |
| `ev_fire_notes` | `void f(int32_t track, int32_t step, int8_t notes[SQ_NOTES])` | every track, after every `ev_gen_notes` handler |
| `ev_fire_vel` | `void f(int32_t track, int32_t step, int32_t *vel)` | the velocity, 0-127 |
| `ev_fire_locks` | `int32_t f(int32_t track, int32_t step, struct sq_lock *out, int32_t room)` | audio tracks: write up to `room` parameter locks (a sound slot below `SQ_SLOTS`, 79 on the Digitone, and its 8.8 value) and return how many |

A stage with two phases runs every generator (`ev_gen_`: what the step is)
before every effect (`ev_fire_`: what is done to it), whatever their
`order`, which sorts handlers within a phase. Each handler changes in place
what the one before left, the first what the stock step made, and
seq-events checks the value after each (bits masked, the step's range, the
velocity clamped, lock slots). `first` is 1 at the trig test's read of the
step, 0 at the fire's second read: both reads must get the same answer.

Beside the events, seq-events keeps a step clock per track (`sq_clock`: the
last real step the fire loaded, and a parity that flips on each wrap), reads
a track's facts when asked, from the pattern a fire reads (`sq_track_info`:
its length, the master length, NOTE, the keyboard's ROOT and SCALE;
`sq_stored`: its stored note trigs, UI task), and re-arms a stopped
sequencer for tracks a mod changed (`sq_rearm`, UI task). A device that
keeps no key for a track gives `root` `SQ_ROOT_NONE` (0xff) and `scale`
`SQ_SCALE_NONE` (-2; -1 is chromatic): a mod that needs a key checks
`root`. The Digitone never gives them; the Digitakt always does.

`examples/dn-euclid` (Digitone), `examples/euclid` (Digitakt) and
`examples/euclid-ot` (Octatrack) are a whole one: track 1 fires 5 steps of every 16,
the same C with the device's header.
It names no firmware address, so its port (1.44, 1.54) is empty:

```json
"subscribe": [{"event": "ev_gen_trig", "fn": "euclid_trig", "order": 50}],
"requires": ["core", "seq-events"]
```

**On the Octatrack** (`examples/euclid-ot`, the same C) the sequencer does not
read the pattern when a step fires: its tick, an interrupt, builds each
track's next step into a record at the start of the step before it, from the
pattern's masks, a timing halfword and 32 lock bytes. The events run in that
build, so about one step ahead of the sound, and seq-events lets the stock
builder do the rest: it writes what the handlers changed into the pattern for
the one build and puts it back when the builder returns. What that means for a
handler:

- `first` is 1 for a build, and 0 for a repeat of an edit's build (the first
  step is built twice when PLAY is pressed); an edit of a step is built again
  with the events, playing or not.
- `sq_clock` follows the tick's own builds, a step ahead of the sound.
- `ev_fire_cond` gets the stock condition's answer, 1 or 0 (the Octatrack
  folds its probabilities into its conditions: never -1), and is asked only
  of a step that fires.
- A step with nothing stored that a handler makes fire has no stored locks, no
  condition and no micro-timing.
- `ev_gen_notes`, `ev_fire_notes` and `ev_fire_vel` run on the MIDI tracks
  (NOTE, NOT2-NOT4, VEL as lock bytes, or the Part's values where there are
  none); an audio track plays a sample, and its raw PTCH is not a note. A lock
  on its AMP VOL (slot 15) is its accent. `ev_fire_locks` runs on the audio
  tracks only, as everywhere.
- `ev_step_built`, the Octatrack's own, is the record the builder has staged,
  for a mod that edits it where it would otherwise hook the builder (below).
- Not offered: the staging-to-pending copies and the frame interrupt's trig
  record after the builder, the sequencer's MIDI packets, pattern and Part
  changes. [mods/seq-events-ot](../mods/seq-events-ot/README.md) has the sites
  and what was checked. The voice-time dispatch is
  [trig-events](#trig-events-octatrack), below.

**`ev_step_built`** (Octatrack only; `octatrack/seq.h`):

| event | prototype | |
|---|---|---|
| `ev_step_built` | `void f(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n, const struct sq_built *rec)` | after the builder has staged a build's record and the stages have run, with the pattern as stored: `rec` points at the words the tick copies out next |

The tick builds each track's next step with `builder(track, bank, pattern,
step, n)` and copies what the builder staged into the pending slot `n` (0-2; -1
for the first step at PLAY and for an edit of a step). `step` is the step the
build read. The event runs for every build of every track, also with no step stage
subscribed, and a change in place is a change in the record:

| `rec->` | |
|---|---|
| `kind`, `recorder` | always current: 0 nothing staged (for `recorder`, no recorder trig). Set to 0 and the step plays nothing, or starts no recording. `kind` is `0x1d` a trigger trig, `0x19` trigless, `0x3d` one-shot, 1 a lock trig |
| `locks` | the 32 lock bytes (`0xff` none): an audio track's page-1 knobs, a MIDI track's NOTE page first |
| `deltas` | audio tracks: each lock's difference to the next trig's, for a slide; null on a MIDI track |
| `length`, `span`, `time` | the length index, the trig's length and its time offset (micro-timing and swing) |
| `rec_time` | audio tracks: the recorder trig's time offset |

The other words are the last staged build's when `*kind` is 0 (`*rec_time` when
`*recorder` is 0): read them when `*kind` is not 0. The handler runs at interrupt
level, or in the UI task for an edit's build, which the tick's build of the same
track can interrupt; check `*kind` before an edit and keep it short. A handler
cannot give a build a record it did not stage.

```c
/* REC TRIG MUTE: T1-T4's recorder trigs are not staged */
void mute_built(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n,
                const struct sq_built *rec)
{
    if (track < 4 && rec->recorder)
        *rec->recorder = 0;
}
```

with `{"event": "ev_step_built", "fn": "mute_built", "order": 50}` in `subscribe`.
Three of octabam's modules hook the builders to do this: REC TRIG MUTE
(`0x4009d9a4`), the shop's MIDI SCENES v1.0 (`0x4009d1de`), and the builder
half of PLOCKS P2 (`0x4009d704`, `0x4009d8c6`); the mod's README says what
each needs of the event and what it still needs elsewhere.

### Trig events (Octatrack)

A trig that reaches the Octatrack's sound is handed to the track's machine
by the frame interrupt, once for every audio track that has one. The
`trig-events` mod (`mods/trig-events-ot`, a companion of core-ot) owns that
instruction and offers it as `ev_track_trig(track)`, the moment a trig
starts, where [seq-events](#step-fire-events-seq-events-digitone-mk1-digitakt-mk1-and-octatrack)
runs a step ahead, when the sequencer builds it. A mod that mutes a track's
trigs, keys a sidechain from them or counts them subscribes to it.

| event | prototype | |
|---|---|---|
| `ev_track_trig` | `int32_t f(int32_t track)` | `track` 0-7 (T1-T8) is about to have its trig started by its machine. Return nonzero to veto: the machine is not called, nothing after the handler sees the trig, and the interrupt goes on as for a machine that did nothing |

```json
"subscribe": [{"event": "ev_track_trig", "fn": "my_trig", "order": 50}],
"requires": ["core", "trig-events"]
```

```c
#include "octatrack/trig.h"

int32_t my_trig(int32_t track)
{
    return track == 2 && my_muted;      /* T3's trigs never start */
}
```

- It runs for a trigger trig and an audio track's one-shot trig; a trigless
  or lock trig sets no trig bit and never comes. The MIDI tracks do not.
- It is at interrupt level 5 inside the frame interrupt's loop over the
  tracks: a few dozen instructions, no firmware call, nothing that waits.
  Leave the EMAC (MACSR and the accumulators) as you found it, because the
  machine's function that follows counts on the mode the interrupt set.
- Handlers run in `order`, and the first to answer nonzero ends the walk:
  an observer that must see the trigs another handler vetoes takes a smaller
  `order`. A vetoed voice's trig is gone, a voice already sounding rings on
  under its own envelope; the sequencer and everything the interrupt does
  after the call are as for any trig.
- With no handler it costs four instructions at each trig; the site is one
  `jmp` over three instructions, the last pushes of the call's arguments
  (`0x4000d496`), and it overlaps octabam's MUTE MODES hook (`0x4000d498`), so
  the two cannot be built together: MUTE MODES, converted with `--bus`, is a
  handler. [mods/trig-events-ot](../mods/trig-events-ot/README.md) has the
  sites around it and what was checked.

## 3. Path B: a linkable mod

**3.1 Start from the template.** Copy `examples/hello-marker/` to a folder
of your own. It is a complete mod: a C handler on `ev_draw` that draws a
small square in the top-right corner of every screen. For the Digitakt II,
`examples/hello-marker-dt2/` builds the same source, and
`examples/perform-direct/` is a real mod in one key handler. For the
Digitone II, `examples/hello-marker-dn2/` builds the same source, and
`examples/hooks-check-dn2/` subscribes to its core's six UI events. For the
Analog Rytm MKII, `examples/hello-marker-ar2/` builds the same source for
1.73 and 1.74, and `examples/hooks-check-ar2/` subscribes to every event its
core has.

**3.2 Fill in `mod.json`.**

```json
{
 "id": "my-mod", "version": "1.0",
 "title": "My mod", "category": "Utilities", "author": "you",
 "license": "GPL-2.0-or-later",
 "description": "One or two sentences a user reads in the loader.",
 "device": "digitakt-mk1", "os": "1.53",
 "sources": ["my.c"],
 "subscribe": [{"event": "ev_draw", "fn": "my_draw", "order": 60}],
 "requires": ["core"],
 "resources": {"names": ["settings:MY ROW"]}
}
```

| field | |
|---|---|
| `id` | lowercase, unique; also the prefix of your global symbols |
| `license` | an SPDX identifier. The example uses `GPL-2.0-or-later`, the loader's own licence. Put the same `SPDX-License-Identifier` line at the top of each source |
| `sources` | `.c` and `.s` files, compiled/assembled with the device's flags |
| `subscribe` | `{"event", "fn", "order"}`: `fn` is one of your global functions |
| `sites` | patches to the stock image: `{"addr", "stock", "op", "target" or "new"}` (below) |
| `collections`, `contribute` | tables you declare, and entries you add to others' ([FORMAT.md](FORMAT.md)) |
| `weak` | names you import that may be missing; they then read as zero |
| `dsp`, `subscribe_dsp` | DSP code, on a device whose OS runs some (below, "DSP code") |
| `resources`, `requires`, `conflicts` | rules 9 and 11 |
| `ports` | the same mod for other OS versions of the device: per OS, the keys that differ there (3.10) |

**3.3 Write the code.** Call firmware routines through function pointers at
their addresses, as `hello.c` does. There is no C library. Keep interrupt-level
handlers (`ev_render_*`) short and free of firmware calls that might block.

**3.4 Hook a site nobody owns (only if no event fits).** First ask whether
another mod could want the same site. If one could, build it in the core's
shape, so that it can become a core event:
[CORE-CONTRIBUTING.md](CORE-CONTRIBUTING.md). Then add a `sites` entry.
`stock` is the hex of the whole stock instruction(s) at `addr`; `op` says
what replaces them:

| op | new bytes |
|---|---|
| `jsr` / `jmp` | `jsr`/`jmp` to `target`, nop-padded to the stock length (6 bytes or more) |
| `keep2` | the stock opcode word, then `target`'s address (a `jsr.l`/`lea.l` you redirect) |
| `ptr` | `target`'s address (4 bytes of data, e.g. a vtable entry) |
| `bytes` | `new`, in hex; add `"kind": "code"` if they are instructions; `"relocs"`, as `[offset, "abs32", "sym:NAME", addend]`, put your own symbols' addresses in them |
| `dsp_jsr` | `jsr >target` over one two-word DSP instruction (6 bytes) in a DSP payload |

A site with a `target` may also give `"addend": N`: the address used is the
target's plus N bytes (N words for `dsp_jsr`).

The routine a `jsr` site calls must do the displaced instruction's work
and keep every register the surrounding code relies on.

**Code at a fixed address.** This is only possible where the device lists
free space inside its image (`image_free`: on the Octatrack, the zero runs
octabam measured):

```json
"fixed": [{"source": "page.s", "addr": "0x400d24e0", "symbol": "page"}]
```

This assembles `page.s` for that address.
- It must fit inside one of those runs, and it may only have `.text`.
- It becomes a site over the zeros there, and its labels become absolute
  symbols, so other code and sites can name them. A mod built this way
  loads in any elekloader that loads format 2.
- Use it when other code must find the code at a known address. Otherwise
  put code in `.run`.

**DSP code (the Octatrack).** The Octatrack's main OS uploads code to its
two DSP56300 cores at boot (payload A runs on core 0, B on core 1). A mod
can carry DSP code too. It is placed in P memory that another mod frees:
the profile lists those areas (`dsp_areas`), and the mod that frees one
claims its name, for example `dsp:harvest:SPATIALIZER` (payload A's 261
words at P:0xaa8, the SPATIALIZER effect's code). Without that mod, yours is
refused.

```json
 "dsp": [{"source": "inject.asm", "payload": "A"}],
 "subscribe_dsp": [{"event": "ev_dsp_rx", "fn": "inject", "order": 20}]
```

- The SDK assembles each source with octabam's `dsp_asm` (its
  `vendor/dsp56300` build). Point `ELEKLOADER_DSP_ASM` at it, or put it on
  the `PATH`. The syntax is dsp_asm's: one instruction a line, `;`
  comments, `label:`.
- Your labels become symbols of the mod, at DSP word addresses, so
  `subscribe_dsp`, sites and other mods can name them.
- The SDK assembles each source at two origins. A word that moves with the
  origin holds an address in your code, so it becomes a relocation, and the
  linker writes the address where the code lands. A short or packed address
  (one that is not a whole word) cannot be moved, and the build refuses it.
  Branches (`bra`, `bsr`, `bcc`) are relative and need nothing.
- `subscribe_dsp` adds `jsr >fn` (plus `addend` words) to a DSP table, in
  `order`. A DSP table is declared as `{"entry": 6, "space": "dsp.A",
  "head": hex, "end": hex}` under `collections`: its words are the head,
  the entries, then the end (FORMAT.md, "DSP code").
- `dsp_jsr` hooks one two-word instruction of a payload: its `stock` is
  those 6 bytes (24-bit little-endian words, as the payload holds them).
  What it calls must do the displaced instruction's work.
- dsp_asm puts each label's address into the text wherever the label's
  name appears, so no label may be part of a longer word (`tone` beside
  `tone_end`). The SDK refuses that.
- Test DSP code under a DSP emulator before any hardware: octabam's
  `ot_emu --dsp` runs both cores. `examples/dsp-tone-ot` ran there: with
  `--dsp-peek "0:X:8100,576"`, input A holds its ramp in every RX block.

**3.5 Build it.**

```bash
python -m elekloader.sdk.build path/to/my-mod --stock Digitakt_OS1.53.syx
```

Expect `BUILT path/to/my-mod/out/my-mod-1.0.elemod` and a line with its
section sizes and imports. `BUILD FAILED: ...` names the problem.

**3.6 Check it, alone and with what it needs.**

```bash
python -m elekloader.lint my-mod-1.0.elemod
python -m elekloader.lint my-mod-1.0.elemod --stock Digitakt_OS1.53.syx --with core-2.1.elemod
```

Expect `OK: ... links as core ... > my-mod ...` and exit status 0.
Add `--with` for every mod users are likely to combine yours with, and
`--json` for machine-readable output.

**3.7 Build a firmware with it.**

```bash
python -m elekloader.patch --stock Digitakt_OS1.53.syx --mod core-2.1.elemod \
    --mod my-mod-1.0.elemod --out test.syx --version 2.0t
```

Expect `verified: ...` and `WROTE test.syx`. The file has passed every check
in [BUILDING.md](BUILDING.md#what-a-build-guarantees), "What a build guarantees".

**3.8 Test before you flash.** A build that verifies is a well-formed
file, not a proof that your code works. Run it in an emulator if one
exists for the device (the example was checked by cold-booting it in
digikit's emulator: every screen differs from stock only in the corner
square, and the audio is identical). On hardware, know the recovery path
first: for the Digitakt mk1, hold FUNC while powering on for the startup
menu, then send the stock `.syx`. For the Analog Rytm MKII, hold FUNC while
powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), and send the
stock `.syx` over 5-pin DIN MIDI (not USB).

**3.9 Ship the `.elemod`.** Users install it from the loader's window
(Install from file) or pass it to `elekloader.patch`. Bump `version` on
every change.

**3.10 Another OS version.** A mod is built for one release: its `.elemod`
names that stock file's hash, and the loader refuses it with any other
(`core 2.1 is made for Digitakt mk1 1.53; the stock file is Digitakt mk1
1.54`). When Elektron releases an OS, build your mod again for it. Every
address your mod names may have moved: its sites (re-read their stock
bytes), its `defsym`, and the firmware routines your C code calls. The rest
of `mod.json` stays as it is. Give the new OS's values under `ports`, and
the stock file you build with picks them:

```json
 "os": "1.53",
 "cflags": ["-DFILLRECT_AT=0x400c19a6"],
 "defsym": {"DRAWALL": "0x400ca382"},
 "sites": [{"addr": "0x401ab9b0", "stock": "00000400", "op": "bytes", "new": "00000500"}],
 "ports": {
  "1.54": {
   "cflags": ["-DFILLRECT_AT=0x400c1bce"],
   "defsym": {"DRAWALL": "0x400ca5aa"},
   "sites": [{"addr": "0x401abcb0", "stock": "00000400", "op": "bytes", "new": "00000500"}]
  }
 }
```

- A port's keys replace the top level's for its OS, whole: give every site
  and every `defsym`, not only the ones that moved. It may not change `id`,
  `version`, `device` or `os`.
- Built from a 1.54 stock file, the SDK writes `my-mod-1.0-os1.54.elemod`;
  from 1.53, `my-mod-1.0.elemod`, as before. Ship one file per OS, and lint
  each with that OS's core (`core-2.1-os1.54.elemod`).
- C code gets its addresses from `cflags` (`-D`), as
  [examples/hello-marker](../examples/hello-marker) does; assembly from
  `defsym`, as [mods/core](../mods/core) does.
- From Digitakt mk1 1.53 to 1.54, and Digitone mk1 1.43 to 1.44, only the
  main OS changed, and most of it only moved: by a few hundred bytes at
  most, and the RAM past the image by 0x1000. A window of the old code with
  its absolute addresses masked out usually finds a routine again in the
  new image. Check every match by its bytes, then run the build in an
  emulator (3.8).
- From Analog Rytm MKII 1.73 to 1.74, code was added in two places: from
  0x40057xxx the code sits 0x6c further on, from 0x40123exx 0x1cc, and the
  data after the code further still ([DEVICES.md](DEVICES.md), "1.74").
  Find each address again by its bytes all the same.

## 4. Path A: a whole build

If you already build a patched firmware in one piece, `mkmod` turns it into
a format-1 mod without changing your build:

```bash
python -m elekloader.mkmod --stock Digitakt_OS1.53.syx --build my-cfw.syx \
    --manifest my-cfw.patches.json [--elf my-cfw.elf] --meta meta.json --out my-cfw.elemod
```

- The manifest lists your patches: `{"patches": [{"addr": "0x...", "old":
  "hex", "new": "hex"}]}`, each `old` the whole stock instruction(s) or data
  word. Every byte your build changes must be covered.
- No manifest? `--diff` (in place of `--manifest` and `--elf`) works the
  sites out by comparing your build with stock. A run of changed bytes is
  a code site only when it covers whole instructions on both sides;
  anything else is a data site. This is the path for whole builds, for
  example an octabam build for the Octatrack:
  `python -m elekloader.mkmod --stock OCTATRACK_OS1.40C.syx --build built.syx --diff --meta meta.json --out my.elemod`.
- The build may append one blob at the stock main OS's end (0x4025CA40 on
  the Digitakt mk1 1.53, 0x4025DA40 on 1.54); the ELF's `__run_start`, `__bss_end`,
  `__fast_start` and `__fast_len` describe where it runs.
- `meta.json`: `id`, `title`, `category`, `description`, `license`, `names`,
  `regions`, `requires`, and `data_sites` for data patches that happen to
  decode as instructions.

Expect `reproduces the build's main OS`. A format-1 mod cannot be combined
with anything, so plan the move to Path B:

1. List every site your build patches. The shared ones (section 2's table)
   become event handlers; the rest stay sites of your mod.
2. Split your code by feature. What one feature needs from another becomes
   an export and an import, or a weak import if the other is optional.
3. Make each feature a mod folder with a `mod.json`, build and lint them,
   and check that the combined build behaves like your old one (the same
   audio and screens in an emulator is a good bar).

## 4b. Octabam modules (Octatrack)

sambanks/octabam declares each Octatrack module in
`modules/<name>/manifest.py`. `elekloader.sdk.octabam` reads those
manifests from a checkout, and turns each ColdFire-only module into a
linkable mod for the Octatrack's core (`mods/core-ot`):

```bash
git clone --recurse-submodules https://github.com/sambanks/octabam
python -m elekloader.sdk.octabam --octabam octabam --stock OCTATRACK_OS1.40C.syx \
    [--module recorder-hold ...] --out ~/octabam-mods
```

`--out` must be outside the elekloader checkout: the converted mods carry
octabam's sources, and they stay out of this repository. Each converted
module gets:
- a mod folder: `mod.json`, octabam's sources, generated glue, and octabam's
  licence;
- a built `octabam-<name>-<commit>.elemod`.

Every line of output is one of these:
- `CONVERTED`, with what was checked;
- `REFUSED`, with the reason;
- `FAILED`, when a build or a check failed.

It needs the m68k cross binutils.

The converted mods of the shop are from octabam's commit 363861e. octabam's
later manifests declare more than the converter serves yet: settings
(octabam's build, or BRAIN, its settings store on the unit, resolves
them), modules that need BRAIN, DRAM regions, fixed arena bytes, stock
bytes a module keeps (`Keep`), units in octabam's loader and a DSP build
step of a module's own (`dsp_source`). A module that uses any of these is
refused, with the reason. A submodule in a module's folder is another
repository (Zac Kyoti's modules check out all of theirs): its `.asm` files
are not the module's. So is a generated include
written for octabam's later `include(modules, remix)`, and a module that
links another's symbols (`CC_MODEDEF1`, `ARENA_BASE`) or requires one that
does not convert (STORE) fails its link: nothing half-converted is written.

`--sites FILE.json` converts nothing: it writes the stock code every module
of the checkout hooks, whether or not it converts, as the code sites its
conversion has (and the reason it is refused, if it is). That is the
upstream list for the community's to-do list
([Growing the core](CORE-CONTRIBUTING.md#what-is-left-to-do)):

```bash
python -m elekloader.sdk.octabam --octabam octabam --stock OCTATRACK_OS1.40C.syx \
    --sites web/upstream/octabam.json
```

Where code goes:
- **Floating code** goes in the core's RAM reserve. That includes the caves
  octabam floats in zero runs inside the OS image, so such a cave is the
  same code at another address.
- **Pinned code** (`cave_addr` set) stays where octabam pins it, as
  `fixed` code (section 3).

The converter's docstring has the table from each octabam construct to what
it becomes. Before it writes `CONVERTED`, it links each mod with the core
and checks:
- **Caves.** Each cave's bytes equal its source, linked by GNU ld at the
  same address, as octabam's build links it. They also equal the bytes the
  manifest ratifies (`pinned`, or `reference(addr)`).
- **Sites.** Every site holds what octabam would write there, for the final
  addresses: hooks, `emit()` pokes, detours, symbol refs, pokes, tables.
- **The whole mod.** Its RAM and fixed code equal GNU ld's link of the same
  object.
- **Units.** A unit that octabam assembles for another CPU encodes the same
  for the chip. A unit with a `reference` still links to its author's bytes.

**Bridges** (`Override`): a module that stands in for another module's hook
converts as one mod that carries both.
- The other module's detour at that site is left out.
- The includes are generated for the pair.
- The bridge conflicts with the other module alone.

**Remixes** (`--remix NAME`): an octabam remix converts as one mod. Use it
for USB AUDIO IN, which only converts this way:

```bash
python -m elekloader.sdk.octabam --octabam octabam --stock OCTATRACK_OS1.40C.syx \
    --remix usb-io-tracks-main-cue-ab --out ~/octabam-mods
```

- The mod carries the remix's own modules (not the stock effects it
  lists), converted as above, with the includes generated for all of them.
- A module whose DSP code is reached only by its hooks into stock DSP code
  (no FX menu entry, no knobs) converts here: USB AUDIO IN's RX inject.
- Everything else the remix writes in the OS image comes from octabam's own
  build of it. The converter runs `tools/build/build_bus.py` in the checkout
  (it needs the checkout's `dsp_asm` from `make setup`, and `m68k-elf` on the
  PATH; it rewrites the checkout's `out/mainos_bus.bin`), or takes that file
  from `--reference`. Each byte it changes outside this mod's sites and the
  core's becomes a data site. For the 12 `usb-io-*` remixes that is:
  - the RX inject in SPATIALIZER's DSP code on payload A, and its hook at
    `P:0x88`;
  - SPATIALIZER's dispatch on payload A pointed at the null stub;
  - the FX1 and FX2 choosers without SPATIALIZER, and their row tables.
  So these mods take SPATIALIZER off both FX menus, as the remixes do. With
  `--bus`, the DSP bus does that instead (below).
- **The check** links the mod with the core and requires the OS image to
  equal octabam's build in every byte. The only exceptions are the address
  operands of our placed code and the descriptor served from a copy. On top
  of that, each module gets the usual checks above.
- Like a bridge, a remix mod conflicts with each of its modules alone.

**On the hook bus** (`--bus`): the hooks the Octatrack core's bus serves
(core-ot 0.2) subscribe to its events instead of patching their sites.
- **Which hooks.** The converter's `BUS` table names them, each read from
  its module's own source:

  | module | its hook | the bus event |
  |---|---|---|
  | TUNER | frame_isr's tail (0x4000d99a) | `ev_frame` |
  | TUNER | the UI task's loop head (0x40056c72) | `ev_tick` |
  | CC FEEDBACK | the key-repeat task's loop (0x4005595c, 120 Hz) | `ev_tick`, two sweeps a tick, so its rate stays the same |
  | CC MAP | the CC entry of the MIDI dispatch (0x400d64a0) | `ev_midi`: it takes CC 62-73 and passes the rest on |
  | USB AUDIO OUT, every layout and every `usb-io-*` remix | the per-block producer (0x4000d9a0) | `ev_frame` |

  TUNER's TEMPO opener and the USB modules' other hooks are not bus events,
  so they stay sites, as without `--bus`.
- **What changes.** The hook's site is left stock. Generated glue
  (`glue/ob_<name>_bus.s`) subscribes to the event and keeps the C
  convention. It calls the hook's own code:
  - CC FEEDBACK's `cf_sweep` already keeps the C convention, and is called
    as it is;
  - TUNER's and USB's stubs take every register as free and end by jumping
    back to the firmware. Their one jump back is rewritten in the source to
    reach the glue;
  - CC MAP's cave passes a CC on through its `CC_NEXT`. The glue defines
    `CC_NEXT`, instead of the stock handler's address.
- **Versions.** Such a mod is versioned `<commit>-bus` and needs core-ot 0.2
  or newer. With core 0.1 the linker refuses it (*"adds to table ev_frame,
  which no given mod declares"*). Every other module converts exactly as
  without `--bus`.
- **The check also proves the bus hooks.** For each one:
  - its site is stock in the linked image, and its glue is in the event's
    table;
  - a stub entered past its replay of the displaced instruction starts with
    that instruction;
  - a rewritten source assembles to octabam's bytes but for its jumps back
    to the firmware, whose relocations name the glue;
  - CC MAP's cave equals octabam's link (and its ratified bytes) but for
    its `CC_NEXT` operand.

  In a remix, the hook's 6 bytes octabam's build writes are the only other
  bytes allowed to differ, and only while they are stock.
- **The DSP bus too.** USB AUDIO IN's RX inject hooks payload A's P:0x88,
  which the DSP bus serves (`mods/dspbus-ot`, `ev_dsp_rx`), and every
  `usb-io-*` remix gives up SPATIALIZER, as the bus does. So with `--bus`:
  - the remix's mod leaves the hook, the FX menus and SPATIALIZER's words to
    the bus. Its inject source is assembled into the mod (`"dsp"`, with
    octabam's dsp_asm: set `ELEKLOADER_DSP_ASM`), and subscribes to
    `ev_dsp_rx` past its replay of the displaced instruction
    (`"addend": 2`, the instruction's two words);
  - it requires the DSP bus (`"requires": ["core", "dspbus"]`);
  - the check links it with the bus (built from `mods/dspbus-ot`, or
    `--dspbus`). The image must equal octabam's build but for the hook's
    jump and the bus's table, so the inject is where octabam placed it and
    the menus are what its build writes. The inject must begin with the
    instruction its hook displaces.

  A remix that gives up another effect, or a DSP hook elsewhere, is refused
  with `--bus`.

**On the intercepts** (`--intercepts`): a detour at one of the intercepts'
sites (the converter's `INTERCEPTS`: mods/intercepts-ot's 19 events,
"Intercepts on the Octatrack" above) leaves its site stock and adds the
record `{0, the detour's label}` to the event's table instead. The stub
enters the label as the detour's own `jmp` or `jsr` did, so its code runs
unchanged; claims 0 intercepts every call.
- **Which detours.** The same kind and length as the event's site; any other
  detour there is refused with `--intercepts`. Today: SCALE QUANTIZER's 17 (the
  CHROMATIC keyboard, its octave key and drawer, the p-lock and knob
  stores, the settings, the project defaults, load, line and write) and the
  SYNTH MACHINE's two (MIDI note on and off). The quantizer's `qz_octnum`
  overlaps Modwerk's `ev_octave_number` site, which intercepts does not
  have, so it stays a site.
- **Versions.** Such a mod is versioned `<commit>-intercepts`
  (`-bus-intercepts` with both) and requires intercepts:
  `"requires": ["core", "intercepts"]`.
- **The check** links it with the intercepts (built from
  `mods/intercepts-ot`, or `--intercepts-mod`) and proves, for each: the site
  is the event's `jmp` or `jsr` to its stub, nop-padded as the detour was,
  and the event's table holds `{0, the label}`. Every other byte is checked
  as without `--intercepts`.

**As machines** (`--machines`): octabam's source machines (ANALOG BD,
OCTA303, CXOSC, BLACKPYRAMIDS-303) each patch the same places to add their
row to the machine chooser, so no two of them share a build. The machines
companion (`mods/machines-ot`, "Machines on the Octatrack" above) owns
those places for every machine. With `--machines`, a module that patches
them becomes a machine on the companion:

```bash
python -m elekloader.sdk.octabam --octabam octabam --stock OCTATRACK_OS1.40C.syx \
    --module octa303 --machines --out ~/octabam-mods
```

- **Which modules.** One that writes any of the machine row's places (the
  converter's `MACHINE_SEAMS`: ANALOG BD's measured seams, which OCTA303 and
  BLACKPYRAMIDS-303 reuse) and has an entry in `MACHINES`. Today that is
  OCTA303 (octabam b085c4d). A module that writes those places with no
  entry is refused with `--machines`; every other module converts exactly
  as without it.
- **What changes.** Its places of the row are left stock, for the
  companion's sites. In OCTA303's case, 15 detours and 6 pokes:
  - the Part validator;
  - the names, by row and by machine byte;
  - SRC SETUP's names, rows, opening row and two commits;
  - SETUP's edit and draw columns;
  - the chooser's row count, drawer, match, highlight, opening row, commit
    and cursor bound;
  - the UI loop where it published its page as row 5's.

  Instead, the mod adds a record (`struct ot_machine`) to `ot_machines`,
  generated in `glue/ob_<name>_machine.s` from what `MACHINES` reads in the
  module's own source:

  | field | OCTA303 |
  |---|---|
  | `sig`, `sig_lo`, `sig_hi` | "TB", 1, 1: the signature its tracks carry, as before |
  | `label` | `o3_title`, "OCTA-303" |
  | `flags` | `PROTECT` (what its validator wrap did) and `RENDER_FLEX` (its renderer also gets the FLEX tracks no machine owns: a TB303*-named sample plays the 303, as before) |
  | `page` | the page record `o3_mdesc` builds, a clone of the user's FLEX record |
  | `track_page`, `ui_tick` | `o3_mdesc`: the companion's 60 Hz tick builds the page, as its own tick did |
  | `defaults` | `o3_mdefs` |
  | `assign` | glue around `o3_sigwrite`, which signs and unsigns a track as before (the defaults, the battery copy, the live lane) |
  | `render` | `sy_render`, which octabam put in the renderer kind table's FLEX entry (`0x400d6438`); that entry is left stock |
- **Its source.** The one line that names the FLEX renderer it calls
  (`STOCK_RENDER`, `0x40004008`, now the companion's site) names
  `ot_stock_render`, so the renderer does not call itself. The names the
  record needs are appended to the source as `ob_<name>_*` aliases of its
  labels. Nothing else changes, and its code for the places it leaves stock
  stays in it, unreached.
- **The rest.** Its detours at the intercepts' sites go on the intercepts
  (OCTA303's CHROMATIC pitch, `ev_chromatic_pitch`). Its other sites stay
  its own: OCTA303's hook at the PLAYBACK page load (`0x40031ece`, for a
  TB303*-named sample) and its retrig (`0x4000c634`). SYNTH MACHINE has
  sites at both, so the two still do not combine.
- **Versions.** Such a mod is versioned `<commit>-machines` and requires
  `["core", "intercepts", "machines"]` (intercepts only when it has a
  detour there).
- **The check** links it with the core, the intercepts and the companion
  (built from `mods/machines-ot`, or `--machines-mod`) and proves:
  - each byte of its places of the row, and of the kind-table entry, is a
    byte of one of the companion's sites or stock, and none is a site of
    its own;
  - the record holds what `MACHINES` says, field by field, and the name it
    points at is the name;
  - its source assembles to octabam's bytes but for its calls to the
    renderer, which name `ot_stock_render`, and nothing in the linked mod
    calls the renderer's entry.
- **Checked in ot_emu**, with core-ot 0.3.1, the intercepts and the
  companion, alone and with CRUSH beside it:
  - the chooser lists OCTA-303 below PICKUP (and scrolls to it below CRUSH);
  - YES signs track 1 "TB" 1, a FLEX track, in the Part and its battery
    copy, with OCTA303's defaults;
  - the SRC page reads TUNE CUT RES ENV DEC ACC under "OCTA-303", and
    PLAYBACK SETUP reads WAVE GATE FINE SLID DRIV ACCV;
  - a trig plays through `sy_render` on every render call;
  - FLEX again unsigns the track.

What does not convert, and why:
- **DSP code.** Modules with DSP code or an FX menu entry are out of scope,
  but for hook-only DSP code in a remix (above).
- **Code pinned outside the device's free image areas.**
- **Formatter registrations.** They draw a DSP module's knob.
- **Runtimes.** This covers Octakit's runtime, arena reservations, and
  bridges over a runtime's writes (KITS RELOAD, SCENES KITS, SCENES P2 KITS).
- **Writes into the protected bootloader copy**, other than a poke wholly
  inside data the device marks `relocatable` (below). The 16 KB below
  `0x400e21e0` stay stock; octabam also places the bootstrap copy there
  (`~0x400de7dc`).
- **Writes into bytes the device keeps stock** (`keep`), and units pinned
  there. These are the 16 bytes at `0x400d24d0`, which the stock DELAY
  reads (octabam #771). A cave pinned there goes in `.run` instead (below).
- **Modules that serve DSP modules only.** These are MODE DEFAULTS, RIG
  HOSTS and TEMPO BUS (`SERVES_DSP` in the converter, with the reasons).
  Converted alone, they would be inert, or they would point parts at
  engines the image does not have.
- **Modules that need one of the above.**

Some differences from an octabam build, by design:
- **`include` units** get the text octabam would generate for the modules
  the mod carries.
  - If that text would change with another module that converts, the mod
    conflicts with that module.
  - If it would change only with modules that do not convert, the notes say
    so.
- **`defsyms`** keep the manifest's own value, even where octabam would
  link another module's symbol (CC MAP's `CC_MODEDEF1`).
- **Claims** (Part window, SRAM) become one named resource per 16-byte
  block. Overlapping claims share a name, so the linker refuses the pair,
  as octabam's ledger does for MIDI SCENES and SCENES P2.
- **A cave pinned on bytes the device keeps stock** goes in `.run`, as a
  floating cave would. SYNTH MACHINE before octabam's `PLACE` setting
  (363861e) pins its page cave at `0x400d24d0`; octabam's own fix, `PLACE`
  = DRAM, links the same `page.s` into its runtime.
  - Emit's pokes are taken at the pin. One that holds an address in the
    cave becomes a `jmp`/`jsr`/`ptr` site against it: SYNTH's resolver at
    `0x40031ece` jumps to the page in `.run`.
  - The check links the source at its pin, where it must equal the bytes
    the manifest ratifies. It then proves that the copy in `.run` differs
    from that only in pointers into itself, moved by the distance. A word
    that points into the cave at its pin and stays put is an address the
    source pins, and the check refuses it.
  - The bytes at the pin stay stock.
- **A `jmp` detour** whose six bytes end inside an instruction is
  nop-padded to that instruction's end.
- **A poke into relocatable data** (the device profile's `relocatable`) is
  served from a copy. The USB AUDIO OUT modules set the class of the USB
  device descriptor (`0x400e2004`, inside the protected bootloader copy).
  Converted, each carries a copy of the 18-byte descriptor in `.run` with
  that poke applied, and points the descriptor's one reference
  (`0x4001d82e`, the `pea` that GET_DESCRIPTOR sends from) at it.
  - The host reads the same bytes as from octabam's build.
  - The protected range stays stock. The check proves the copy, the
    reference and the untouched original.
- **The assembler.** octabam builds with the bare-metal `m68k-elf` binutils.
  Ubuntu's `m68k-linux-gnu-as` is for a Linux target: it leaves every
  reference to a global label to the linker (a shared library may preempt a
  global), where `m68k-elf-as` resolves it in a shorter PC-relative form.
  - With `m68k-linux-gnu`, such code assembles longer: the same code, but
    not the same bytes. A unit whose author pinned its bytes (USB MIDI)
    fails its check.
  - The converter uses `m68k-elf-` when it is on the PATH (or
    `ELEKLOADER_CROSS`), and says at the start which kind it has.
  - To build it: GNU binutils, `configure --target=m68k-elf`, then
    `make all-gas all-ld all-binutils`.

## 5. When the loader refuses: message → fix

| message contains | means | fix |
|---|---|---|
| `made for a firmware elekloader does not know` | the target or stock file is not a supported release | use the exact stock file; check its hash against `devices/` |
| `is made for <device> 1.53; the stock file is <device> 1.54` / `mod.json is for ... 1.53; the stock file is ... 1.54` | the mod is for another OS version | build it for that OS: a port (3.10) |
| `is inside 0x...-0x..., ...: protected on the <device>` | the site writes bytes the device protects (the Octatrack's bootloader copy) | move it; the device's profile (docs/DEVICES.md) gives the range and the reason |
| `writes into 0x...-0x..., which stays stock on the <device>: ...` | the site, or `fixed` code (SDK), writes bytes that look free but the OS reads: the Octatrack's 16 bytes at `0x400d24d0`, read by the stock DELAY | put the code in `.run`, or `fixed` past the range (`0x400d24e0` on); the message gives the reason |
| `0x...-0x... differs from stock, and no site, DSP section or table of the mods declares it` / `... is not stock in the linked image` | the linker built a main OS in which bytes changed that no mod declared, or that the device keeps stock: a bug in the linker, not in your mod | report it with the mods and the stock file's version; the build refuses rather than write the file |
| `the stock bytes are ..., not ...` (SDK) / `not the ones it expects` | the address is wrong, or the OS version differs | re-read the stock bytes at that address |
| `ends mid-instruction`, `sweeps land on the start`, `does not decode` | the site does not cover whole instructions | move or widen the site to instruction boundaries (disassemble the stock main OS) |
| `... overlap (0x...-0x...)` | another mod patches or claims those bytes | subscribe to an event instead, agree with that mod's author, or propose the site as a core event ([CORE-CONTRIBUTING.md](CORE-CONTRIBUTING.md)) |
| `X requires Y` / `core is not enabled` | a dependency is missing | add Y (with lint: `--with Y.elemod`) |
| `X needs core 3.0 or newer, and this build has core 2.1` | the mod's `resources.core` asks for a newer core | build with that core (the app and the web page take it when you tick the mod); an older loader says `imports fw_..., which no given mod exports` instead |
| `imports cm_ui_v31, which no given mod exports` | the page names `cm_ui_v31` (a knob that draws itself, `CM_DRAW`), and no mod given exports it: machine-pages before 1.1 does not | use machine-pages 1.1 or newer |
| `CM_KNOB: draw= needs flags=CM_DRAW` or `CM_KNOB: with CM_DRAW, +36 is draw` (assembly, `core3.inc`) | a knob's `draw=` without `flags=CM_DRAW`, or its `gfx=` with it | give the knob `flags=CM_DRAW` with `draw=`, or `draw=` in place of `gfx=`; the page's `CM_UI` needs `abi=cm_ui_v31` |
| `imports N, which no given mod exports` | a missing dependency, or a typo | add the mod that exports N, fix the name, or list N under `weak` |
| `adds to table T, which no given mod declares` | the table's owner is missing, or the event name is wrong | add core (or the owner); check the event name |
| `both export S` | two mods define the same global | prefix your globals with your mod id; make internals `static` |
| `relocation type N` | a reference the linker cannot relocate (e.g. 16-bit absolute) | use 32-bit addressing for your own symbols; build with the SDK's flags |
| `section X is not one elekloader places` | code or data in a section the linker does not know | C: default sections; asm: `.section .run, "ax"`, `.fast` or `.bss` |
| `the mods need RAM to ...` / `.fast code needs SRAM to ...` | over budget | shrink tables and buffers; keep `.fast` for the hottest code only |
| `has .fast code, which needs ...` | `.fast` is copied in by FAST AUDIO | require `fast-audio`, or use `.run` |
| `PC-relative, inside ... copied block` | a site in FAST AUDIO's block branches relatively | use an absolute `jmp`/`jsr` |
| `both claim N` | two mods use the same named resource | pick another SysEx id, row name or path |
| `is a whole build: it cannot be combined` | a format-1 mod was mixed with format-2 mods | use one or the other; see section 4 |
| `has DSP code for payload A, which needs a mod that frees DSP memory there` | nothing frees P memory for DSP code | add the mod that claims the name the message gives |
| `the DSP code for payload A needs P words to ...` | the DSP code and tables do not fit in the freed words | shrink your DSP code |
| `holds an address the linker cannot place` (SDK) | a label used as a short or packed operand | use a form that takes the address as a whole word (`>`); branches are relative |
| `is a DSP reference to a ColdFire address` (or the reverse) | a `dsp24` relocation names a ColdFire symbol, or a ColdFire one a DSP label | name the right symbol; DSP labels are word addresses on the DSP |
| `dsp_asm is not installed` | the SDK needs octabam's DSP assembler | build octabam's `vendor/dsp56300` and set `ELEKLOADER_DSP_ASM` |
| `the label N is part of a longer word here` (SDK) | dsp_asm would put N's address inside that word | rename the label |
| `SHARC ABI does not match the device profile` | a SHARC mod's `abi` is not `dn2-master-mono32-l2-v1` | set it; the other ABI is core's bridge's |
| `SHARC payload has a gap or overlap in its owner` | the segments and zero ranges do not cover the mod's 32 KiB exactly once | add a `zero` range for the bytes you do not use; fix overlapping offsets |
| `SHARC mod code needs its profiled NOP read-ahead guard` | a code segment does not end in 64 bytes of NOPs (`0100` repeated) | pad the end of each code segment |
| `SHARC entry is in the instruction read-ahead guard` / `SHARC entry is not in owned code` | a callback offset is in the guard, outside the code, or odd | link the callbacks inside a code segment, before its guard |
| `active SHARC mod requires a qualified matching core bridge` / `has unqualified active SHARC core bytes or entry` | the core is older than core-dn2 0.2, or its bridge is not the one the profile pins | build with core-dn2 0.2 or newer, its bridge unchanged; set `"resources": {"core": "0.2"}` |
| `active SHARC stack exceeds the profile limit` | the mod declares more than 428 bytes of stack (the bridge's 116 and yours may not pass 544) | use less stack, and declare what you use |
| `only one SHARC mod may own the DN2 reservation` | two SHARC mods in one build | pick one |

## 6. Definition of done

- [ ] `python -m elekloader.sdk.build <dir> --stock <stock.syx>` prints `BUILT`.
- [ ] `python -m elekloader.lint <mod> --stock <stock.syx> --with <core> [--with <others>]`
      exits 0, with every mod users are likely to combine yours with.
- [ ] `python -m elekloader.patch ... --out test.syx --version XXXX` prints `WROTE`.
- [ ] `mod.json` has `description`, `category`, `license`, `requires`, and every named
      resource you use under `resources.names`.
- [ ] Your sources hold no copied firmware code, and no firmware file is
      committed anywhere.
- [ ] The build ran in an emulator or on a device you can recover.
