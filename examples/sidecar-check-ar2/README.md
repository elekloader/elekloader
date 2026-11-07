# sidecar-check (Analog Rytm MKII)

A test for the [core-ar2](../../mods/core-ar2) 0.11 calls the Sidecar mods
use, checked in RAM: each part of 0.11 adds what it needs here, and the
emulator and a unit's readback read the records after the magics `SCK1`
to `SCK4`.

## Part 1: locks the mod keeps itself (core 0.11)

A SIDECAR CHECK page behind the LFO key and a SIDECAR CHECK FX page behind
the FX LFO key (page ids 20 and 21) put ids 560..563 on knobs A..D. One
`core_params` entry answers for them without a trig (a value of 0..127 each,
`sc.base`), and one `core_param_locks` entry keeps their p-locks in RAM,
per id, track (0..12) and step (`sc.lock`):

| call | what the check mod does |
|---|---|
| `get` | the step's lock, or -1; counted, the last one kept (`sc.last_get`) |
| `set` | the lock set to the value |
| `turn` | the lock moved by the delta, from the value without one; with `CORE_LOCK_RECORDING` (live recording) the lock is the knob's value |
| `clear` | the lock gone |
| `label`, `name` | SCA..SCD, SC KNOB A..D |
| text (`core_params`) | the value, and with a trig held `L` and the held step (1..64) from `core_held_step`: `L5 64` |

Every call but `get` and text goes into a ring of 64 (`sc.e`, at
`sc.n % 64`), with its kind (0 get, 1 turn, 2 set, 3 clear, 4 text, 5 the
knob's turn without a trig), id, track, step (with bit 8 for live
recording), argument and answer; `sc.count` counts each kind. On each
`ev_tick` it notes track 1's trigs and lock trigs on steps 1..16 in the
current pattern (`sc.trigs`, `sc.lock_trigs`, bit 0 for step 1), so a check
can tell what a held step left behind.

## Part 2: step tags (core 0.11)

The record after the magic `SCK2` (`sc2`, tags.c) owns tags through
`core_step_owners` (`sc_owner`). On each `ev_tick` it shows its owner
number, `core_step_tables`, the pattern edited (`core_step_pattern_now`)
and the tags of track 1's steps 1..16 there (`now`) and in the pattern
`watch` names (`seen`; -1 for none), and it logs every `ev_steps` in a
ring of 64 (`e`, at `n % 64`; `kinds` counts each kind). A command word
`cmd` (and `arg`), written from outside, acts on the next `ev_tick`, then
goes back to 0 (`done` keeps it):

| `cmd` | what |
|---|---|
| 1 | tag steps 1 and 6 of track 1 in the pattern edited: 0x101, 0x106 |
| 2 | measure stock's heap: the largest block operator new gives, and the total in up to 64 blocks, all given back |
| 3 | tag every step of patterns 0..`arg` - 1: `(pattern << 10 \| track << 6 \| step) & 0xffff` |
| 4 | clear every tag of the 128 patterns |
| 5 | walk the tags (`core_step_tags`): those in patterns, those in copies, the handles' sum |

## Part 3: moments (core 0.11)

The record after the magic `SCK3` (`sc3`, moments.c):

- `ev_sysex_in` takes every message that starts F0 7D (`sx_taken`) and lets
  the rest through (`sx_passed`, `sx_passed_elektron` for F0 00 20 3C);
  `sx_len`, `sx_iface` and `sx_head` keep the last one seen.
- each `ev_tick` it notes `core_current_kit`, `_track`, `_pattern`, their
  items and `core_project_name` (`name`); `ev_project_save` copies the name
  at pre 1 (`name_pre1`) and pre 0 (`name_pre0`).
- every `ev_kit` (kind 1) and `ev_project_slot` (kind 2) goes into a ring
  of 32 (`e`, at `n % 32`).
- two storage fields marked `"handle": true`: `sc_h_project` (project, 16
  bits) and `sc_h_pattern` (pattern, 12 bits).

A command word `cmd` (and `arg`) acts on the next `ev_tick` (with `arg`'s high half, that many ticks later):

| `cmd` | what |
|---|---|
| 1 | `arg` note-ons through `core_midi_try_send` from the UI task (`sent`, `refused`); a refused one is tried again on the next tick |
| 2 | `arg` note-ons from the audio interrupt (`ev_render_in`), into core's ring (`irq_sent`, `irq_refused`); a refused one is tried again on the next interrupt |
| 3 | the project's handle = `arg`, the current pattern's = `arg` + 1 |
| 4 | walk the handles (`core_handle_fields`): `walk_n`, `walk_sum`, `walk_fields` (bit 0 the project's, bit 1 the pattern's) |
| 5 | one SysEx F0 7D .. F7 of `arg` bytes (4..64) through `core_midi_try_send` |

## Part 4: the sequencer (core 0.11)

The record after the magic `SCK4` (`sc4`, seq.c):

- every `ev_seq_step` goes into a ring of 64 (`e`, at `n % 64`) with the
  tick, both masks (`CORE_SEQ_FIRST` shown as bit 15), `core_track_step`
  of tracks 1 and 9, `core_transport` and `core_pattern_now`; the PLAY
  seed counts in `seeds` (its trigs in `seed_trigs`), every other call in
  `step_events` and the per-track `steps_n` and `trigs_n`.
- `ev_seq_tick` counts `ticks` and keeps the getters' last values:
  `state`, `now`, `next`, `tempo`, `speed[13]`, `step[13]`.
- every `ev_transport` goes into a ring of 8 (`tr`, at `tr_n % 8`) with
  the tick.
- two more `ev_seq_step` subscribers do a little work only when `subs`
  asks for them (`work` counts it), for the cost with 1 and 3.

A command word `cmd` (and `arg`) acts on the next `ev_tick`:

| `cmd` | what |
|---|---|
| 1 | `subs` = `arg`: how many `ev_seq_step` subscribers work (1..3) |
| 2 | the counters and rings back to 0 |
| 3 | `quiet` = `arg`: 1 makes `ev_seq_tick` only count (no getters), for the cost |

## Part 6: payloads (core 0.11)

payload.s is a 1 MB `.payload` of 4,096 blocks of 64 words: the block's
number (xor 0x5041594c), 48 xorshift32 words and a 15-word table, so the
OS file still packs. The record after the magic `SCK6` (`sc6`, payload.c)
holds what `core_payload` answered for the mod's id (`id`, `addr`, `len`),
the mod's own symbol beside it (`own`, `own_len`), the entries in
`core_payloads`, and each readback's count and wrong words (`verifies`,
`words`, `bad`, `first_bad`, `total_bad`). It reads the payload back at
the first `ev_tick` and on `cmd` 1.

## Part 7: the interrupt send and the step getters (core 0.11)

The record after the magic `SCK7` (`sc7`, send.c):

- each message is a SysEx F0 7D 37, the message's number in four 7-bit
  bytes, then `(number + i) & 0x7f` for each byte i up to F7, so a USB
  capture shows order, loss and damage. `sent` and `refused` count
  `core_midi_try_send`'s answers; core's own record `MIRQ` counts the
  interrupt path (at once, queued, drained, refused) and its longest time
  at level 5.
- `ev_seq_tick` keeps `core_track_length` of the 13 tracks (`length`).

A command word `cmd` (and `arg`) acts on the next `ev_tick`:

| `cmd` | what |
|---|---|
| 1 | one message of `arg` bytes (8..128) at each `ev_seq_step`, from the clock's interrupt; 0 stops |
| 2 | the counters back to 0 |
| 3 | `core_step_trig` of every step of the pattern playing: bit s of `note[t]` and `lock[t]` (two words, steps 0..31 and 32..63); out-of-range calls must answer -1 (`bad` otherwise) |
| 4 | one message of `arg` bytes at each of the next 96 `ev_seq_tick`s, playing or stopped |

## Over MIDI (0.8)

For a unit run, which can't peek or poke RAM, remote.c takes SysEx
commands over USB and sends the records back (all start F0 7D 53 43):

| message | what |
|---|---|
| in `01 <part> <cmd> <a0..a4> F7` | part 2..4, 6 or 7's `arg` = a0 << 28 \| a1 << 21 \| a2 << 14 \| a3 << 7 \| a4, then its `cmd` |
| in `02 <part> F7` | send part 1..4, 6 or 7's record back (from 0.9 there is no part 5) |
| out `03 <part> <off, 3 x 7 bits> <n> <2n nibbles> F7` | n bytes (at most 48) of the record from off, each byte as two nibbles, high first |
| out `04 <part> <size, 3 x 7 bits> F7` | the record's end |

One record goes out at a time, up to 8 messages an `ev_tick`; a request
while one is going out is dropped. The record `SCRM` counts the commands,
dumps, messages sent and refused. These messages are taken before part 3's
F0 7D count. In the emulator a command set part 7's ticks sends and the
records of parts 1, 4, 6 and 7 (7,768, 1,032, 68 and 280 bytes) came back
whole.

## Building

```bash
python -m elekloader.sdk.build examples/sidecar-check-ar2 --stock <Rytm 1.74 .syx>
```
