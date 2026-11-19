# smpl-machines

SMPL machines for the Analog Rytm MKII (core-ar2 0.15 or later, OS 1.74): a second
machine layer on the SMPL page, the way machine-pages is the Digitakt mk1's
page companion. Stock's SMPL page (the sound's indices 17..24, records
39..46: TUN, FIN, SMP, BR, STA, END, LOP, LEV) is one sample layer that
every SRC machine shares. A SMPL machine takes that page over for the
sounds that run it, and can replace the sample layer's sound with its own
engine. Mods such as PLAITS, DAISYSP and DSP SAMPLE add their machines here
and list smpl-machines as a dependency, so they combine in one build.

On its own, and for a sound that runs no SMPL machine, it changes nothing
but one extra row, SMPL MACHINE, in the sound setup menu, and the machine
list TRK + SMPL opens.

| | |
|---|---|
| needs | core-ar2 0.15 or later, with knob faces (`core_knob_faces`) and `core_param_set`; `resources` says 0.13, the core on this branch, until that core exists |
| sites | 4: the render's call of the sample layer's trig intake, 0x40121134 (`jsr 0x4011c1e0`), where it calls each machine's engine and holds back the trigs of voices a machine runs; the entry of the audio side's routine that puts a sound on a track, 0x4011f60a (the kit's sound, or a sound lock's pool sound); the sound setup menu's build, 0x400ef184, after its MACHINE row; and stock's SRC machine setter's entry, 0x400bd1ba, which it wraps |
| exports | `smpl_track_machine`; `smpl_face`, `smpl_text`, `smpl_c_get` and `smpl_c_delta` for smpl-knob-c |
| imports | `smpl_knob_c`, weakly: smpl-knob-c's record (below) |
| declares | `smpl_machines`, the machines (`elekloader/sdk/include/analog-rytm-mk2/smpl.h`); the id pool `smpl_machines`, ids 0..29 |
| storage | `smpl_choice`, 5 bits a sound: 0 AUTO, 1 stock, 2 + id a machine |
| RAM | 2.6 KB code, 7.8 KB data |

## What a machine is

A mod asks for machine ids by count (`"pool_ids": {"smpl_machines": n}`;
docs/FORMAT.md, "Id pools") and adds a `struct smpl_machine` for each to
`smpl_machines`: its title, a four-letter label, the voices that may run
it, how it binds to an SRC machine, its engine, and its eight knobs (label,
name, look, range, default, value text). docs/ADAPTING.md, "SMPL machines
(smpl-machines, Analog Rytm MKII)", has the record and an example.

Which machine a sound runs:

1. **Required**: one whose `bind` is `SMPL_REQUIRED` for the sound's SRC
   machine. The menu row then shows it and doesn't turn. A required machine
   is never offered in the row or the list: only its SRC machine runs it.
2. **Chosen**: the sound's `smpl_choice`, picked in the sound setup menu's
   SMPL MACHINE row, under MACHINE, or in the list SMPL opens while TRK is
   held: AUTO, STOCK, then the machines the track's voice may run. The list
   is stock's SRC machine list with these rows: UP and DOWN choose as you
   go, YES keeps the choice and NO puts the first one back, with the
   knobs' values as they were. With a required machine, or no machines,
   TRK + SMPL is stock's.

3. **Auto**: on AUTO, one whose `bind` is `SMPL_DEFAULT` for the SRC
   machine, else stock's.

A machine whose `voices` leave out the track's voice is not offered, and a
sound that names it plays stock's.

A change of the machine a sound runs, by a choice or by a change of its SRC
machine, writes the new machine's defaults, as a knob turn writes them
(`core_param_set`): a machine's into its knobs that have their own range
(`min < max`), stock's from its own SMPL page records. SMP's word (index
19) is never written, so the sound keeps its sample.

## Knob C

A machine's page shows its knobs in record order, so its knob C is SMP's
word, the sample slot stock plays. A machine that leaves knob C blank, or
uses it for the sample, needs nothing more. One that needs knob C for
something else sets `SMPL_OWN_C` in its `flags`, and its mod lists
`smpl-knob-c` in `requires`: that companion (mods/smpl-knob-c) holds one
lockable 7-bit sound field and one knob id, which every such machine
shares, as a sound runs one machine at a time. Knob C then shows that knob
id, with the machine's face; its value is the field, with p-locks, scenes,
performances, the LFO, routes and a MIDI CC from core, and the engine gets
it in `words[2]` (`core_param_value`). A build without smpl-knob-c takes
none of these bits, and knob C stays SMP's word.

## How it works

- **The page.** The knobs stay stock's records 39..46, so the values are
  still the sound's words: they store, p-lock, slide and take the LFO and
  MIDI as stock's do. Core's knob faces (core-ar2 README, "Knob faces")
  give them the machine's label, name, look, range and default; core's
  `core_params` entries on the stock ids give their text; and a page
  function on stock pages 4 and 5 gives the title. A knob with no look
  whose range differs from the record's under it gets BR's plain dial
  (42): the record's look draws on its own range (TUN's +-24 stops short). From 0.3 page 5, stock's
  waveform, is shown only for stock's layer and machines with
  `SMPL_WAVEFORM` (a `core_view_pages` entry with a shown on the stock
  page, core-ar2 0.15).
- **The sound.** At 0x40121134 the render hands the sample layer the trig
  mask (D3) and the stop mask (D5). For each voice whose owner track runs
  a machine, smpl-machines calls the engine with the track's engine words
  for indices 17..24 and the trig bit, then clears the voice's trig bit and
  sets its stop bit, so stock's sample stays silent there. The same masks
  reach the sample layer's second call (0x4011c6b8), which reads them again.
- **Which sound plays.** Stock's audio side puts a sound on a track with
  one routine, 0x4011f60a: the kit's sound, or a sound lock's pool sound
  for its step. smpl-machines notes the sound there, and the render runs
  the machine that sound names, so a sound-locked step plays its pool
  sound's SMPL machine (the choice is a sound field, so it travels with the
  sound). Each UI tick (`ev_tick`, order 40) it also works out the current
  kit's machines, which the page and a track with no sound noted yet use.
- **SRC machine changes.** Stock's SRC machine setter (0x400bd1ba: sound,
  machine, track, flag) is every route to a new SRC machine: its stepper
  and list, MIDI CC 15 and NRPN, a page's paste and clear, Overbridge.
  smpl-machines wraps it: it notes the SMPL machine the sound runs, runs
  stock's body as a call (its link and movem, then 0x400bd1c2), and writes
  the new SMPL machine's defaults if the change changed it. It reads the
  machine after the call, since stock can return without writing.
