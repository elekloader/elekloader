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
  - **0.7.1 checks the copy.** It reads the reserve back (through the alias)
    and hashes it: a rolling x33 over the image's 32-bit words from the seed
    5381, the same function as the linker's `run_hash` (elekloader/link.py).
    The linker puts the hash of what it linked in the symbol `__run_hash`.
    The mods' sites already point into that image, so a copy that is not the
    linked one cannot be run around; `.boot` halts instead, in
    `core_boot_fatal`, with interrupts masked and before `.bss` is cleared or
    any mod code has run. The stock file recovers a unit that stops there
    (the bootloader is never changed). `ot_boot_info` (3 words: state, the
    linked hash, the hash of the copy; `octatrack/core.h`) records what `.boot`
    found, `OT_BOOT_OK` or `OT_BOOT_FAIL`, for an emulator or a debugger to
    read. A mod that runs has seen it say OK. What this does not catch: a copy
    that is right at boot and changes later, and a unit that stops before the
    boot site. Octabam's loader hashes its payloads the same way and stops in
    a loop of its own.
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
- **0.6: the project, the card and battery RAM** (`store.s`, `store.c`,
  `core.h`). The firmware's engine task (priority 1) runs the project jobs,
  one at a time, from its queue (0x460d17ce). Core takes two places in its
  loop, and the routines those jobs call. Every caller reaches those
  routines directly; octabam's STORE module took the census (MIT):

  | event | site (was) | when |
  |---|---|---|
  | `ev_project(how, dropped)` | 0x4008484e (`pea 0x460d17ce`, the receive every job goes back to), a jmp; 0x4008485e (the job switch) notes the job | when a job that loads a whole project (3, 4 LOAD PROJECT, 5 the power-up, 19 RELOAD) or makes one (9, 10, 13) has ended; the same prototype and constants as core-ar2's and core-a4's |
  | `ev_banks_load(pre, mask)` | 0x40090504 and 0x400905d4, the bank loads, wrapped | before and after |
  | `ev_banks_write(pre, mask)` | 0x400918aa (inside the bank writer) and 0x400917c8 (around it) | before the first bank file, and after |
  | `ev_project_store(pre)`, `ev_project_reload(pre)` | 0x4008ee74 and 0x4008f180, wrapped | SAVE PROJECT's `.work` to `.strd` copy, RELOAD's copy back |
  | `ev_cs1_bank(to, bank)` | 0x4000faf0 (before) and the power-up's call at 0x40025808 (after) | the bank's working copy into battery RAM, and back |
  | `ev_part_saved(part)`, `ev_part_cleared(part)` | 0x4004a9c4 (Part Save's tail), 0x4004a9d0 (Part Clear's entry) | stock's Part Clear ends in its Part Save |

  - **`ot_engine_post(fn, arg)`** runs a mod's function on the engine
    task, where the firmware does its own card I/O. Core's own job type is
    0x42 (stock's are 0..45). The file calls (`ot_file_open` and the rest)
    are for there.
  - **Project and card data.** A mod contributes the Digitone's descriptor
    (`{tag, size, data, loaded}`) to `core_projdata` or `core_carddata`.
    - **Where:** core keeps each block in a file pair the stock OS
      ignores: `<project>/elekloader.work` and `.strd`, and
      `/ELEKLOADER/card.work` and `.strd`.
    - **When:** `.work` is written about 2 s after a mod calls
      `core_projdata_dirty`, and at SAVE PROJECT. `.strd` is copied where
      stock copies its own, and copied back on RELOAD.
    - **Damaged files:** the pair rules are octabam's (KITS and BRAIN): a
      valid `.work`, else a valid `.strd`. A file that reads back as
      neither is never written over. At the next encoder turn, core asks
      whether to keep it (mods run without it) or start fresh, with the
      old file copied to `.bak` first.
    - **Records of mods not in the build** are kept and written back.
  - **Battery RAM.** `ot_sram` claims get addresses in 0x100f859c..0x100fff00,
    the range stock never touches, at main's end. A claim keeps its bytes
    from boot to boot while the build keeps the same claims in order.
  - **Settings.** Core declares `ot_settings` for the settings companion
    (`core.h`, `struct ot_setting`); without it, each setting stays at its
    default.
  - **The cost.** With no block and no claim in the build, core reads and
    writes nothing. Its buffers (a 64 KB file buffer, the kept records and
    a file context) put about 100 KB in `.bss`, about 17 of the reserve's
    6 KB pages.
  - A stock routine that returns a pointer returns it in d0, where GCC's
    m68k ABI expects a0: declare one as returning an integer, and cast
    (`PROJDIR` in `store.c`).
- **0.7: the voice** (`voice.s`, `core.h`). The frame interrupt's builder
  turns each track's mailbox word (0x46c80354) into the DSP command byte
  (0x46104d15), the live lanes and the 64-byte records the DSP receives; the
  voice struct's routines (0x40006820 reset, 0x4000f450 bind) tie a track to
  its sample. Octabam's synth machines (CX OSC, SYNTH, OCTA303,
  BLACKPYRAMIDS-303), MUTE MODES, TEMPO SYNC, PLOCKS P2 and the recorder
  fixes each detour a few of those places, and only one module can. Eight
  events, all `jmp` sites whose stubs replay what they displaced:

  | event | site (was) | when | handler |
  |---|---|---|---|
  | `ev_voice_start` | 0x4000c63c (`lea 0x46104d0c,a0`), the builder | a DSP command was posted for the track this frame (the byte is not 0) | `unsigned f(trk, cmd)`: returns the command to use |
  | `ev_voice_release` | 0x4000b4f6 (`move.b 0x80001828,d4 ; move.b d4,118(sp)`), the builder | the track's mailbox word has bit 6, the AMP release; the release bytes 0x80001828/9 are read after the handlers | `void f(trk, word)` |
  | `ev_voice_kill` | 0x40006862 (`move.l d1,d0 ; lsl.l #3,d0 ; movea.l d0,a0`), in 0x40006820 | a voice was reset, with every interrupt masked: STOP, a pattern change, the sample loaders, the preview, a voice that has played to its end, a track with no sample | `void f(trk)` |
  | `ev_voice_bind` | 0x4000f4dc (`move.l a5,4(a2) ; move.l a4,8(a2)`), in the binder | it is about to write the voice's two arena pointers | `int f(trk, voice)`: nonzero leaves them |
  | `ev_voice_seek` | 0x4000f82c (`movea.l 50(sp),a3 ; move.l a0,56(a3)`), in the binder | it is about to count a (re)start of the voice (+0x90) | `int f(trk, voice, same)`: nonzero leaves the counter |
  | `ev_trig_apply` | 0x4000c59e (`move.l a4,d0 ; move.w d0,ccr ; bmi`), the builder | where the lock restore and apply paths join | `void f(trk, flags)` |
  | `ev_dsp_trig` | 0x40004c72 (`moveq #3,d0 ; and.l d1,d0 ; beq`), the voice record writer | a track's per-trig flag bits are about to be built into its DSP frame word | `int f(trk, flags)`: nonzero builds the word as for a frame with no trig |
  | `ev_dsp_record` | 0x40004d40 (`move.b 3516(a0),d2 ; ext.w d2 ; move.w d2,56(a2)`), the same writer | the track's 64-byte record for the DSP has its FX ids; a store here lands last in the frame | `void f(trk, rec)` |

  - **Why these addresses.** Octabam's modules detour 0x4000c634, 0x4000b51a,
    0x4000685c, 0x400795ba, 0x40079816, 0x4005a848 (SYNTH, in
    octabam2elemod v1.0) and 0x4000f834 (FLEX SEEKBIND). Each event sits on
    the instruction beside the one they take, so those files still link and a
    build with both sees both (checked below).
  - **Where they run.** All but `ev_voice_kill` run in the frame interrupt
    (level 5; the binder is called only from the renderer and the packer's
    kind table, both in it). The kill runs in tasks as well, always with
    every interrupt masked (level 7). Handlers keep to a few dozen
    instructions and do not touch the EMAC: the interrupt has saved the
    interrupted task's state before these places, but the builder goes on
    using the EMAC itself after them.
  - **What they cost.** `ev_dsp_record` runs 8 times a frame; the others run
    on a trig, a release or a reset (14 trigs in 12,000 frames of two
    tracks at 120 BPM). With no handler a stub costs a test of its table
    (core alone: +36 counts on a 12,300-count frame, 0.3%).
  - **`ev_voice_bind`'s veto leaves a voice unbound.** The renderer finds
    nothing left to play and resets the voice (`ev_voice_kill`, from the
    renderer's end-of-sample path at 0x40008110) every frame: one or two
    kills a frame on that track. It is what octabam's
    MUTE MODES does for a silenced track (its `mt_rebind`, there an untested
    guess about what the DSP watches). A mod that takes it should know its
    voice is not just muted.
  - **`ev_voice_seek`'s veto** is FLEX SEEKBIND's: the voice's start counter
    is left alone on a re-bind of the same sample, so the frame builder does
    not send the voice as new and the DSP seeks it.
  - **Not here: `ev_machine_set`.** Octabam's machines detour the places
    where a track's machine or slot is committed (0x400795ba, 0x40079816,
    0x4005a848). That event belongs in machines-ot (0.2), not in core.
    machines-ot already owns the chooser's and SRC SETUP's commits right
    after those places (0x4007981e, 0x4005a616, 0x4005a850). It decides
    there whether the commit happens (`admit_track`, `assign`) and what the
    final byte is, and it reads a track's machine from the Part's signature,
    not from the machine byte alone. An event at the stock writes would run
    before that decision, with a byte machines-ot may still refuse.
    machines-ot 0.2 fires it after the commit, with the Part, the track and
    the old and new rows. The five source machines are its only consumers,
    and nothing in a build adds machines without it.
  - **Not here: MUTE MODES' veto of the reset** (0x40006844). It is a veto
    before the reset routine's body runs, which has to unwind that routine's
    two saved registers; and the trigs of the test project (below) bind and
    restart a voice without passing through that routine. The other places
    MUTE MODES hooks for a silenced track that are voice events are here:
    the binder's pointer stores and its start counter, the DSP word's
    flags, and the command byte (a handler may return 0). What they do to
    the sound is not checked: the runs below have no DSP.
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
- from 0.7, the voice: SYNTH's detours (0x4000c634, 0x4000b51a, 0x4000685c,
  0x400795ba, 0x40079816, 0x4005a848) and FLEX SEEKBIND's (0x4000f834) are
  the instructions beside the voice events' sites (0x4000c63c, 0x4000b4f6,
  0x40006862, 0x4000f82c), and `jmp` to the instruction an event's stub is
  at (SYNTH's start rewrite ends in `jmp 0x4000c63c`), so a build with both
  sees both.

So the 34 mods of irpina/octabam2elemod v1.0 link with 0.2 and 0.4 as they did
with 0.1 (checked for 0.4 and 0.7: each alone, and the shop's two largest
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

0.6, in ot_emu with a probe mod (tests/fixtures/octatrack-store): a project
block, a card block, a battery-RAM claim, and a record of every lifecycle
event. Its handler adds one to each block and marks it changed at every
`ev_project`:
- **A first boot on a card with an empty project folder.** LOAD PROJECT ran
  as two loading jobs, and each fired `ev_project(LOADED, ...)` at its end.
  Both blocks came with `loaded(0)`, dropped = project | card. The bank loads
  fired before and after. 2 s later core wrote `OCTABAM/ONEAUX/elekloader.work`
  and `ELEKLOADER/card.work`, valid containers holding the counters. The CS1
  claim got 0x100f85b4, behind its directory.
- **The next boot from that card** loaded both, `loaded(1)`, and the counters
  carried on. RELOAD (the stock routine the menu's YES calls) fired
  `ev_project_reload` before and after, then `ev_project(LOADED)` again.
- **SAVE PROJECT** (the stock routine the menu's YES calls) fired
  `ev_banks_write` and `ev_project_store` before and after, and core copied
  `card.work` to `card.strd`. The emulator's card image comes out of a full
  SAVE PROJECT damaged on stock as well (its project folder gone), so the
  project's `.strd` copy is checked through the card pair, which takes the
  same path.
- **A damaged file.** With `elekloader.work` corrupted and no `.strd`, core
  refused the pair: zeros, `loaded(0)`, no write. The next encoder turn
  opened the prompt, and YES started fresh: the old file was copied to
  `elekloader.work.bak`, and new `.work` and `.strd` files were written.
- **Core alone matches stock.** After a boot, LOAD PROJECT, an encoder turn
  and a page key, the card image is byte for byte stock's and CS1 is
  untouched.
- **Every one of the 34 shop mods** links with 0.6, each alone and in the
  shop's two largest combinations.
- Tests: test_octatrack 30/30 (two new: the store's sites, with a scan of the
  image for branches into them, and the probe's tables), test_units 30/30.

0.7, in ot_emu (the checks are a probe mod, tests/fixtures/octatrack-voice,
that logs each event and checks its arguments against the firmware's own
memory, and a project of its own: T1 FLEX and T2 STATIC, each playing a
looped two-second sample in slot 1, trigs on A01 steps 1, 5, 9 and 13 (T1)
and 3 and 11 (T2), 120 BPM, `--sequencer --internal-clock`, no DSP, so these
are CPU-side checks and not audio):
- **Core alone matches stock.** After 4,000 frames of playback with trigs
  (and again after 8,000 with a release posted, STOP and PLAY called and a
  reset called), the current bank's RAM (635,712 bytes), the CS1 window
  (729,088), the engine's RAM (26,688: the voice structs, the mailboxes, the
  lanes, the DSP records), both frame buffers and the sequencer's I/O words
  are byte for byte stock's. Core 0.6, and core 0.7 with a probe that
  subscribes to every event and changes nothing (the 4,000-frame run), give
  the same bytes.
- **Registers and stack.** At the instruction each of the ten stubs goes
  back to, the registers (d0-d7, a0-a6) and the top five stack words are
  those of stock in all 24,018 hits: 21 of them for the start, release,
  reset, bind, seek, trig-apply and flag-bits stubs, exactly (stack address
  too); the other 23,997 for the two writer stubs (every frame, every track),
  where only the stack's address differs, in 160 of them, because core's
  boot shifts which task a frame interrupt lands on.
- **Every event, 12,000 frames** (14 trigs: T1 nine, T2 five; a release
  posted on T1 at frame 3,000 by storing 0x40 in its mailbox word; the
  reset called for T4 at frame 6,000; `pr_bad` counts an argument that does
  not match the firmware's memory, and stayed 0):

  | event | seen | per track | level |
  |---|---|---|---|
  | `ev_voice_start` | 14 (commands 0x10..0x1e) | T1 9, T2 5 | 5, in the frame |
  | `ev_voice_release` | 1 (word 0x40) | T1 | 5 |
  | `ev_voice_kill` | 1 | T4 | 7, from the calling task |
  | `ev_voice_bind` | 14 | T1 9, T2 5 | 5 |
  | `ev_voice_seek` | 14 | the first of each track new, the rest the same sample | 5 |
  | `ev_trig_apply` | 14 (flags 0x23) | T1 9, T2 5 | 5 |
  | `ev_dsp_trig` | 14 (flags 0x1b or 0xb) | T1 9, T2 5 | 5 |
  | `ev_dsp_record` | 95,992 | 8 a frame, every frame | 5 |

  `voice` is 0x800049d8 + 168 t, `rec` is 0x80000110 + (the buffer select
  << 9) + 64 t. STOP (calling the stock routine at frame 3,000) reset the
  two sounding voices, T1 and T2, from the task, at level 7; ordinary trigs
  reset none.
- **Changing and taking** (the probe's controls, 12,000 frames):
  - `ev_voice_start` chain: T1's nine commands were changed by the first
    handler; the second saw the change in all nine, and the command array
    held the final byte after the frame in every case.
  - `ev_voice_seek` taken on T1: its start counter was nine lower than
    without (78, where 87), and T2's was not.
  - `ev_dsp_trig` taken on T2: the flag bit (0x80) that marks a trig in the
    DSP word was set for T1's nine trigs and none of T2's five.
  - `ev_dsp_record`: a byte stored at record offset 0x1b of T6 every frame was
    still there at the end of the frame in 11,999 of 11,999 frames.
  - `ev_voice_bind` taken on T2: its arena pointers stayed 0. The voice
    stayed unbound, and the renderer reset it: 22,619 resets of T2 in 12,000
    frames, each at level 7 in the frame, all from the renderer's
    end-of-sample call (0x40008110: 4,621 of 4,621 in a 3,000-frame run).
- **With the shop's SYNTH and FLEX SEEKBIND linked** (octabam2elemod v1.0,
  which detour the instruction beside four of the sites) the same run gave
  the same counts and arguments, and FLEX SEEKBIND's effect showed: at the
  last `ev_voice_seek` the start counters were 73 and 73 where they had been
  80 and 76.
- **The frame meter** over those 12,000 frames: core 0.6 mean 12,313 counts
  a frame (28.8% worst), core 0.7 alone 12,349 (29.0%), with the probe's eleven
  handlers (it logs and compares) 13,032 (31.2%), none late or over.
- **Every one of the 34 shop mods** links with 0.7, each alone and in the
  shop's two largest combinations (the 15 mods that are not USB or SCENES P2,
  with USB MIDI and with a USB IO mod).
- Tests: test_octatrack 32/32 (two new: the voice sites, with a scan of the
  image for branches into them and the stock instructions the stubs rely on,
  and the probe's tables; with the bare-metal binutils 31/32, where
  `test_core_builds_and_links` fails on 0.6 too), test_units 30/30,
  test_octabam (bare-metal binutils) 21/21.

0.7.1, in ot_emu (tests/test_octatrack_boot.py, docs/TESTS.md, "The boot gate"):
- core alone, with hello-marker-ot, and with intercepts-ot and hello-marker-ot:
  `.boot` ran once, the handoff was reached, the reserve holds the linked RAM
  image byte for byte, `.bss` holds zeros apart from `ot_boot_info` and the
  meter's counts-per-frame, and the hash `.boot` took is the linker's
  `__run_hash` (recomputed in the test, not by `link.run_hash`);
- one byte of the image wrong at its first word, its middle and its last (a
  planted copy): `.boot` halts in `core_boot_fatal` (the emulator calls the loop
  a spin there), `ot_boot_info` says `OT_BOOT_FAIL` with the linker's hash and
  the planted copy's, and the handoff is never reached;
- the pages the reserve gave back, up to the end of the profile's `ddr`, hold
  zeros at the handoff;
- the checks fail when the code is wrong: with the seed changed in `.boot`
  the clean boots halt, and with the compare removed the planted copies are
  run.

On a unit (an Octatrack MKII, 3 Oct 2026): 0.1 alone, and 0.1 with 19 of the
converted octabam mods in seven groups, flashed from the card. Every build
booted with its own version. 0.2 is not yet run on a unit, and nothing is
run on an MKI.
