# core for the Octatrack

The boot copier and the hook bus for the Octatrack (MKI and MKII), OS 1.40C.
Every set of linkable (format 2) mods for the Octatrack needs it. It takes
from the sample and recorder memory only the 6 KB pages the mods' RAM fills
(from 0.3: one page for core and a small mod, where 0.2 took 10 MB), and
changes nothing else the unit does.

- **The reserve.** The OS gives the whole audio page arena
  (0x40a955e0-0x46025de0, 14,602 pages of 6,144 B) to samples and the
  recorders. The core moves the arena's base past its bottom pages, so they
  are left for mods: the linker places `.run` and `.bss` from 0x40a955e0
  (the device's `ddr`). That takes 27 writes (`mod.json`):
  - 22 instructions carry the base;
  - one carries base + 6,144;
  - four literals set the geometry: the page count, the fill limit and the
    clear length each shrink by the pages taken.

  These are the same writes sambanks/octabam's platform makes (`arena.py`,
  MIT) for the same reservation.
  - **0.3.1: PURGE SAMPLES no longer freezes the unit.** The base's
    literal has a 23rd use, `cmpa.l #0x40a955e0,a2` at 0x400254f8, which
    is not the arena: it ends PURGE SAMPLES' walk over the 16 banks, which
    end where the arena starts only by layout. 0.1 to 0.3 moved it with the
    base, and the walk stops only on an exact match, so it never ended. It
    stays stock now. Found and fixed in octabam (#753), whose unit (an MKII)
    froze on PURGE and stopped freezing with the fix. In octabam's emulator,
    calling PURGE's routine (0x400253d4) after a load: with 0.1 (the released
    core) and 0.3 the walk runs on until the emulator stops it at 0x69f72010;
    with 0.3.1, as with stock, it leaves the walk at 0x40025502 and returns.
    octabam's builds before #753 carry the same move: `sdk.octabam` leaves it
    out of a converted remix and its check allows for it.
  - **0.3 takes only what the mods use.** Each write is a `ptr` against a
    symbol the linker defines from the pages the linked mods' RAM fills, at
    least one (the profile's `reserve`): `arena_base`, `__arena_pages`,
    `__arena_fill`, `__arena_clear`. Core with track meters and OT HEALTH
    takes 1 page, so the arena keeps 14,601 of its 14,602 pages; the most
    is 1,707 pages, 0.2's whole reserve (0x40a955e0-0x41495de0).
  - 0.2 took 1,707 pages whatever the mods used, and exported the moved
    base as `arena_base` itself. With 0.3 the linker defines `arena_base`,
    for mods that compare against it.
  - tests/test_octatrack.py derives each write from the page count: the
    stock value at 0 pages, 0.2's at 1,707. Under octabam's port, the
    OS's free-page count (0x80006920) after boot is 1,706 pages higher
    with 0.3 than with 0.2, for the same mods.
- **At boot.** The boot site 0x4000050c called 0x40001e50; it now calls
  `boot`, at the end of the OS image (0x4010fdf0), where the bootstrap
  unpacks it.
  - `boot` makes the call it replaced first, as octabam's loader does.
  - It then copies the RAM image into the reserve and zeroes `.bss`, both
    through the uncached alias (+0x08000000).
  - It sets `core_up` (below) and returns with every register as that call
    left it.
- **The hook bus** (0.2, `bus.s`). Six sites become events that mods
  subscribe to (docs/ADAPTING.md, "The hook bus on the Octatrack"):

  | event | site (was) | when |
  |---|---|---|
  | `ev_tick` | 0x40061e94 (`jsr 0x40031970`) | 60 times a second, in the sys task's loop |
  | `ev_draw` | 0x40013cae (`movea.l 0x400b9710,a5`) | each composed frame, before it goes to the LCD |
  | `ev_key` | 0x40061dc8 (`jsr 0x40031734`, `key(code, pressed)`) | each key press and release |
  | `ev_enc` | 0x40061e00 (`jsr 0x40031944`, `enc(encoder, delta)`) | each encoder turn |
  | `ev_midi` | 0x40005572 (the MIDI thread's handler call) | each MIDI message in |
  | `ev_frame` | 0x4000d94e (`tst.l 0x46104d08`) | the frame interrupt, every 16 samples |

  `ev_tick`, `ev_key`, `ev_enc` and, nearly always, `ev_draw` run in the same
  task, so their handlers never interrupt one another.
- **0.4: the frame's end and MIDI out** (`bus.s`). Three more events, each on
  the instruction after bytes an octabam2elemod v1.0 mod patches, so those
  files still link. Their sites are `jmp`s: each stub replays what it
  displaced and goes back to stock itself.

  | event | site (was) | when |
  |---|---|---|
  | `ev_frame_end` | 0x4000d9a6 (`movem.l (sp),d0-a6 ; lea 252(sp),sp`, then `rte`) | the frame interrupt's last point, every 16 samples, past TUNER's and USB audio's tail |
  | `ev_midi_out` | 0x40010bd0 (`movea.l 28(sp),a2 ; tst.l 0x460ba978`), in midi_send past its prologue | each MIDI message the firmware sends |
  | `ev_midi_out_rt` | 0x400108c4 (`move.w sr,d2 ; move.w #0x2700,sr`), in the priority sender past its wait | each realtime byte the firmware sends |

  At `ev_frame_end` the interrupted task's EMAC state is already back. So,
  after the handlers, the stub runs the interrupt's own restore of it
  (0x4000d964..0x4000d980, from the interrupt's frame) again, and a handler
  may use the EMAC there too. The MIDI-out events run where the sender
  runs (a task or an interrupt). USB MIDI mirrors the DIN port's output
  through them; so can a MIDI monitor or router.
- **0.5: the transport, the frame's head, main's end, the 120 Hz tick and
  exports** (`bus.s`, `exports.c`, `elekloader/sdk/include/octatrack/core.h`).
  Five more events, then what the core exports. Each site is again a whole
  stock instruction (or the two of the frame interrupt's entry) that no
  octabam2elemod v1.0 mod patches, and its stub replays it:

  | event | site (was) | when |
  |---|---|---|
  | `ev_frame_begin` | 0x4000aad0 (`lea -252(sp),sp ; movem.l d0-fp,(sp)`), a jmp | the frame interrupt's first point; the EMAC state is not yet saved: handlers must not use it |
  | `ev_transport(what, playing)` | 0x4009c3d4 (start), 0x4009c4d4 (resume) and 0x4009f5c6 (stop), each a jmp over `move.l d0,0x800065b8` | right after the sequencer's word is stored: START (1), RESUME (1), STOP (2) |
  | `ev_boot_done` | 0x4001fc96 (main's last init call, `jsr 0x40098a2c`), a jsr | once, with the OS's tasks made and no project loaded |
  | `ev_idle` | the same stub | main's idle loop, only in a build that has a handler |
  | `ev_tick120` | 0x40055966 (`jsr (a2) ; addq.l #4,sp ; bra.s loop` in the key-repeat task), a jmp | 120 times a second |

  - **Stop is the STOP routine, not 0x4000b2c8.** The frame builder's
    `clrl 0x46c80350` there (SYNTH v1.0's `po_stop`) clears the DSP's
    restart word. Octabam's notes for SYNTH say it never ran for a STOP in
    its tests.
    The STOP key and MIDI stop both go through 0x4009f5bc, whose first store
    is `PLAYING = 2`.
  - **Main's idle** is the stock `bras .` unless a mod subscribes to
    `ev_idle`. Then the stub is the loop and never returns. That one site
    is a jsr: octabam's emulator reads a jmp at 0x4001fc96 as a detoured
    park and skips its code (`Rtos::atSpin`, for CF METER IDLE), and
    this way a build without an idle handler parks in the stock loop.
  - **The frame budget meter** needs no subscriber: `ot_meter` times every
    frame interrupt from its first instruction to its last on DMA timer 3
    (`core.h`, `struct ot_frame_meter`).
  - **`ot_page2_write`** and **`ot_midi_send_paced`** (`exports.c`) are the
    two routines octabam's CC MAP, TEMPO BUS and MODE DEFAULTS, and its
    CC FEEDBACK, each hand-roll (docs/ADAPTING.md, "What core-ot 0.5
    exports").
  - Core is C from 0.5: `exports.c` needs the cross compiler (`m68k-linux-gnu-gcc`)
    as well as the assembler.
- **The draw gate** (`gate.s`). The OS composes the screen twice before the
  boot site, so before `.run` is in the reserve. The draw site therefore
  calls `core_draw_gate`, which `mod.json` places (`fixed`) in the last 24
  bytes of the 338-byte zero run at 0x400c45b0, in the image from power-on.
  The gate goes on to the bus only once `boot` has set `core_up`; until
  then it does just what the site replaced.

## Beside octabam's modules

No site of the bus is a byte that a converted octabam module patches. The
bus hooks the same routines one instruction away from them:
- the MIDI thread's dispatch call, where CC MAP repoints the CC entry of the
  table that call reads (0x400d64a0);
- the frame interrupt before the EMAC state is put back, where TUNER and
  the USB audio mods hook its tail (0x4000d99a, 0x4000d9a0); and from 0.4
  its epilogue (0x4000d9a6), the instruction after them;
- from 0.4, midi_send past the prologue USB MIDI patches (0x40010bc8), and
  the priority sender past its wait, whose entry USB MIDI patches
  (0x400108b0);
- the sys task's tick, where TUNER and CC FEEDBACK hook the UI task's loop
  head and the key-repeat task (0x40056c72, 0x4005595c).

So the 34 mods of irpina/octabam2elemod v1.0 link with 0.2 and 0.4 as they did
with 0.1 (checked for 0.4: each alone, and the shop's two largest
combinations). Their code moves up by the size of core's own (216 bytes, rounded to
their alignment), and the linker relocates it.

`elekloader.sdk.octabam --bus` converts those modules' hooks onto the bus
instead (docs/ADAPTING.md, 4b): TUNER, CC FEEDBACK, CC MAP, and USB AUDIO
OUT's producer in every layout and `usb-io-*` remix. Those mods
(`<commit>-bus`) need 0.2.

## Build

Build it with the SDK (it needs m68k binutils, and from 0.5 the m68k gcc):

```bash
python -m elekloader.sdk.build mods/core-ot --stock OCTATRACK_OS1.40C.syx
python -m elekloader.lint mods/core-ot/out/core-0.2.elemod --stock OCTATRACK_OS1.40C.syx
```

[examples/hello-marker-ot](../../examples/hello-marker-ot) is the smallest
mod that uses the bus.

## Checks

Checked in octabam's emulator (`ot_emu`) with a test mod that has a marker
string in `.run`:
- stock and the core build both reach the RTOS handoff;
- `boot` runs once, called from the boot site;
- after boot, the marker is at 0x40a955e0, where stock has zeros.

0.2, in `ot_emu` on an empty card, 9 s with keys, encoders and MIDI from a
script:
- **Core alone matches stock.** The firmware's key, encoder, CC and
  note-on handlers run as often as on stock, and the last frame the
  compositor sent to the LCD is the same, pixel for pixel.
- **A test mod subscribed to every event** saw:
  - 428 ticks and 440 frames composed;
  - the 4 key events (the last, UP released: code 0x33, 0);
  - both encoder turns (the last, encoder 3, -2);
  - the MIDI messages that reached the firmware's handlers;
  - 18,741 audio frames (2,756 a second).

  No draw ran inside a tick. Its square was in the frame sent to the LCD,
  and nothing else changed.
- **With every handler taking its event**, the firmware's key, encoder, CC
  and note handlers never ran.
- **The v1.0 files.** Every hardware-test group (T0–T7), every variant and
  every one of the 34 mods alone links with 0.2. Each image differs from
  its 0.1 build only at core's new sites, and in pointers that point at the
  same symbol plus offset as before.

0.5, in ot_emu (the checks are a probe mod that logs each event, and a copy
of ot_emu whose park range for main can be set, for the idle loop):
- **Core alone matches stock.** After a session of 9 key presses (two
  track keys, the AMP, FX1, FX2 and LFO page keys, PLAY and STOP) and 36
  encoder turns, the current bank's RAM (635,712 bytes), the CS1
  window (729,088), the engine's RAM (26,688), the Part and its copy, and
  both frame buffers are byte for byte stock's. The one byte that differs is
  the OS's free-page count, one lower for the page core takes.
- **The transport events** fire for the PLAY and STOP keys, for the
  `--sequencer` start and for calls of the start and STOP routines, and for
  MIDI stop, continue and start (with transport receive on): START, RESUME
  and STOP, with `playing` as the word was stored, which the handler reads
  back as stored, in the task that made the call.
- **Frames.** `ev_frame_begin` ran before `ev_frame_end` in every frame
  (46,028 of 46,028 in a 17 s run). The meter read about 10,600 counts a
  frame (80 us in the emulator, which is not hardware's time), `tpf` measured
  47,891 over a 1,024-frame window (132 MHz and 44.1 kHz exactly). A handler
  that spent 80,000 counts in one frame's begin showed as `worst` 96,564,
  `overruns` 1 and `late` 1.
- **`ev_tick120`** ran 120 times a second (2,047 times in 17.05 s).
- **`ev_boot_done`** ran once, before the first frame, and ten tasks were
  made as in stock. `ev_idle` ran from main's loop, with ot_emu given main's
  loop as its park range (its own reading of the stub's jmp is why the site is
  a jsr).
- **`ot_page2_write`** against the four stock editors, from the same state:
  for each of the 6 slots of AMP, FX1 and FX2, and of PLAYBACK for three
  machines (36 cases), on tracks 0, 2, 5 and 7, the editor and
  `ot_page2_write` changed the same bytes to the same values (the Part, its copy, the part-changed bits, the
  bank's and the project's dirty flags, the live lane and its two copies the
  frame builder makes, the redraw marker). The editor also marks one more
  cell when the slot is 0, a redraw bookkeeping this does not make. A
  value out of range is clamped as the editor clamps it.
- **`ot_midi_send_paced`** took 289 of 362 messages (the queue holds 127
  three-byte ones), refused the 73 that did not fit and a length of 0 and 256,
  and sent each it took once, in order, byte for byte (one 255 bytes long,
  across the queue's end), at a median 8.33 ms apart, with the firmware's own
  sends between.
- Tests: test_octatrack 28/28, test_units 30/30, test_octabam (with a
  bare-metal assembler) 21/21.

On a unit (an Octatrack MKII, 3 Oct 2026): 0.1 alone, and 0.1 with 19 of the
converted octabam mods in seven groups, flashed from the card. Every build
booted with its own version. 0.2 is not yet run on a unit, and nothing is
run on an MKI.
