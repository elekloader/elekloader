# machines (Digitone mk1)

Machines for the Digitone mk1 and Digitone Keys, OS 1.43 and 1.44: sounds
that play a machine mod's voice in place of FM, through the Digitone's own
multimode filter, mixer and effects, with the AMP page's envelope. It is
built on core-dn1 3.1 ([../core-dn1](../core-dn1)). On its own it adds
MACHINES to the Mod Menu and nothing else: machines come in mods of their
own, and [examples/dn-sine-machine](../../examples/dn-sine-machine) is one
to start from.

0.1's files are on the [machines-dn1-v0.1](https://github.com/irpina/elekloader/releases/tag/machines-dn1-v0.1) pre-release, and it is in
the mod shop, beside the cores: a machine mod that requires it (`"requires": ["core",
"machines"]`) ticks it there, and the page takes core-dn1 3.1 or newer for it.

| | |
|---|---|
| needs | core-dn1 4.2 (`resources.core`; 0.2: 3.1) |
| sites | none: it uses core's `ev_voice_on`, `ev_render_voices`, `ev_tick` and `ev_enc` (0.3, an SDK machine's presentation), `core_param_override`, `core_sound_set` and `core_menu_open` |
| table | `dn_machines`: a machine mod contributes a pointer to its `struct dnm_machine`; `sdk_alpha_machines` (0.3): the SDK alpha builder contributes each registration (`elekloader/sdk_alpha_machine.h`) |
| exports | `dnm_knob`, `dnm_knob_range`, `dnm_phase_inc`, `dnm_sin` (`digitone-mk1/machines.h`) |
| version | 0.3: SDK alpha machines, empty or with up to two pages and fifteen knobs, each knob kept in one SYN parameter (SYN2's first page's eight, then DTUN, FDBK, MIX, ADEL, BDEL and two ratio offsets) whichever page shows it, on pages of their own which SYN2 shows in place of its pages on the machine's track (`core_page_list`), each track keeping its page, with the SDK labels, a four-column grid without FM's algorithm diagram, and blank positions for undeclared knobs; `.present` runs again when a value changes, applies five-character labels, visibility and optional 16×16 glyphs, and blocks hidden encoders while retaining their values in native page copy, clear/undo and paste. Both native LFOs target its logical knobs with current labels, even while hidden; destination naming calls `.present` with `ELEK_UI_NO_PAGE`. A machine with no render plays FM. 0.2: a machine may take the SYN pages' second pages (`DNM_PAGES2`) |
| claims | `soundslot:0`, `page:30` and `page:29` (0.3: the SDK pages) |
| RAM | OS 1.44: 9.3 KB code and initialized data, 13.4 KB zero-initialized data |

## How it works

- **A sound's machine** is its slot 0, which no stock parameter uses: the
  machine's id << 8, 0 for FM. A voice loads it with the rest of the sound
  at each note.
- **MACHINES** in the Mod Menu (hold a track key) opens a submenu: FM and
  each machine in the build, as tiles. Picking one sets the held track's
  slot 0 and its SYN knobs to the machine's defaults (to FM's when it goes
  back to FM), with `core_sound_set`, so the track's voices play it from the
  next note.
- **The knobs.** While the active track's sound plays a machine, SYN1's first
  page and SYN2's first page are the machine's sixteen knobs: its labels, its
  value texts, on plain knobs that turn as the stock ones do
  (`core_param_override`, `core_param_ui_make`). The values stay the stock
  parameters', in their slots and ranges. The second pages are FM's alone,
  and show no labels or values, unless the machine takes them.
- **The second pages (0.2).** A machine with `DNM_PAGES2` in its flags owns
  SYN1's and SYN2's second pages as well: knobs 16-31, 27 knobs in all (the
  pages have five empty positions). They are FM's ratio offsets (0-127.99 in
  1/256 steps), the operators' delays (0-127), their trig and reset switches
  (0-1) and the phase reset (0-4), relabelled the same way, saved with the
  sound and p-lockable; on FM they mean nothing while the machine plays. A
  switch away from such a machine, to FM or to a machine without them, sets
  them back to FM's defaults.
- **The sound.** Each block (`ev_render_voices`) every voice playing a
  machine gets the machine's 32 samples in place of the FM voice's. The DSP
  applies the AMP envelope to FM before its output, so this applies it to
  the machine: ATK, DEC, SUS and REL from the voice's slots 66-69, started
  by `ev_voice_on` and released by `fw_gate_off`, and the note's velocity.
  A voice whose envelope has ended is silent.

The fields of `struct dnm_machine` and `struct dnm_knob` are in
[docs/ADAPTING.md](../../docs/ADAPTING.md), "Machines on the Digitone".

## Checked

In digikit's emulator, OS 1.43 and 1.44, with SINE and digitables 1.4:
MACHINES opens its submenu; SINE picked for track 1 plays a pure sine at the
note (G4, 392 Hz, no harmonics), FOLD adds odd harmonics, OCT +1 moves it an
octave up, the AMP page's release fades it; track 2 stays FM; back to FM sets
FM's defaults. Turning a knob on a machine track moves its value as on FM
(ALGO, under OCT, about four detents a step). A machine voice that peaks at
`DNM_PEAK` plays about as loud as FM at the stock defaults.

0.2, OS 1.43 and 1.44, with SINE 1.1: picked through MACHINES, SINE's FINE
(SYN1's second page, knob A) turned up to 127.99 and down to 0 moves SINE's
oscillator by x1.0592 and x0.9439 (FINE asks x1.0595 and x0.9439), and the
pop-up shows FINE's value text ("+11" at 71.6); SYN2's second page shows its
seven knobs unused, as SINE leaves them; MACHINES back to FM sets the second
pages to FM's defaults again (COFF 64.00, ATRG on). Not yet on a unit.

## Not yet

- The SDK pages claim `page:30` and `page:29`, two of core-dn1's four mod
  page indices (27-30): a build with another mod on either refuses to link.
  And SYN2's
  list follows `core_page_list`, where the last call wins: machines-dn1
  must be the only mod that changes it.
- Outside the SDK page, the SYN pages still draw FM's pictures: SYN1's
  algorithm diagram (which
  follows ALGO, a machine's knob A), the group headings ("RATIO", "RATIO
  OFFSET"), and SYN2's envelope graphs over its first page, whose labels
  show three characters. Core-dn1 3.1 cannot change a stock page's drawing
  per track. So SYN1's second page shows no labels for a machine's four
  knobs there (its heading stands over them); their values show in the
  pop-up as they turn.
- A knob a machine leaves unused still turns, and changes the FM parameter
  under it (unseen, and unheard while the machine plays).
- The value pop-up's long name keeps FM's name. Non-SDK machines also keep
  FM's long destination names and groups in the LFO list and DEST box.
- The DSP still renders FM for a machine's voices, and its AMP envelope is
  repeated here.
- SYN1's knobs keep their stock ranges (ALGO 0-7, the ratios as indices, HARM
  -26..26, MIX -63..63); SYN2's are 0-127.
- A machine's render costs the main CPU per voice: in the emulator SINE
  takes about 3,000 instructions a voice a block (a block is 667 us). There
  is no voice limit yet.
