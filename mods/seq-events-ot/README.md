# seq-events (Octatrack)

The Octatrack's step fire as events, OS 1.40C: the same API and the same
shared part as [the Digitone's](../seq-events-dn1/README.md) and
[the Digitakt's](../seq-events-dt1/README.md), so a mod that changes what the
sequencer plays is written once for all three. On its own it changes nothing
the unit does.

| | |
|---|---|
| needs | core-ot (0.2 or newer: it uses only the hook bus's `ev_tick`) |
| sites | 4: the entry of each of the sequencer's two step builders, and the trig condition's answer in each (below) |
| tables | `ev_fire_step`, `ev_gen_trig`, `ev_fire_trig`, `ev_fire_cond`, `ev_gen_notes`, `ev_fire_notes`, `ev_fire_vel`, `ev_fire_locks` (every device's), and the Octatrack's own `ev_step_built` (below) |
| exports | `sq_clock`, `sq_track_info`, `sq_stored`, `sq_rearm` (`seq-events.h`, through `octatrack/seq.h`) |
| RAM | 4.9 KB code, 756 bytes data: with core, one 6 KB page |
| sources | [../seq-events/seq.c](../seq-events/seq.c) (every device's), [dev.h](dev.h) (the Octatrack's layer), [fire.c](fire.c) (the builders' wrappers), [seq.s](seq.s) (its sites) |

[examples/euclid-ot](../../examples/euclid-ot) is a mod to start from: the
same C as the Digitone's and the Digitakt's EUCLID, with one include.

## The Octatrack's terms

`octatrack/seq.h` gives the sizes: 8 audio tracks (0-7, T1-T8), then 8 MIDI
tracks (8-15, M1-M8); 4 notes a step; lock slots 0-29. Where the Octatrack
differs from the other two, the device layer maps it, and some things it
cannot say:

- **The Octatrack builds a step before it plays it.** The sequencer's tick, an
  interrupt, builds each track's next step at the start of the step before
  it (a track's builder: `builder(track, bank, pattern, step, n)`), from the
  pattern's masks, a timing halfword and 32 lock bytes, into a record that is
  applied when the step's time comes. So every event runs about one step
  ahead of the sound, with the pattern the step belongs to. The first step
  is built when PLAY is pressed, and a step is built again when it is
  edited (the builder's n is -1): those builds run the events too.
- **`first`** is 1 for a build, and 0 for a build of such an edit that
  repeats the one before it (the first step is built twice at PLAY). Both
  must get the same answer, as on the other devices.
- **The step clock** (`sq_clock`) follows the tick's builds only: the first
  step at PLAY and the builds of an edit leave it alone, and a restart counts
  as a wrap at the next step. The parity is the one the step was built with,
  a step ahead of the sound.
- **Trigs.** The pattern keeps one mask a kind of trig: `SQ_TRIG` is a trigger
  trig or an audio track's one-shot trig (the sample or the MIDI note
  starts); `SQ_LOCK` is a trigless trig or a lock trig (a locked step with
  no trig: locks and envelopes, no sample, no note). A trig a handler adds is a
  trigger trig; one it turns to a lock is a lock trig; the other masks and
  every kind a handler leaves alone stay as they are. A step that fires with
  nothing stored (a generated one) has no condition, no micro-timing and no
  locks: the stored ones are ignored, as they are on the other devices.
- **Conditions.** `ev_fire_cond` runs after the stock condition has answered,
  with its answer: 1 passed, 0 failed (`-1` is never given: the Octatrack folds
  its probabilities into its conditions). A handler may revive a failed
  step or fail a passed one; a step with no trig is not asked.
- **Notes** are the MIDI tracks' NOTE and NOT2-NOT4, as the step's lock bytes or the
  Part's values where it has none: `notes[0]` the note, the others
  intervals (NOT2-NOT4 are the knobs' 64 + the interval, 64 none, and an
  interval beyond the knob's range is cut). A generated trig starts at the
  Part's NOTE alone, whatever chord the Part keeps. `notes[0]` -1 plays no
  note (the velocity is set to 0, which the sequencer does not send). **An
  audio track plays a sample, not a note, and its pitch is a raw PTCH knob
  that is not a semitone count here: the notes events do not run on the
  audio tracks.**
- **Velocity** is the MIDI tracks' VEL. An audio track has none; an accent is
  a lock on its AMP VOL (slot 15).
- **Locks** (audio tracks): `SQ_SLOTS` is 30, the page-1 knobs in the
  firmware's order (PLAYBACK 0-5, LFO 6-11, AMP 12-17, FX1 18-23, FX2 24-29);
  the byte is the value's integer part (0-127; a bipolar knob stores its
  panel value + 64). A lock goes into the step the build reads, over a
  stored lock of the same slot. There are no page-2 locks. A MIDI track's
  locks are its notes, velocity and controllers; they are not offered here
  (its NOTE page is the notes and velocity events).
- **No key.** `sq_track_info` gives `root` `SQ_ROOT_NONE` and `scale`
  `SQ_SCALE_NONE`; `note` is the MIDI track's NOTE (the audio tracks' 60).
- **Re-arm.** `sq_rearm` does nothing: the first step is built when PLAY is
  pressed, so nothing can have gone stale before it.
- **Slides** look at the next step's stored masks to find where they end: a
  trig a handler adds there does not end one. The recorder trigs and the swing
  are not offered to the stages and ride with the step the build reads;
  `ev_step_built` (below) sees them in the staged record.

## How it works

The stock builder reads the step from the pattern, so seq-events lets it:
the stages run on a virtual record of the step (its trig bits, its 32 lock
bytes), and what they changed is written into the pattern for the one build
and put back when the builder returns. The builder, its timing and its
slides are the stock code's, on the step as the handlers left it.

1. At the builder's entry (a `jmp` over its first two instructions, replayed)
   `enter` ([fire.c](fire.c)) takes the build: the step the build reads
   (`ev_fire_step`: the argument on the stack is replaced), the trig
   (`ev_gen_trig`, `ev_fire_trig`) and, if they changed it, writes the masks
   (and, for a generated step, zeroes its timing halfword). It replaces the
   builder's return address with the trampoline of a frame it takes for the
   build (`sq_tramp0`-`2`).
2. After the stock condition ([seq.s](seq.s) `sq_site_acond`, `sq_site_mcond`)
   `sq_cond` finds the build's frame by the return address in the builder's
   stack frame, runs `ev_fire_cond`, and for a step that fires the stages that
   need it to fire: the notes and the velocity (MIDI tracks) or the locks
   (audio tracks), then writes the lock bytes that differ.
3. The builder returns to its trampoline, whose `sq_exit` puts back every byte
   written, runs `ev_step_built` (below) on the record the builder staged, frees
   the frame and returns where the builder was called.

Nothing else reads the pattern while that goes on but the interrupt that is
running. A build started in a task (the firmware's edit path) may be
interrupted by the tick's own: each build has a frame of its own, found by
where it returns to, so they nest or interleave in any order (three at most; a
fourth runs as stock). A task's build of the very step the tick builds at that
moment is the one case that can see the other's writes, for the few microseconds
between them. With no handler on any table, `enter` returns at once and the builder
is untouched; with a handler on `ev_step_built` alone, a build takes a frame and
nothing else is written: the trampoline only reports the record.

## The staged record (`ev_step_built`)

The Octatrack's own: `ev_step_built(track, bank, pattern, step, n, rec)`
(`octatrack/seq.h`). A builder does not play a step, it stages its record: it
writes the kind, the lengths, the time, the 32 lock bytes and the recorder trig
into the tick's staging words, and the tick copies them into the pending slot `n`
right after it returns, then clears the kind and the recorder words. The event runs
in between, for every build of every track, after the stages and with the pattern as
stored again, and `rec` points at the words: a mod that changes what a step plays
after the stock builder has decided it edits them in place, in place of hooking the
builder. `step` is the step the build read (what `ev_fire_step` answered), `n` the
pending slot, 0-2, or -1 for the first step at PLAY and for an edit of a step.

| `rec->` | the staged word | |
|---|---|---|
| `kind` | the kind | 0 nothing staged, `0x1d` a trigger trig, `0x19` a trigless trig, `0x3d` a one-shot trig, 1 a lock trig. Always current: set it to 0 and the step plays nothing |
| `locks` | 32 lock bytes | `0xff` no lock. An audio track's page-1 knobs (15 is AMP VOL), a MIDI track's NOTE page first (1 is VEL) |
| `deltas` | 32 signed bytes | audio tracks: each lock's difference to the next trig's lock, for a slide trig; null on a MIDI track |
| `length`, `span` | the length index, the length | `span` is a step divided by the index; the index is the timing halfword's length bits + 1 |
| `time` | the time offset | the trig's micro-timing and swing, 0 for neither, in the same units as `span` |
| `recorder`, `rec_time` | the recorder trig | audio tracks: its flags (`0x1000`, `0x2000`, `0x4000` for the three recorder trig masks, and `0x20`) and its time; null on a MIDI track. Always current: set it to 0 and no recording starts |

Only `*kind` and `*recorder` are current for every build. The words of one number
are shared by the audio and the MIDI track, and a build that staged nothing leaves
the last staged build's: read the others when `*kind` is not 0 (`*rec_time` when
`*recorder` is not 0). A mod cannot give a build a record it did not stage. A build
from a task (the edit path) can be interrupted by the tick's own build of the same
track, which stages the same words and does not wait for it: check `*kind` (or
`*recorder`) in the handler, as below, and keep it short.

```c
#include "octatrack/seq.h"

/* REC TRIG MUTE: T1-T4's recorder trigs are not staged */
void mute_built(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n,
                const struct sq_built *rec)
{
    if (track < 4 && rec->recorder)
        *rec->recorder = 0;
}
```

with `{"event": "ev_step_built", "fn": "mute_built", "order": 50}` in `subscribe` and
`"requires": ["core", "seq-events"]`: it names no firmware address.

### What it serves of octabam's newer upstream (b085c4d)

The three modules the exit was asked for, and what each does at the builders:

| module (site) | at the builder | the event |
|---|---|---|
| REC TRIG MUTE (`rtm_gate`, `0x4009d9a4`, audio builder) | zeroes the recorder trig bits of a muted audio track before they are staged | `*rec->recorder = 0` for that track: the same pending record, whole. (The stock gate also skips the recorder's time word; the event leaves it written, and the tick copies it unused: the pending times are stock's.) Its other five sites (the edge glyph, the keys, CC 80) are not the builders' |
| MIDI SCENES v1.0 (the shop's, from octabam 363861e: `plock`, `0x4009d1de`, the MIDI builder's last instructions) | copies the staged 32 lock bytes of the MIDI track into its snapshot and, with scenes assigned, mixes the scene over the row | `rec->locks` of a MIDI track with `*rec->kind` not 0 is that row: the whole of it. The rewrite in b085c4d (`modules/midi-scenes`) hooks no builder at all: its sequencer sites are the tick's step routine and its trig-record copy (`0x400a169a`, `0x400a19da`, `0x400a1d32`, `0x400a3c2a`, `0x400a4ba0`) and the MIDI note routine (`0x4009faaa`), which this does not offer |
| PLOCKS P2 (`plk_fill_slide`, `plk_fill_plain`, `0x4009d704` and `0x4009d8c6`, audio builder) | at the head of each path's lock fill, copies the step's 12 page-2 bytes from its table to `P2STAGE` (n = -1) or `P2PEND` (slot n) | the same table lookup by `(bank, pattern, track, step)` into the slot `n`, when `*rec->kind` is not 0, which is what reaching either site means. Page 2 has no slide, so nothing the slide path computes (the deltas) is used: **no site inside the builder is needed**. Its other playback sites mirror the stock stages outside the builders and are not served: staging to pending slot 0 (`0x4009b858`, `0x4009c038`), the pending reset (`0x4009b220`), the frame interrupt's pending to trig record (`0x4000bafe`, `0x4000b770`) and the lane (`0x4000c59e`), and its editor, clipboard and storage sites |

BLACKPYRAMIDS 303's `ot_note_hook` and `ot_oneshot_hook` (`0x4009d422`, `0x4009d4a8`)
pass the track where the builder writes the kind `0x1d` or `0x3d` and replay it; by
reading, the same call is `*rec->kind` being `0x1d` or `0x3d` at the exit (not run).
Its entry hook is the entry events'.

## The layout

The part every device shares is [../seq-events/seq.c](../seq-events/seq.c):
the tables and their two phases, the checks, the step clock, `sq_track_info`
and `sq_stored`. It knows the firmware only through the device layer,
[dev.h](dev.h), whose functions are static inline, so the one file it is
built into inlines them. What is the Octatrack's alone is in [fire.c](fire.c):
the builders' arguments, the virtual record, and the writes and their undo.

| | the Octatrack's |
|---|---|
| `dev_pattern()` | the pattern the sequencer plays: bank `0x800065bd`, pattern `0x800065be` |
| `dev_fire_begin(track, pat)` | the pattern a build reads, kept per track (`sq_fire_pat`) |
| `dev_track_len(pat, track, &master)` | the length (the pattern's `+0x8e53`, or a track's own in per-track scale: audio `+0x50`, MIDI `+0x28`), and the master length (`+0x8e50`, -1 INF) |
| `dev_step_word(pat, track, step)` | the trig bits from the masks at `+0`, `+8`, `+0x10`, `+0x18` of the track's record |
| `dev_trig_in(word)`, `dev_trig_out(word, trig)` | the bits as `SQ_TRIG`, `SQ_LOCK`, and back |
| `dev_notes_in/out`, `dev_lock_set` | a MIDI track's NOTE, NOT2-NOT4 in the step's lock bytes; an audio track's lock bytes |

The records: an audio track is `pattern + 0x91a * t`, a MIDI track
`pattern + 0x48d0 + 0x8b0 * t`; a pattern is `bank + 0x8ed8 * p`, a bank
`0x400e21e0 + 0x9b340 * b`. The masks are big-endian 64-bit values with
step s at bit s; the lock bytes are 32 a step (audio from `+0x59`, MIDI from
`+0x30`; `0xff` no lock); the timing halfwords 64 from `+0x89a` and `+0x830`.

## The sites

| | 1.40C | over |
|---|---|---|
| audio builder: the entry | `0x4009d1e8` | `lea -104(sp),sp; movem.l d2-d7/a2-a6,(sp)` |
| audio builder: after the condition | `0x4009d354` | `lea 16(sp),sp; tst.l d0` |
| MIDI builder: the entry | `0x4009cf4c` | `lea -40(sp),sp; movem.l d2-d7/a2-a5,(sp)` |
| MIDI builder: after the condition | `0x4009d036` | `lea 16(sp),sp; tst.l d0` |

The entries are `jmp` sites over two whole instructions, which the stub
replays; the others `jsr` sites over two, after the stock condition
`0x400a5164(track, step, condition, immediate)` (a MIDI track's number is its
index + 8, so both builders count 0-15 as seq-events does). None overlaps
core-ot's sites or those of the 34 v1.0 conversions of octabam
(irpina/octabam2elemod): the nearest is MIDI SCENES' at `0x4009d1de`, the MIDI
builder's last instructions, which return into the build's trampoline unchanged. In
octabam's newer upstream (b085c4d) the audio builder is the crowded one:
BLACKPYRAMIDS 303 hooks its entry, the bytes the stub here replays, and
PLOCKS P2 and REC TRIG MUTE hook it inside, to edit the record; `ev_step_built`
(above) is what those two would be rebuilt on, and until they are they stay where
they are. The event adds no site.

## Not here

Events of the Octatrack's own that mods ask for, none of them a step mod's:
the voice-time dispatch in the frame interrupt (a trig's moment, not its
build), the sequencer's MIDI packets (core-ot 0.4's `ev_midi_out` sees them), and
the pattern and Part changes. They are core-ot's or a companion's to add, each at
its own sites. Of the staged record, `ev_step_built` offers the builders' arrays,
not the stock stages after them: the staging to pending copies at the transport
start, the pending reset and the frame interrupt's trig record (PLOCKS P2 mirrors
all of them).

## Checked

In octabam's `ot_emu` (the ColdFire port), by
[tests/probe_octatrack_seq_events.py](../../tests/probe_octatrack_seq_events.py)
(`docs/TESTS.md`): a project of one pattern with a trigger, trigless, lock,
one-shot and slide trig on audio tracks, and trigs and locks on MIDI tracks,
the sequencer running eight steps, with a probe mod
([tests/fixtures/octatrack-seq-events](../../tests/fixtures/octatrack-seq-events))
that has a handler for every stage:

- **Alone it is stock.** Stock, core alone, core with seq-events, and with a
  probe whose handlers change nothing: the same pending records (kinds, times,
  lock bytes, both families), the same pattern memory afterwards and the same
  MIDI out, byte for byte. Core alone and core with seq-events also leave the
  same 80 KB of engine and sequencer memory (stock differs from either by core's
  own few bytes: the arena's page count).
- **Each stage does what it says.** A Euclidean rhythm on T1 adds a trig on
  steps with none, which fires with no stored locks; a handler that drops
  trigs does; a reversing `ev_fire_step` plays the mirrored step's trigs; a
  condition forced to pass revives a step the stock condition failed, one forced to
  fail ends one; `ev_fire_locks` sets AMP VOL and PLAYBACK locks over stored
  ones and puts them on lock trigs it makes; on a MIDI track the notes (a
  transposition and a chord), the velocity and a generated step arrive in the
  note-on bytes the unit sends, and a step given no note sends none.
- **Everything is put back.** After every run the pattern's 36,568 bytes are the
  ones it started with, with a handler on every stage of every track. (That run
  also calls the firmware's edit routine, `0x4009da20`, 236 times from outside, and
  the tick's records are the same; but while the sequencer plays that routine builds
  nothing, and a watch on the builder's entry counts as many entries without the
  calls as with them, so they show nothing about a build from a task. A build inside
  a build is checked below, with `ev_step_built`.)
- **The clock** flips its parity at each wrap of a 4-step pattern.
- **`sq_track_info`** reports a track's own length in per-track scale (audio
  and MIDI), the master length, and the MIDI track's NOTE.

`ev_step_built` is checked the same way, by
[tests/probe_octatrack_step_built.py](../../tests/probe_octatrack_step_built.py), with a probe
([tests/fixtures/octatrack-step-built](../../tests/fixtures/octatrack-step-built)) whose handler
logs what it is given and, by a configuration, does what the three modules do at the builders:

- **Alone it is stock.** With a handler that changes nothing, on its own and beside the stage
  handlers: the same pending records (kind, locks, time, length, span, slide steps, recorder flags
  and time), pattern memory and MIDI out as stock's, with and without recorder trigs in the
  pattern.
- **It is given what the tick later copies.** The arguments (bank, pattern, the step read, n,
  also under a reversing `ev_fire_step`); the pointers, which are the builders' own arrays, for
  each kind of track and for n = -1; the kind, locks, length, span and slide steps of every
  slot-0 build equal the pending slot 0 the tick filled; the time word is the micro-timing and
  the swing; the recorder flags are the three masks'. The pattern is as stored when the event
  runs, also while a stage has added locks to the build.
- **The three modules' work, on the record.** Cancelling a step (`*kind = 0`) leaves no pending
  record and sends no note; a lock over a stored one arrives in the pending record and in the
  note-on velocity, the first step's too; a time offset moves exactly the staged times; a zeroed
  recorder word leaves none in the pending slots and the rest as stock; a copy of a MIDI row is
  the row the builder wrote; a table keyed by `(bank, pattern, track, step)` into the slot `n`
  is right at every dump, with n = -1 too.
- **A build inside a build.** The probe's handler starts the stock builder for T8 from inside (120
  times): each runs its own event with its own arguments, the tick's events and records are
  those of a run without them, and the pattern is put back.

PLOCKS P2, MIDI SCENES and REC TRIG MUTE themselves are not rebuilt on the event here: the probe
does their work at the same points, on the same words.
