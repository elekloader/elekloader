# core (Digitone II)

The boot copier and the hook bus for Digitone II OS 1.12. Every set of
linkable (format 2) mods for the Digitone II needs it. On its own it changes
nothing the unit does.

It builds the shared [../core/core.s](../core/core.s) and
[settings.s](../core/settings.s) with the Digitone II's addresses
(`mod.json`), and the Digitakt II's
[../core-dt2/personalize.s](../core-dt2/personalize.s) unchanged: the
PERSONALIZE builder ends the same way. There is no `fast.s` (SRAM is not
qualified, so there is no `.fast` area) and no `render.s` (the audio renders
on the DSP).

The core includes the SHARC bridge. A DSP mod requires this core and
provides a prelinked artifact following its callback contract.

`bridge.sharc.json` is the bridge, already compiled and linked from
`sharc-bridge.s`, with the slot and pitch reader the
[sine example](../../examples/sharc-sine/README.md) relies on. The linker
adds it, and replaces the six-byte stock instruction that runs it, only for
a SHARC mod with callbacks that passes the
[checks](../../docs/FORMAT.md#digitone-ii-sharc-mod). The device profile pins
the bridge's bytes; a mod's code and callback offsets may change. With core
alone, or with an inert SHARC mod, the DSP's audio stays stock. Building
this core needs only the ColdFire tools, no SHARC compiler.

- **At boot** (the OS entry's call at 0x40000538, which called 0x400019b0,
  the Digitakt II's: it sets the VBR to 0x40000000 and returns), `boot`
  copies the RAM image the linker built to the device's DDR area
  (0x47F00000), zeroes `.bss`, and goes on to the call it replaced, before
  the OS copies its data to SRAM and clears its `.bss`. It does not start
  DTIM0 (`NO_DTIM0`): as on the Digitakt II, the OS reads its counter at ten
  sites (0xfc07000c) and never writes its mode register, so stock reads it
  as stopped, and this core keeps that.
- **The sites** become the Digitakt II's events, with the same prototypes
  ([docs/ADAPTING.md](../../docs/ADAPTING.md), "The hook bus"):

  | site | was | event |
  |---|---|---|
  | 0x4002e464 | `jsr 0x4011d5d4`: returns the view controller's byte +0x20 | `ev_tick` |
  | 0x4002e4ca | `jsr 0x4011d60a`: `drawAll(ctrl, bmp)`, the 128x64 frame | `ev_draw` |
  | 0x4002f20c | `jsr 0x4002c5c8`: `Brain::key(brain, event)` | `ev_key` |
  | 0x4002f256 | `jsr 0x4002c55c`: `Brain::enc(brain, event)` | `ev_enc` |
  | 0x4009ff88 | `movea.l (a2),a0; pea 2.w`, the SETTINGS builder 0x4009fbe0 going on at 0x4009ff8e | `ev_settings` |
  | 0x400987a2 | `movem.l $18(a7),d2-d6/a2-a6`, the end of the PERSONALIZE builder 0x40097eac, after NOTE EDIT MENU | `ev_personalize` |

  `core_additem(menu, row)` adds a row with the firmware's routines:
  `operator new` 0x40120544, `MenuItem`'s constructor 0x401173b8,
  `Menu::addItem` 0x40116bee and the `std::function` manager 0x4018de3e
  (for `PFSs15FileStorageInfoE`, as on the Digitakt II). A row's label
  callback builds a `std::string` with 0x401ceab8, as the firmware's own
  labels do (0x4002b294). In SETTINGS the row comes after SYSTEM; in
  PERSONALIZE after NOTE EDIT MENU.

Each address was found by the Digitakt II 1.17's code, with the addresses
masked, then checked in the Digitone II's own, with what the shared
sources assume about it:
- the UI loop around the tick, draw, key and encoder sites is the
  Digitakt II's instruction for instruction. The tick routine is
  `move.l 4(sp),a0; move.b $20(a0),d0; rts`, what `core_tick` does after
  its handlers. The draw site pushes the frame's Bitmap and then the view
  controller; the key and encoder sites push the event and then the brain.
  Each routine is its site's only caller. `Brain::key` hands the event to
  the listeners at brain +0x94..+0x98, as on the Digitakt II.
- the SETTINGS builder keeps the menu in a2 and its site is the two
  instructions `core_settings` replays.
- the PERSONALIZE builder makes its last row exactly as `core_additem`
  does: `new(0x54)`, the constructor with (item, label, select, draw,
  change, -1, 8), each callback a 16-byte `std::function` (manager at +8,
  invoker at +12), then `Menu::addItem(menu, item)` with the menu in a4;
  it ends with the `movem`, `lea $80(a7),a7` and `rts` that `personalize.s`
  replays. Its frame is the Digitakt II's (0x80 bytes).
- `KeyEvent` (built by 0x40116278 from the UI queue's message): +12 the key
  code and +16 the action, as on the other devices (docs/ADAPTING.md);
  read back in the emulator: a tap of TRIG 1 gives code 25, a release 0x10.

`tests/test_digitone2.py` checks each of these against the stock file.

Build it with the SDK (it needs m68k binutils; with Homebrew's
`m68k-elf-binutils`, set `ELEKLOADER_CROSS=m68k-elf-`):

```bash
python -m elekloader.sdk.build mods/core-dn2 --stock Digitone_II_OS1.12.syx      # out/core-0.1.elemod
python -m elekloader.lint mods/core-dn2/out/core-0.1.elemod --stock Digitone_II_OS1.12.syx
```

## Input bridge and SysEx observers

DigiHealth's DN2 port uses these shared facilities for remote input and
SysEx requests.

- `input.c` merges physical and synthetic key masks and refuses physical
  overlap or a full UI queue. `core_keys_set` and `core_keys_clear` run only
  on the UI task. The stock driver at `0x4011fc8c` retains its 68-byte frame;
  the bridge replays its complete eight-byte prologue and continues at
  `0x4011fc94`. Its state bytes are at `0x445a2e30`.
- The queue object is `0x4059e214`: the UI receives it at `0x4002f178`, and
  initialization passes it to `0x4011fa7c`. `0x4029f0d0` is the panel's
  pointer cell, not the queue object. Queue checks reserve room below the
  stock key/encoder record-pool limits.
- `core_encoder_turn(1..9, -30..30)` (nonzero detents) calls the stock panel
  decoder `0x4011ff0c`. The stock periodic callback queues the accumulated
  encoder event through the same core/stock UI path as physical input.
- `core_millis()` uses the existing panel callback, registered at
  `0x401200c4` with mask 1. PIT2's stock setup has prescaler 6 and modulus
  17187: approximately 120 Hz at the derived 132 MHz bus clock. The bridge
  adds 25 ms per three callbacks (8/9 ms resolution), separately from the
  stock counter that wraps at 4096. It starts no timer. A stalled scheduler
  can delay input cleanup.
- `ev_sysex`: `void f(const unsigned char *message, unsigned length,
  unsigned source)`, on the MIDI task before stock routing. It preserves
  the router ABI at `0x4012197a` and always continues stock routing. Handlers
  validate their own header, exact lengths and source; 2/4 are USB and 8/16
  DIN. `fw_sysex_send` exports the stock sender `0x401236d2`.

The C declarations are in `elekloader/sdk/include/digitone-mk2/core.h`.
No arbitrary memory-write or function-call command is added.

## Checks

`tests/test_digitone2.py` checks stock sites, stack/queue contracts, clock
setup and exact patch boundaries. `tests/test_dn2_input.py` checks input
ownership and queue bounds without firmware. The coupled emulator checks
native machine selection and DSP output with the external consumer.
Hardware workload and timing qualification remain separate.

Use the owner's [DigiHealth DN2 fork](https://github.com/simonjohansson/digihealth/tree/codex/dn2-remote-control)
for device interaction. The hooks-check example deliberately overlays the
stock interface; omit it from normal mod builds.
