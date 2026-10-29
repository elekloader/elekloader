# machines (Digitone mk1)

Machines for the Digitone mk1 and Digitone Keys, OS 1.43 and 1.44: sounds
that play a machine mod's voice in place of FM, through the Digitone's own
multimode filter, mixer and effects, with the AMP page's envelope. It is
built on core-dn1 ([../core-dn1](../core-dn1)). On its own it adds
MACHINES to the Mod Menu and nothing else: machines come in mods of their
own, and [examples/dn-sine-machine](../../examples/dn-sine-machine) is one
to start from.

0.1's files are on the [machines-dn1-v0.1](https://github.com/irpina/elekloader/releases/tag/machines-dn1-v0.1) pre-release, and it is in
the mod shop, beside the cores: a machine mod that requires it (`"requires": ["core",
"machines"]`) ticks it there, and the page takes core-dn1 3.1 or newer for it.

| | |
|---|---|
| needs | core-dn1 3.1 on OS 1.43; 4.2 on OS 1.44 |
| sites | OS 1.43: none; OS 1.44: ten SYN draw/name/value/DEST sites, owned once |
| tables | `dn_machines`; OS 1.44: optional `dn_machine_pages`, `dn_machine_names` |
| exports | `dnm_knob`, `dnm_knob_range`, `dnm_phase_inc`, `dnm_sin` (`digitone-mk1/machines.h`) |
| version | 0.3 |
| claims | `soundslot:0` |

## How it works

- **A sound's machine** is its slot 0, which no stock parameter uses: the
  machine's id << 8, 0 for FM. A voice loads it with the rest of the sound
  at each note.
- **MACHINES** in the Mod Menu (hold a track key) opens a submenu: FM and
  each machine in the build, as tiles. Picking one sets the held track's
  slot 0 and its SYN knobs to the machine's defaults (to FM's when it goes
  back to FM), with persistent `dnm_sound_edit` on OS 1.44 (`core_sound_set` on 1.43), so the track's voices play it from the
  next note.
- **The knobs.** While the active track's sound plays a machine, SYN1's first
  page and SYN2's first page are the machine's sixteen knobs: its labels, its
  value texts, on plain knobs that turn as the stock ones do
  (`core_param_override`, `core_param_ui_make`). The values stay the stock
  parameters', in their slots and ranges. The second pages are FM's alone,
  and show no labels or values, unless the machine takes them.
- **The second pages.** A machine with `DNM_PAGES2` in its flags owns
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

## Native UI (OS 1.44)

The companion replaces SYN content at the native draw call when a machine's
optional callback handles it; menus and notifications follow normally.
Without a page descriptor, the native renderer/navigation remain available.
The page count, full/short labels, held-lock helpers and compatibility
contract are in [Machines on the Digitone](../../docs/ADAPTING.md#machines-on-the-digitone-machines-digitone-mk1)
and [machines.h](../../elekloader/sdk/include/digitone-mk1/machines.h).
[dn-native-machine](../../examples/dn-native-machine) is the smallest example
with native drawing and full names; SINE remains portable on 1.43/1.44.

The 1.44 integration imports core4.2's processed pitch and Sound/UI helpers.
It uses processed pitch including portamento, invalidates
the native packed Sound on persistent edits, uses the native parameter-view
getter for GRID/STEP display, and handles FUNC+turn for selected steps 1-64 on either subpage.
Core owns the notification/LFO hooks; machines contributes names, formatting
and visibility callbacks. Long labels appear in notifications and LFO DEST lists; current DEST keeps
the short label. Unused destinations are hidden/skipped without renumbering
or changing saved assignments.

## Checks

```sh
python tests/test_machines_dn1.py
python tests/test_units.py
python tests/test_link_refusals.py
ELEKLOADER_DN_SYX=stock-1.43.syx ELEKLOADER_DN_SYX_144=stock-1.44.syx python tests/test_sdk.py
```

The host fixture checks UI routing, held locks/reset, PAGES2 ownership,
bounded names metadata, persistence notifications, DEST IDs and eight voice
pitches. SDK tests build/link both OS versions, verify descriptor sizes,
and refuse optional native extensions with an old/unported companion.
These checks do not qualify physical timing or storage/cold behaviour.

## Not yet

- On OS 1.43, and without a native page callback, SYN pages draw FM's pictures: SYN1's algorithm diagram (which
  follows ALGO, a machine's knob A), the group headings ("RATIO", "RATIO
  OFFSET"), and SYN2's envelope graphs over its first page, whose labels
  show three characters. Core-dn1 3.1 cannot change a stock page's drawing
  per track. So SYN1's second page shows no labels for a machine's four
  knobs there (its heading stands over them); their values show in the
  pop-up as they turn.
- On OS 1.43 a knob a machine leaves unused still turns, and changes the FM parameter
  under it (unseen, and unheard while the machine plays).
- On OS 1.43 notifications and LFO destination lists keep FM's names.
- The DSP still renders FM for a machine's voices, and its AMP envelope is
  repeated here.
- SYN1's knobs keep their stock ranges (ALGO 0-7, the ratios as indices, HARM
  -26..26, MIX -63..63); SYN2's are 0-127.
- A machine's render costs the main CPU per voice: in the emulator SINE
  takes about 3,000 instructions a voice a block (a block is 667 us). There
  is no voice limit yet.
