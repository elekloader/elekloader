# intercepts

Places in the Octatrack's firmware (OS 1.40C) that a mod intercepts, one
mod at a time, as events: MIDI notes on a listening track, the CHROMATIC
keyboard, p-locks and knobs, the settings and the project file. A companion
beside the core (core-ot 0.3.1 or newer), like machine-pages on the
Digitakt mk1: it is only in a build when a mod requires it.

Without it, a mod that needs one of these places patches it, and no other
mod can have it. With it, the place has one owner, and a mod adds a
**record** to the event instead. The events, their sites, kinds and
arguments are the ones Modwerk's Octatrack base gives the same names (it
calls them "take" events: repeat98/modwerk,
`sdk/machines/octatrack/EVENTS.md`), so a record means the same thing on
both. The code is elekloader's own (GPL-2.0-or-later).

On its own it changes nothing the unit does.

## A record

Each event's table holds 8-byte records, `{claims, entry}`, in (order,
mod id) order:

```c
struct intercept { int (*claims)(unsigned arg); void *entry; };
```

The first record whose `claims` is 0, or returns nonzero for the call (the
C convention), intercepts it:

- its `entry` runs with every register, the condition codes and the stack
  as they were at the site, as if the site had jumped there (a **jmp**
  event) or called it (a **jsr** event: it returns with `rts`, past the
  site). It does what the site's stock instructions did, as a detour does,
  and goes back to stock itself;
- with `entry` 0, the stock instructions run at once.

When no record intercepts the call, the stock instructions run and stock
goes on. In `mod.json`:

```json
"requires": ["core", "intercepts"],
"contribute": [{"to": "ev_knob_store", "order": 50, "data": "0000000000000000",
                "relocs": [[4, "abs32", "sym:my_knob", 0]]}]
```

`sdk.octabam --intercepts` writes these records for octabam's modules: a
detour at one of these sites becomes `{0, the detour's label}`, which
intercepts every call and runs the detour's code exactly as before.

## The events

| event | site | kind | claims' argument | when |
|---|---|---|---|---|
| `ev_midi_note_on` | 0x4000e746 | jmp | d2, the track | a MIDI chromatic note-on reaches a listening track, before stock posts its trigger and PTCH |
| `ev_midi_note_off` | 0x4000dfd4 | jmp | d2, the track | a MIDI chromatic note-off, before stock compares it with the track's held note |
| `ev_octave_key` | 0x40045918 | jmp | the current track | FUNC + LEFT or RIGHT in CHROMATIC, before stock toggles its octave word |
| `ev_keyboard_draw` | 0x40044968 | jmp | none | the CHROMATIC drawer reads stock's octave word |
| `ev_chromatic_release` | 0x4004fbde | jmp | none | a CHROMATIC key is released |
| `ev_chromatic_held` | 0x4004fbfe | jmp | none | a key is pressed while another is held |
| `ev_chromatic_pitch` | 0x4004fc58 | jsr | none | a key's index becomes a raw pitch |
| `ev_chromatic_trig` | 0x4004fc94 | jmp | none | a key trigs the voice |
| `ev_chromatic_record` | 0x4004fce0 | jmp | none | a key goes to the live recorder |
| `ev_chromatic_recorded` | 0x4004fd06 | jmp | none | a key was recorded |
| `ev_chromatic_locked` | 0x4004fd20 | jmp | none | a recorded key got its PTCH lock |
| `ev_chromatic_index` | 0x40050254 | jmp | none | a key's index is computed from stock's octave |
| `ev_plock_store` | 0x40050e60 | jsr | none | the p-lock editor stores a lock byte |
| `ev_knob_store` | 0x40055170 | jsr | none | a knob turn stores a Part byte |
| `ev_settings_sanitise` | 0x40010212 | jsr | none | stock sanitises the battery-RAM settings block |
| `ev_project_defaults` | 0x40025ac2 | jsr | none | stock resets a project's settings to their defaults |
| `ev_project_load` | 0x400866cc | jmp | none | a project load starts |
| `ev_project_line` | 0x400867a2 | jmp | none | the project loader meets a line |
| `ev_project_write` | 0x400888aa | jmp | none | the project writer writes the settings lines |
| `ev_midi_note_key` | 0x4009fb58 | jmp | d7, the MIDI track 0-7 | a MIDI track's note, from its trig, chord or arpeggiator, is about to be corrected to the track's KEY and sent |

Each runs in a firmware task (the UI, MIDI or project file), after core's
`.boot` has put `.run` in place. A record's entry runs there too: keep it
short, and call nothing that can block. The exception is `ev_midi_note_key`,
which runs in the sequencer tick (an interrupt, vector 0x60): a few
microseconds, no firmware call, nothing that waits or allocates.

`ev_midi_note_key` comes four times for a trig, once for each of the track's
chord notes (NOTE and NOT2-NOT4), whether or not a chord is set: in the
emulator, a trig with no chord came four times with the same note byte, and
stock sent the note once. A handler does not count the calls, and a rewrite
applies to every one. It hands its entry the MIDI track in `d7` (0-7, M1 = 0),
a pointer to the note byte in `a2` (0-127; -1 is no note, and storing -1 drops
that pass's note) and stock's KEY root offset at `-64(%fp)`. The entry rewrites the note, then goes to `0x4009fb80` to send it
as it now is, or replays the site's two instructions (`tst.l -64(%fp)`, then
`ble.s 0x4009fb80`) to have stock correct it to KEY. Stock keeps the note it
sends for that note's note-off. A taker that stores into the pattern or the
Part writes the bank and its battery-backed copy and sets the edited flags, as
stock's writers do. The site is the one octabam's MIDI SCALES (`ms_output`)
detours today.

Not here (yet) of Modwerk's: `ev_octave_number` (octabam's quantizer patches
an instruction that overlaps its site), and the call events and tables
(`ev_play_increment`, `ev_voice_bind`, `ev_tail_kill`, `ot_machines`, ...).

## Checked

- `tests/test_octatrack.py`, `test_the_intercepts_replay_their_sites`:
  every site is whole stock instructions, and each stub's stock path
  replays them byte for byte, a short branch at the end as the same branch
  to its absolute target.
- In octabam's `ot_emu`, the same session (creating a set and a project,
  MIDI notes, a knob turn, a p-lock, the CHROMATIC keyboard with held keys,
  octave keys and live recording, then a boot from the saved card), each
  pair compared by the screen and memory (the Part, its shadow, the
  settings, the octave word):
  - stock against core + intercepts with no records: identical; 16 of the
    19 sites ran (not the project load and line, nor the settings sanitise);
  - octabam's quantizer and synth with their own detours against the same
    converted with `--intercepts`: identical.
- `ev_midi_note_key` (0.2), in `ot_emu` with the sequencer playing M1's two
  notes (`tests/probe_octatrack_note_key.py`, 17 checks; emulator only, not run
  on a unit):
  - stock, core, core + intercepts and a probe record that claims nothing:
    every pass reaches stock's exit (`0x4009fb80`) with the registers and the
    stack stock has, and the MIDI out and the frame records are stock's;
    the stub takes 31 instructions from the site to the send where stock
    takes 2, and 53 with one record asked;
  - claims is given the MIDI track in `d7` (M1 is 0), four times for each trig;
    a record that claims another track is not entered;
  - an entry can add two to the note (the note-off follows), store -1 (nothing
    is sent), jump to the send, or replay the site for stock's KEY correction:
    with a KEY offset of 1 the notes sent are the ones the stock image's own
    table gives.
