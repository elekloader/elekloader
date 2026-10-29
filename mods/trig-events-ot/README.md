# trig-events (Octatrack)

The moment the frame interrupt hands a track's trig to its machine, as an
event, OS 1.40C. A mod that needs a trig at the instant it sounds (a trig
mute, a sidechain's key, a meter, a machine that starts its own voice) gets
`ev_track_trig(track)` and may veto the trig. On its own it changes nothing
the unit does. From 0.2 it also tells the mods of STOP's tail kill, as
`ev_tail_kill(record, track)` (below).

| | |
|---|---|
| needs | core-ot (checked with 0.3.1 and 0.5: it uses only the boot copier, not the bus) |
| sites | 2: `0x4000d496`, three stock instructions before the call (below), and from 0.2 `0x40004d0c` (the tail kill) |
| tables | `ev_track_trig`, `int32_t f(int32_t track)`, nonzero vetoes; from 0.2 `ev_tail_kill`, `void f(uint8_t *record, unsigned track)`, tells (`octatrack/trig.h`) |
| RAM | 144 bytes of code: with core, one 6 KB page |
| sources | [trig.s](trig.s) |

Compare [seq-events-ot](../seq-events-ot/README.md), which runs when the
sequencer's tick *builds* a track's next step, about a step before it sounds.
This runs when the sound starts.

```json
"subscribe": [{"event": "ev_track_trig", "fn": "my_trig", "order": 50}],
"requires": ["core", "trig-events"]
```

## The event

The frame interrupt (vector 0x41, level 5, every 16 samples, `0x4000aad0`)
goes through the eight audio tracks in the loop `0x4000d3fc..0x4000d55e`. For
a track whose flag byte (`0x46104d0c + track`) has bit 4 set, a trig, it looks
the track's machine up in the table at `0x400d6454` and calls that machine's
trig function, which starts the voice. trig-events puts the handlers in front
of that call:

- `ev_track_trig(track)` runs for every trig the interrupt hands a machine, in
  `order`, with `track` 0-7 (T1-T8). The MIDI tracks never come here.
- A handler that returns nonzero **vetoes** the trig: the machine's function is
  not called, nothing later in the table sees the trig, and the interrupt goes
  on exactly as for a machine that did nothing. This is what octabam's MUTE
  MODES does at the same instruction (its README reports the DT mode on its author's
  unit): no voice start, no arena bind, no position write, and a voice already sounding
  rings on under its own envelope. An observer that must see the trigs another handler
  vetoes takes a smaller `order`.
- Which trigs get here: a trigger trig and an audio track's one-shot trig. A trigless
  trig and a lock trig set no trig bit and never reach the machine. That is checked with
  the sequencer; a key's or a MIDI note's trig is not.
- The handler runs at interrupt level 5 inside the loop: a few dozen instructions, no
  firmware call, nothing that waits. It must leave the EMAC (MACSR and the accumulators)
  as it finds it: the machines' functions after it count on the mode the interrupt set
  (MACSR `0x20`, checked below). `trig.h` gives the flag byte and the machine of a track
  for a handler that wants them.

## The site

| | 1.40C | over |
|---|---|---|
| the dispatch | `0x4000d496` | `mvz.b (a1),d0; move.l d0,-(sp); move.l d3,-(sp)` |

A `jmp` over three whole two-byte instructions: the load of the call's second argument and
the pushes of its second and third (the first was pushed at `0x4000d490`). The call itself
(`jsr (a0)`, `0x4000d49c`) is left alone. At the site `d3` is the track and `a0` the
machine's function (the loop skips a machine with none before it gets here). The stub replays
the three instructions, so the stack is what the function expects, and then:

- **no handler subscribed** (the table's first word is 0): `tst.l`, `bne`, `jmp` back to the
  call. In the emulator that is 7 instructions from the site to the call where stock has
  3: four more at each trig, none at a track with no trig.
- **handlers**: it saves `d0-d1/a0-a2`, calls each handler with the track, and puts the
  registers back as stock had them at the call (not required by the C convention, but cheap
  and it means the machine's function sees what it always did). The first nonzero answer vetoes:
  `a2` comes back, `d0` is 0 (the function "did nothing", the value MUTE MODES leaves there)
  and the stub jumps to `0x4000d49e`, past the call; the three pushed arguments are popped
  at `0x4000d4ce` as always. The trig bit is cleared at the loop's end for a vetoed trig, as for
  any.

### Who else has bytes there

| | | |
|---|---|---|
| the 34 v1.0 conversions (irpina/octabam2elemod) | nothing in `0x4000d3e0..0x4000d570` | clear |
| core-ot 0.5 (PR #169) | `ev_frame_begin` `0x4000aad0`, `ev_frame` `0x4000d94e`, `ev_frame_end` `0x4000d9a6`, the arena literal `0x4000d5c2` | clear |
| machines-ot (PR #161) | owns the renderer kind table `0x400d6438` and the machine chooser's seams; a machine keeps FLEX's kind, so its trig function is FLEX's in `0x400d6454` | clear |
| ANALOG BD (octabam) | the same 21 seams and the renderer entry `0x400d6438`, none in the loop | clear |
| intercepts-ot, dspbus-ot, seq-events-ot | no site in the loop | clear |
| octabam b085c4d | LOAD METER `0x4000d310`, cxosc `0x4000d514`, EUCLID `0x4000d562`, SIDECHAIN COMPRESSOR `0x4000cae8` | clear of all four |
| octabam b085c4d | **MUTE MODES `0x4000d498`**, 6 bytes: `move.l d0,-(sp); move.l d3,-(sp); jsr (a0)` | overlaps by four bytes, on purpose |

The site starts two bytes earlier than MUTE MODES' hook, at the `mvz.b` that loads the
second argument, so that the three instructions are whole; MUTE MODES' hook is this event
(its `dt_trig` is a veto by track), and the two cannot be built together. MUTE MODES is not a
shop file; converted with `sdk.octabam --bus` its `dt_trig` becomes a handler.
No byte of any shop file is touched.

## The tail kill (0.2)

STOP, STOP, STOP holds a track's effect tails. The frame interrupt's record
builder (`0x40004bd4`) builds, for each track, the 64-byte record its DSP
instances read. The tracks the kill holds are the bits of the byte at
`0x8000184b`; for each one the builder tests the halfword at +56 of the
record, the FX2 effect id, against stock's three reverbs (ids 20-22) and, for
them, zeroes the halfwords at +24 and +34 and the byte at +53, FX2 SETUP's slot
11. `ev_tail_kill(record, track)` is told of each held track, each frame,
before that test:

| | 1.40C | over |
|---|---|---|
| the test | `0x40004d0c` | `move.w 56(a2),d0; addi.l #-20,d0` |

A `jmp` over two whole instructions (10 bytes: the jump, two nops). At the site
`a2` is the record and `d4` the track (0-7). The stub replays the two
instructions after the handlers, which run in `order`; every handler runs, none
can stop the kill, and stock's own test follows. A handler for an effect of its
own zeroes that effect's bytes in the record the same way (it checks +56
first): a byte it stores is what the next handler finds, and it goes on to the
copies of the record the DSP is sent. The builder rebuilds the record in the
next frame, so the kill's zeroing is renewed each frame while it holds. With no
handler the stub is a `tst`, the two instructions and a `jmp`: 6
instructions from the site to the next where stock has 2, at each held track.

It runs at interrupt level 5, in the builder's loop over the tracks: a few
dozen instructions, no firmware call, nothing that waits, the EMAC as you found
it, `d2-d7`/`a2-a6` as the C convention has it.

The event is Modwerk's Octatrack base's `ev_tail_kill` (repeat98/modwerk,
`sdk/machines/octatrack/HOOKS.md`) at the same site, with the same arguments
and the same stock bytes; the code is elekloader's own. Octabam's BBD
(`bbd/kill.s`) detours exactly these ten bytes (not in the shop), so it and
this event cannot be built together; BBD's kill is a handler.

## Cost

In `ot_emu`, from the site to the call: stock 3 instructions, no handler 7, two small
handlers (`tests/fixtures/octatrack-track-trig`: a count, a log, a counter read) 91 to 93.
From one handler's entry to the next took 36 counts of the emulator's 132 MHz timer. A frame is
47,891 counts; a track has at most one trig a frame, so eight at the most. The dispatch
itself does not touch the EMAC. This is the emulator's account and says nothing about a unit.

## Checked

In octabam's `ot_emu` (the ColdFire port), by
[tests/probe_octatrack_track_trig.py](../../tests/probe_octatrack_track_trig.py)
(`docs/TESTS.md`): a project of one pattern with trigger, trigless, lock, one-shot and
slide trigs on audio tracks and the sequencer running eight steps (nine machine calls,
tracks 0, 3, 0, 4, 3, 0, 2, 3, 3), with a probe mod
([tests/fixtures/octatrack-track-trig](../../tests/fixtures/octatrack-track-trig)) of two
handlers:

- **Alone it is stock.** Stock, core alone, core with trig-events and with the probe (its
  handlers change nothing): every call of the machine has the registers and the stack
  stock has (`--watch-pc` on `0x4000d49c`, the function's entry and the voice rebind inside it), the
  pending step records, the engine's frame records and flag bytes, and the MIDI out are
  the same. Core alone and core with trig-events leave the same 80 KB of engine and sequencer
  memory.
- **A handler sees what the machine would.** Every trig, in order, with the flag byte (trig
  bit set), the machine and MACSR `0x20`, the same for every trig.
- **A veto does only that.** Vetoing T1 in the first handler, T4 in the second, both, and every
  track: the machine is called for the other trigs and for no other, with stock's registers and
  stack; the voice rebind inside it runs once less for each; the pending records and the MIDI
  out are stock's; a handler after the veto does not see the trig, one before does; the trig
  bits are clear at the end as stock's.
- **Tests:** `tests/test_octatrack.py`: the site and the stock words around it, the stub's
  registers and that it names no EMAC instruction, the header's addresses against the image, the
  build, the table, and that linked with core alone it changes only its six bytes; it also links
  beside seq-events, intercepts and the DSP bus.
- **The tail kill (0.2), in `ot_emu`** (`tests/probe_octatrack_tail_kill.py`, 19 checks;
  emulator only, not run on a unit): with the held byte poked to every track and the
  sequencer running, stock passes the site 13,060 times in 2,576 frames, for six of the
  eight tracks (T1 and T4 have trigs in the fixture and leave the mask at their first;
  a held track is released at its next trig).
  - Alone it is stock: with core, with trig-events and no handler, and with two
    handlers that change nothing, every pass reaches the next instruction (`0x40004d16`)
    with stock's registers and stack (the stack's address left out: core shifts which
    task the interrupt lands on), and the MIDI out and frame records are stock's.
  - Both handlers are told of every pass, in order, with the record (a2) and the track
    (d4); with nothing held they are never called.
  - A handler's marker in slot 11 is found by the next handler; at the end of the
    run the only differences from stock are the marker, in the record and in the
    copies of it the DSP is sent (6 bytes for one track, 24 for six).
  - From the site to the next instruction stock runs 2 instructions, no handler 6,
    two small handlers 54 to 65.

## Not here

The interrupt's other points (the head, the end, after the loop) are core-ot's
`ev_frame_begin`, `ev_frame` and `ev_frame_end`. The effect of a trig after the machine has
run (what the voice got), a way to change the trig rather than drop it (its velocity, its
note), and the MIDI tracks' notes (core-ot 0.4's `ev_midi_out`) are not here.
