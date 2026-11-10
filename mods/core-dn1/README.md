# core for the Digitone mk1

The boot copier and the hook bus for the Digitone mk1 and Digitone Keys,
OS 1.43 and 1.44. It is [../core/core.s](../core/core.s), with
[settings.s](../core/settings.s), [render.s](../core/render.s) and from 5.3
[notes.s](../core/notes.s), built with the
Digitone's addresses and sites (`mod.json`: 1.43's, and 1.44's under
`ports`). Every set of linkable (format 2) mods for
the Digitone needs it. On its own it changes nothing the unit does.

- **At boot** (the OS entry's call at 0x40000538, as on the Digitakt),
  `boot` copies the RAM image the linker built to its area (0x43EC0000
  from 4.0, 0x47BE0000 before), zeroes
  `.bss`, starts the DTIM0 counter if nothing has, and goes on to the call
  it replaced.
- **The events** are the Digitakt mk1's (docs/ADAPTING.md, "The hook
  bus"), from the Digitone's sites: 0x4001900c (tick), 0x40019072 (draw),
  0x40019d9c (key), 0x40019de4 (encoder), 0x40072a34 (SETTINGS),
  0x4009d108 and 0x4009e51c (the render's entry and exit). In 1.44 the
  last three are 0x40072a54, 0x4009d128 and 0x4009e53c; the rest are where
  they were.
- **From 2.1, `ev_voice_on`** ([voice.s](voice.s)), which the Digitakt's
  core does not have: at 0x4009e928 (0x4009e948 in 1.44), where the
  render's note-on loop stores a voice's pitch word (the note << 16, at
  `0x41391f80` + 4 x voice; `0x41392f80` in 1.44), it
  makes that store itself and calls `f(voice, track, event)`. A handler
  may change the pitch word: the render reads it every block, so a change
  is heard within about three blocks. The voice's sound and the step's
  locks are loaded after this point; read the voice's parameters from
  `ev_render_out`.
- **From 2.1, parameter slots** ([params.s](params.s)): ids 182-184 for
  mods' own parameters, handled by the stock UI as its own (knob, pop-up,
  value, p-locks). At boot `core_dn_boot` moves the two small tables after
  the stock parameter records to `core_ptab_save` and installs the records
  contributed to `core_params` in their place; the firmware's 58 id
  bounds are raised by three, and the UI record lookups build each slot's
  record from the stock one it borrows (docs/ADAPTING.md, "Parameter
  slots").
- **From 2.1, mod pages** ([pages.s](pages.s)): pages 27-30 for mods,
  each put after a stock page in its key's list (AMP's third page, say)
  and shown, turned and p-locked by the stock UI. The page record lookup
  serves them, the views' builder puts them in their key's list, and
  `core_page_open` opens one from a key handler, `core_page_shown` says
  whether it is on screen (docs/ADAPTING.md, "Mod pages").
- **From 2.1, project data** ([projdata.s](projdata.s)): mods' blocks
  saved and loaded with the project, in the 480 bytes of its storage
  header the OS leaves alone. The serializer's entry puts them there for
  every save; the loads and the new project's builder take them out, or
  tell the mod there are none (docs/ADAPTING.md, "Project data").
- **From 2.2, the Mod Menu** ([menu.s](menu.s)): hold a track key on its
  own and core opens a menu of what mods contributed to `core_menu` (a name
  and an `open` function each), unless a handler of `ev_hold` takes the
  hold first. The four UI sites go to `core_dn_tick`, `core_dn_draw`,
  `core_dn_key` and `core_dn_enc`, which see to the menu and go on to
  core.s's (docs/ADAPTING.md, "The Mod Menu").
- **From 2.3, the menu is a grid of icons:** tiles two across and two down,
  each an entry's 16 x 16 icon over its label, with a scroll bar; the
  arrows move by tile and row. A descriptor may add the tag `0x49434F4E`
  ("ICON") and an icon after its `open`; one without (2.2's) gets a chip.
  Below, from digikit's emulator, with digitables 1.3 and a test mod's
  seven entries linked: holding T1 opens the first page, with TABLES
  selected; RIGHT and then DOWN three times reach the last row, and the bar
  on the right has moved down with it; YES on TABLES opens digitables' page.

  ![The Mod Menu's first page: TABLES, SYNTH, DEMO TWO and NOTES, TABLES selected](../../docs/img/dn-modmenu-grid.png)
  ![The Mod Menu scrolled to its last row: DICE, a long label cut at 14 characters, HEART and SMILE, SMILE selected](../../docs/img/dn-modmenu-scrolled.png)
  ![TABLES picked: digitables' Table page, AMP's third page, with its TBL and SPD knobs](../../docs/img/dn-modmenu-tbl-page.png)
- **From 3.0, firmware locations as exports** ([fw.s](fw.s)): the data and
  routines Digitone mods use, as symbols. They cover the voices' pitch words,
  parameters and lengths, the timeline, the LFOs, the note queue, the kit,
  the slot ids, and drawing. 1.43's values are in mod.json and 1.44's in its
  port. A mod that names them instead of addresses builds for both OS
  versions unchanged (`elekloader/sdk/include/digitone-mk1/core3.h`;
  docs/ADAPTING.md, "Firmware locations (core-dn1 3.0)"). Nothing else
  changes: 3.0 is 2.3's code, sites and tables.
- **From 3.1, what machines need** (docs/ADAPTING.md, "Machines and stock
  parameters"):
  - `ev_render_voices` ([voices.s](voices.s)): each block, the DSP's eight
    voices (32 samples each, `fw_voices`) after they reach SRAM and before
    the render's multimode filter, at 0x4009e078 (0x4009e098 in 1.44). A
    handler may write a voice's block: that is the voice's sound through
    its filter, the mixer and the effects. The machines mod
    ([../machines-dn1](../machines-dn1)) plays its machines there.
  - `core_param_override` ([params.s](params.s)): a mod answers for a stock
    parameter while it wants to: its UI record (value text, knob graphic,
    how a turn moves it), its label and the LFO destination names and groups
    ([names.s](names.s), 5.2). Core consults the table in its UI
    record lookups, and replaces the routine that makes a knob's label
    (0x400207b8, the same in 1.44). `core_param_ui_make` builds a UI
    record from a stock parameter's turn and another one's graphic.
  - `core_sound_set(track, slot, value)`: a sound slot changed as a stock
    edit changes it, the kit and then the track's voices. A voice keeps the
    sound it loaded last and loads it again only for another sound, so a
    write to the kit alone is not heard on a track's voices until a stock
    edit reloads them.
  - `core_menu_open(list, sel, pick)` ([menu.s](menu.s)): an entry's open()
    shows a list of its own in the Mod Menu's grid, a submenu.
  - Six more firmware locations: `fw_voices`, `fw_voice_track`,
    `fw_gate_on`, `fw_active_track`, `fw_params` and `fw_uirecs`.

  3.1 keeps everything 2.3 and 3.0 have: their sites, tables, code paths
  and exports, so every 2.x and 3.0 mod links with it (`tests/test_sdk.py`
  checks it against the released 2.3). It adds two sites and two tables.
- **From 3.2, exclusive audio** ([audio.s](audio.s); docs/ADAPTING.md,
  "Exclusive audio"): a mod may take the whole output while it wants to.
  Each block core takes the first record in the table `core_audio` whose
  `on` is set. While there is one, the render's master stage (the inputs'
  mix, chorus, delay, reverb and drive, called at 0x4009e146) does not run,
  and the owner's `render(out, in)` gets the input and writes the output.
  With `CORE_AUDIO_MUTE_VOICES` the voices' filter loop (0x4009e07e) is
  skipped too, so the synths are silent. Also `fw_tempo`,
  the tempo x 120, and the stock pages' fonts, `fw_font_label` and
  `fw_font_title`, for a mod that draws a page of its own in their style. The profile's `bulk` area (0x44000000-0x47BE0000) holds
  such mods' large buffers, as regions they claim.
  [examples/dn-thru](../../examples/dn-thru) is the smallest such mod.
- **From 3.2, `ev_midi_cc`** ([midi.s](midi.s)): each MIDI CC the unit
  receives on a track's channel (1-9 by default) or the auto channel, as
  `f(track, cc, value, flags)`, before the stock applies it; a handler
  that returns nonzero takes it. The site is the CC router's track check
  at 0x400ed96e (0x400edbe2 in 1.44), 32 bytes past the router's entry,
  which Tone+FX patches: the two link together, and a CC Tone+FX lets
  through comes to core next.
- **From 3.3, `ev_key_leds`** ([leds.s](leds.s)): the key-LED frame,
  before the stock views set theirs, `f(mgr)` with the LedManager, at the
  frame's call to the views' LED update (0x40018332 in both versions); the
  first setter of an LED wins, so a handler's stays over the stock view's.
- **From 3.3, `ev_render_params`** ([block.s](block.s)): each block's sound
  parameter words, `f(words)`, after the smoother writes them and before
  the controller stage (0x4009c058) and the LFOs ride on them, at its call
  (0x4009db80, 0x4009dba0 in 1.44). A change lasts the block
  (docs/ADAPTING.md, "Key LEDs and each block's sound parameters").
- **From 3.3, more firmware locations** ([fw.s](fw.s); docs/ADAPTING.md,
  "Firmware locations"): the firmware's C library, `fw_memcpy`,
  `fw_memset`, `fw_memchr`, `fw_strcmp`, `fw_strncmp`, `fw_strchr`,
  `fw_strncpy` and zlib's `fw_crc32`; libgcc's 64-bit shifts under their
  own names, `__ashldi3`, `__lshrdi3` and `__ashrdi3`, so gcc's calls link
  with no libgcc in a mod (which then must not define them); the stock
  menus' text box, `fw_textbox`;
  the transport, `fw_transport`; and the active pattern and the project,
  `fw_pattern` and `fw_project`, with the pattern's layout in core3.h
  (`FW_TRACK`, `FW_STEP`, `FW_TRACK_NOTE`, ...). These are exports only:
  they add no code, sites or tables.
- **From 4.0, insert audio** ([audio.s](audio.s); docs/ADAPTING.md,
  "Insert audio"): an owner with `CORE_AUDIO_INSERT` in its flags gets
  what the master stage made instead of the input: the stage runs as
  stock, and its output goes through the owner's `render` on its way to
  the codec and USB's main pair. An effect on everything the Digitone
  plays, which may stay on under the Digitone's own pages. No new site:
  with that flag, and `core_audio_caps`, an export that says
  so to a mod that weak-imports it (0 with an older core). [examples/dn-tremolo](../../examples/dn-tremolo)
  is the smallest such mod.
- **From 4.0, the mods' RAM and program changes** (docs/ADAPTING.md,
  "The mods' RAM" and "Program changes"): the linker lays the mods out
  from 0x43EC0000 to 0x44000000, 1.25 MB in place of 128 KB (the
  profile's `core_ddr`; nothing in core's code changes for it), and
  `ev_midi_pc` ([midi.s](midi.s)) gives a mod each program change the
  unit receives before the stock applies it, at 0x400ea1a8 (0x400ea41c
  in 1.44), MIDI CONFIG's receive test in the firmware's handler.
  [examples/dn-progchange](../../examples/dn-progchange) is the smallest
  such mod.
- **From 4.1, the note queue** ([queue.s](queue.s); docs/ADAPTING.md, "The
  note queue"): `ev_note_queue` gives a mod each event a note source queues
  for the render (the sequencer's fires a step ahead, with the step's
  p-locks), with the time it plays, at the queue's entry 0x400fff04
  (0x40100178 in 1.44). fxkit ([../fxkit-dn1](../fxkit-dn1)) is built on it:
  an effect's p-locks, kept in a track's lock table, on their steps.
- **4.1.1, SETTINGS rows off the heap** ([rows.s](rows.s)): a row a mod
  adds with `core_additem` is no longer allocated from the firmware's heap.
  Its MenuItem and the shared_ptr's control block live in core's RAM (eight
  rows; a ninth comes from the heap, as before), with a control block that
  frees nothing when the menu drops the row. Before, each row kept 0x54 +
  16 bytes of the heap, a buddy allocator, for good, and that changed the
  rest of the boot's heap: on a first boot (FACTORY PROJECT >> +DRIVE) the
  progress screen's allocations then split and merged blocks on every
  frame, and the factory project's format upgrade, a lower-priority task,
  starved. In digikit's emulator, two rows on 1.43 or 1.44 never got past
  that screen. The calls in `core_additem` changed (`ROW_POOL`), not its
  place or its arguments: mods need no change.
- **From 4.2, the parameter pages and the starting voice** ([edit.s](edit.s),
  [voice.s](voice.s); docs/ADAPTING.md, "Parameter pages and the starting
  voice"), for a mod that keeps values of its own for stock parameters:
  - `ev_param_turn(view, id, delta, knob)`: a knob turn on a parameter page
    that the stock would put into the sound (no trig held), at the knob
    handler's held-steps question (0x400455ba, the same in 1.44); a handler
    that returns nonzero takes it, and the stock neither edits the sound nor
    shows its pop-up. GRID REC's held trigs keep their turns.
  - `ev_param_value(view, id, &value, &locked)`: the value a page shows on a
    knob, at the pages' value getter (0x40043dd0, the same in 1.44); a
    handler may answer, inverted as a p-lock when `locked` is 1. Core asks
    the same handlers at the views' value routine (0x400432dc), which the
    SYN pages draw their envelopes and algorithm from, and at SYN2's
    envelope page's own call of it (0x4004b462, 1.44 0x4004b482), which
    draws that page's knobs.
  - `ev_voice_ready(voice, track, event)`: each voice starting a note, once
    it holds its sound and the event's p-locks, at the note-on loop's last
    instruction for it (0x4009eadc, 1.44 0x4009eafc), after `ev_voice_on`.
    With `fw_voice_locks` (the render's own routine) a handler puts more
    locks into the voice as the render puts a step's; `fw_voice_locked` is
    each voice's locked slots, and `fw_param_uirec` a parameter's UI record.
  - Project data blocks that save only what they hold: `CORE_PROJDATA_VARIABLE`
    in a descriptor's size (projdata.s).
- **From 5.0, the firmware's moments** ([system.s](system.s), [events.s](events.s);
  a prototype: the sites, their 1.44 ports and what the emulator showed are in
  the hook map, research notes `dn1-hooks/`). Each is an observer list unless
  it says "may take" (a handler's nonzero return stops the stock), and every
  handler gets the routine's own arguments:
  - the system: `ev_card_ready(status)` once the +Drive is up and before the
    project loads (0x4008d27c, 1.44 0x4008d29c: the init task's store of the
    card's status), `ev_boot_ui()` as the UI task is about to start
    (0x4008d3d4), `ev_usb(configured)` (0x40004778, 0x400047ce),
    `ev_crash(frame)` for an exception nothing handles (core installs its
    handler in place of the stock crash printer at 0x400e7872, and calls the
    printer after the handlers), and the exports `core_restart` (a reboot
    that waits for the +Drive and lets the panel link drain with interrupts
    on, which the stock's own restart does not) and `core_restart_now`;
  - the +Drive and SysEx out as exports: `fw_card_read`, `fw_card_write`,
    `fw_card_erase`, `fw_card_sectors`, `fw_sysex_send`;
  - storage: `ev_project_saving(store, slot, a3)` and
    `ev_project_saved(block, slot, ok)` around the slot writer (0x40027704,
    0x400277dc; slot 128 is the working project);
  - the sequencer: `ev_transport(what, arg)`, may take, at its start
    (0x40093e1c), stop (0x400952f0) and pause (0x40094de0), which the PLAY and
    STOP keys and MIDI Start, Continue and Stop all reach; `ev_seq_tick()`
    each tick (0x40095518, interrupt); `ev_pattern_request(ui, request, a3)`
    (0x40029424); `ev_tempo(bpm100)`, may take, at the tempo setter
    (0x4009c82c); `ev_pattern_switch(pending)` as a requested pattern change
    is applied (0x400fde0c);
  - MIDI: `ev_midi_in(msg, len, source)`, may take, every message the MIDI
    task dispatches, on both of its paths (0x400e99f4, 0x400e9a5e);
    `ev_sysex(msg, len, source)`, may take, at the SysEx router's entry
    (0x400ea854; digihealth's site further in still works);
    `ev_midi_out(buf, len, port)`, may take, at the port switch (0x400edf32);
  - voices: `ev_voice_free(voice)` as a voice is released (0x4009f9bc),
    `ev_voice_steal(voice, old_track)` as the allocator takes a busy voice
    (0x4009f700) and `ev_arp_step(track, event)` as an arpeggiator step is due
    (0x4009d6a0), all at interrupt level;
  - the UI task: `ev_ui_msg(msg)` for each message it takes (0x40019cda);
  - KEYBOARD mode and mutes: `ev_keyboard_note(state, on, track, note)`, may
    take a press, at the note-on dispatcher (0x4003b198) and the note-off
    (0x4003b26e), from 5.2.1 hooked 6 bytes into each (0x4003b19e,
    0x4003b274), so a mod that hooks their entries runs first, and `ev_mute(where, track, muted, row)` before a track's
    mute is set: the global mute (FUNC + a track key, 0x4002f994), the
    pattern's mutes (0x400238ee) or a song row's (0x40033bee);
  - the pattern menu: `ev_pattern_op(op, index, what)` as FUNC+YES saves
    (op 0, 0x40027a00) or FUNC+NO reloads (op 1, 0x40027e4a) the pattern,
    its kit (what 1), its sequence (2) or both (3), before the stock checks,
    and `ev_track_reload(track, pattern, params)` as a held track key + NO
    reloads a track's sound (0x40029d16);
  - the SOUND MANAGER (SETTINGS > SOUNDS): `ev_sound_op(op, a, b)`
    before it copies the ticked sounds to the pool (op 0, 0x400d33ce), to the
    +Drive or a bank (1, 0x400d3604), deletes them (3, 0x400d3a42), sends
    them as SysEx (4, 0x400d3854) or toggles their write protection (5,
    0x400d3d90), or the SOUND BROWSER pastes into a pool slot (6,
    0x400d5a7a: how a copied track sound is saved) or the pool view saves
    the track's sound into a slot (7, SAVE TO HERE, 0x400d5236); op 2 copies from the
    manager's pool view (0x400d31ec); and
    `ev_sound_load(track, sound, kit)` before the kit puts a sound on a track
    (the SOUND BROWSER's FUNC+YES, 0x4001d710), and `ev_sound_clear(slot,
    name)` before CLEAR TRACK SOUND initialises one (0x400328f6);
  - `ev_idle()`, over and over in the init task (the lowest priority task
    that runs) once the UI task is up, from the ev_boot_ui stub: with no
    handler the stock idle spin runs;
  - the view stack: `ev_view(op, container, a, b)` before a view is pushed
    (op 0, 0x400e60de), replaced by a page key (1, 0x400e6ad4) or popped
    (2, 0x400e63c0), and `fw_view()`, the view on top (keys go to it);
  - sequencer edits: `ev_trig_set`, `ev_step_note`, `ev_plock_store`,
    `ev_plock_clear`, `ev_step_clear`, `ev_track_length` at the routines
    that write a step's flags, note slots, locks and a track's length;
  - sound parameters: `ev_param_set` at the stock setter's commit
    (0x40020fc0) and `ev_sound_live(value, track, slot)` as a value goes to
    the voices (0x4009c972);
  - the master's effect stages: `ev_render_fx(stage, out, in, params)`, may
    take, at the calls of the three send effects and the master drive
    (0x400971e6, 0x40097268, 0x40097318, 0x40097402; interrupt, each block).
  The test mod [examples/hooks-check-dn1](../../examples/hooks-check-dn1)
  subscribes to all of them.
- **5.0.1, what the first 5.0 mods needed** ([seqmidi.s](seqmidi.s),
  [track.s](track.s); in core3.h's 5.0.1 section):
  - `ev_seq_midi(msg, len, buffer)`, may take: each message the sequencer's
    MIDI engine queues for its output, whole, at its two append routines
    (0x400f3bfc buffer 0, 0x400f39a6 buffer 1; 1.44 0x400f3e70, 0x400f3c1a):
    the MIDI tracks' notes, CCs and the rest. The engine writes its buffers
    straight to DIN and USB (0x400f355a), so these never pass the port
    switch `ev_midi_out` sees: they are the hook map's "direct sends" a MIDI
    track makes. Interrupt level.
  - `core_midi_receive(msg, len)`: a message played into the unit as if it
    came in on DIN: `ev_midi_in`'s handlers, then the MIDI task's handler for
    its status (the dispatch table 0x401cdec4, 1.44 0x401ce294), so
    `ev_midi_cc` and `ev_midi_pc` see it. A task, not an interrupt.
  - `core_pool_load(track, index)`: a pool sound put on a track as the SOUND
    BROWSER's FUNC+YES does (the pool's getter 0x40033540, then the kit's load
    0x4001d710, which `ev_sound_load` sees).
  - `core_mutes()` and `core_mute_set(track, muted)`: the global mutes, as
    FUNC + a track key sets them (0x4002f994 with the project's track state,
    project + 0x30; `ev_mute` sees them).
  - Exports: `fw_brain_key` (Brain::key, 0x40016fc0: a handler of `ev_key`
    may hand the stock an event of its own, to replay a key or a combo),
    `fw_sound_load` and `fw_midi_chan` (MIDI CONFIG's track channels,
    0x41367d30, 1.44 0x41368d30).

  5.0.1 keeps all of 5.0 (`tests/test_sdk.py`, with a 5.0 build) and adds
  two sites and one table. The mods it was made for, each in its own
  repository: [digiarpmenu](https://github.com/irpina/digiarpmenu),
  [digisolo](https://github.com/irpina/digisolo),
  [digiloop](https://github.com/irpina/digiloop) and
  [digipcsound](https://github.com/irpina/digipcsound).
- **5.1, mod parameters with p-locks and their knobs** ([lanes.c](lanes.c),
  [lanes.s](lanes.s); in core3.h's 5.1 section; ADAPTING.md, "Lane
  parameters, knobs on pages and TRIG's second page"):
  - ten mod parameter ids, 182-191: two more of the firmware's small tables
    after the parameter records move into core (0x4018fc90 and 0x4018fd34,
    read at 0x400081a6, 0x40007d46 and 0x40008152, ended at 0x40008160), and
    the 58 id bounds are raised to 192;
  - lane parameters (`CORE_SLOT_LANE`): values per track and per step in
    core's RAM, for all 128 live patterns (`fw_patterns`, 0x1611d bytes
    apart), saved as lock rows 0x80+ and 0xC0+ after stock's, at the rows'
    save (0x400090f2) and load (0x40008ee6), both by jmp at their entries;
    the knob's turn and value through 4.2's sites; a trig's removal clears
    them (`ev_step_clear`'s site); CREATE NEW clears them all;
  - knobs on the stock pages' empty boxes (`core_knobs`, at each page
    record lookup) and core's own page 30, TRIG's second (`core_trig2`);
  - `ev_note_built(track, step, event)`, at the note builders' four calls
    (0x40094d4e and 0x40094d7e, the first step PLAY plays, and 0x40095b28
    and 0x40095b3a, the sequencer's tick; 1.44 0x20 further on), and
    `core_note_step(event)`; a lane changed while stopped has the sequencer
    build its first step again (0x40094bc2, 1.44 0x40094be2);
  - `core_pool_count()` and `fw_pool_sounds`, for a note that plays a pool
    sound as a sound lock does.

  5.1 keeps all of 5.0 and 5.0.1 but for the id bounds it raises again
  (`tests/test_sdk.py`, with a 5.0 build). The mods it was made for are
  [digichoke](https://github.com/irpina/digichoke),
  [digipcsound](https://github.com/irpina/digipcsound) and
  [digiloop](https://github.com/irpina/digiloop).
- **5.2, page lists and LFO names** ([lists.c](lists.c), [pages.s](pages.s);
  docs/ADAPTING.md, "Mod pages"): `core_page_list(first, list, n)` gives a
  page key another list of pages for a while (SYN2 = page 4: its stock
  list starts there), or its stock list back with `n` 0. machines-dn1 shows
  an SDK alpha machine's pages behind SYN2 with it, in place of FM's
  envelope pages. No new site: `core_view_init` notes each view it builds.
  The LFO destination list asks `core_param_override`'s name for a
  destination's name and group (four sites, [names.s](names.s)).

  5.2 keeps all of 5.1.
- **5.2.1, room for DigiArp MIDI** ([events.s](events.s)): `ev_keyboard_note`'s
  two sites move 6 bytes into their routines (0x4003b19e, 0x4003b274), past
  the instructions [DigiArp MIDI](https://github.com/aleks-karachevtsev/Digitone-MIDI-Arpeggiator)
  hooks; with 5.0-5.2 the two could not be built together. A mod that hooks
  a routine's entry and goes on at its + 6 runs before the event; one that
  takes the press there leaves the event unsent. Nothing else changes.
- **5.3, the played notes** ([../core/notes.s](../core/notes.s), the
  Digitakt core 4.2's, last in `sources`). Two sites more, the entries of
  the stock note-on and note-off routines that KEYBOARD mode's trig keys
  and a MIDI note on a track's channel reach (1.44: 0x400eae5c,
  0x400eabb4; 1.43: 0x400eabe8, 0x400ea940, 0x274 earlier as the rest of
  that code):
  - `ev_live_note(track, note, vel, on, source)`, which a handler may
    take, press or release: the stock then neither counts the key nor
    plays (or releases) it. `source` is 0x40 or 0x80 from KEYBOARD mode,
    and the port for MIDI (8 or 0x10 DIN, 2 or 4 USB);
  - `fw_note_on(track, note, vel, source, skip, sound)` and
    `fw_note_off(track, note, source)`: those routines past the event.

  With no subscriber the unit is stock. machines-dn1 0.5 carries out the
  SDK's mono machines on it.
- **OS 1.44:** every site has its port (`ports` in mod.json): the same
  code, moved, and RAM 0x1000 further on.

Build it with the SDK (it needs m68k binutils):

```bash
python -m elekloader.sdk.build mods/core-dn1 --stock Digitone_and_Digitone_Keys_OS1.43.syx   # out/core-5.3.elemod
python -m elekloader.sdk.build mods/core-dn1 --stock Digitone_and_Digitone_Keys_OS1.44.syx   # out/core-5.3-os1.44.elemod
python -m elekloader.lint mods/core-dn1/out/core-5.3-os1.44.elemod --stock Digitone_and_Digitone_Keys_OS1.44.syx
```

The LFO name hooks have ColdFire ABI probes (stock fallback, first answer,
arguments and registers). Run with digiemu's Python (`unicorn`); no stock
file is needed:

```bash
ELEKLOADER_CROSS=m68k-elf- python tests/test_digitone_lfo_names.py
```

A release names them `core-dn1-5.3.elemod` and `core-dn1-5.3-os1.44.elemod`.
5.3 is on main, not released yet; 5.2.1's are in the latest release, and
5.1's on the [core-dn1-v5.1](https://github.com/elekloader/elekloader/releases/tag/core-dn1-v5.1) pre-release
(5.2, 5.0.1, 5.0 and 4.2 were not released);
4.1.1's are in release v0.9.0, 4.1's on the [core-dn1-v4.1](https://github.com/elekloader/elekloader/releases/tag/core-dn1-v4.1) pre-release,
4.0's on the [core-dn1-v4.0](https://github.com/elekloader/elekloader/releases/tag/core-dn1-v4.0) pre-release
(3.3 was not released);
3.2's are on the [core-dn1-v3.2](https://github.com/elekloader/elekloader/releases/tag/core-dn1-v3.2) pre-release
(3.0 and 3.1 were not released), and
2.3's on the [core-dn1-v2.3](https://github.com/elekloader/elekloader/releases/tag/core-dn1-v2.3) pre-release.

Checked by cold-booting it in digikit's emulator against stock (docs/DEVICES.md):
- every stage passes, and every screen is identical;
- the audio is identical up to PLAY, then the same sound slightly shifted in
  time, as stock's own is when PLAY lands 0.3 ms later.

The 1.44 port is checked the same way, against stock 1.44 (from 2.3):
every stage passes, and every screen is identical. It is the same code with
1.44's addresses. Its eight sites hold the same stock bytes as 1.43's, and
each routine it calls was found again by its own code, with the addresses
in it masked, and checked against the code that calls it. It lints and
links, and a build with it verifies, depacking in place.

3.1 was checked the same way on 1.43 and 1.44: every stage passes and every
screen is identical. With the machines mod, SINE and digitables 1.4, on both
versions, SINE plays at the note through the filter and the AMP envelope
while the other tracks stay FM ([../machines-dn1](../machines-dn1)). Its two
new sites are the same code in 1.44 (the voices' site calls the routine
it calls at its new address), and the setter's last step, `SOUND_LIVE`, was
found again by its own code.

3.2 was checked the same way on 1.43 and 1.44: every stage passes, every
screen is identical, and the audio is identical up to PLAY. With THRU
([../../examples/dn-thru](../../examples/dn-thru)) open while the factory
pattern plays, the inputs reach the output at their own level and the FM
does not; after NO the FM is back. Its two new sites are the same code
in 1.44, 0x20 bytes later, as are the routines they call.

3.3's key LEDs and the block's parameters were checked in digiemu on 1.44:
against stock, both screens are identical and the audio differs from PLAY
on in the same 28 windows of 100 ms as the core without the two events.
With a test mod that lights trig key 1 green from `ev_key_leds` and sets
every voice's Amp VOL (slot 72) to 0 from `ev_render_params`, the factory
pattern plays at an RMS of 4-29 where core alone gives 1650-2015, and trig
key 1 is green while every other LED (the playhead's red included) is as
with core alone. Its two sites hold the same stock bytes in 1.43, the same
code 0x20 bytes earlier for the render's (not run on 1.43).

`ev_midi_cc` was checked with CCs sent into the emulator's MIDI input on
1.43 and 1.44, with a mod's handler that takes the CCs it maps: a CC it
takes stops at core's site, and one it lets go, or any CC while it has
nothing to take, goes on into the stock router. With the default
channels, channels 1-9 came as tracks 0-8 (all nine on 1.43, 1 and 4 on
1.44), the auto channel as the active track with `CORE_MIDI_CC_AUTO`,
and channel 12, which no track uses, never reached the site. The site's
six bytes and the router's exit are the same code in 1.44, 0x274 bytes
later. Core 3.2 links beside Tone+FX 3.0a, which patches the router's
entry.

3.3's exports add no code of their own (only its two events do), and it keeps all of the released 2.3 (`tests/test_sdk.py`). Each
new 1.43 and 1.44 value was found by its own code (a routine, its bytes
with the addresses in them masked, and the routines it calls the same)
or by every piece of code that names it (data), and the eleven library
routines were called through the values the built cores export, with
random arguments on each version's image, against Python: every call
agreed.
Insert audio (written as 3.4, released in 4.0) was checked the same way on
1.43 and 1.44: every stage passes, every screen is identical, and the audio
is identical up to PLAY. With TREMOLO
([../../examples/dn-tremolo](../../examples/dn-tremolo)) on while the factory
pattern plays, the pattern comes out with a 4 Hz wobble (its level's 4 Hz
part 0.26 of its mean, against 0.008 stock), and picking TREMOLO again gives
the stock sound back; on the cycle clock, the stock master stage takes
53,240 cycles of a block, core's conversion in and out 602 and TREMOLO
926.
`tests/test_sdk.py` checks that 4.0 keeps all of 3.2: the same sites,
tables, contributions and exports, with 3.3's and 4.0's added; and that 4.1
keeps all of the released 4.0, with the note queue's site and table added.

4.0 was checked the same way on 1.43 and 1.44: every stage passes and every
screen is identical, alone, with PROG, and with seven effects mods at once
(five pedals, DigiSwarm and DigiPrism: 243 KB of the area, more than the
old 128 KB could hold). In that build each opened from the Mod Menu in turn
owns the audio alone, plucked notes on the inputs come out through it, and
after NO the synths are back; with an insert running (a DigiPrism that stays
on), each exclusive one opened over it took the audio. Program changes went
in through the emulator's MIDI input on both versions, with PROG
([../../examples/dn-progchange](../../examples/dn-progchange)): channel 16's
were taken and the stock did not change the pattern; with MIDI CONFIG's
receive off the stock ignored them and PROG still saw them, flags 1 on the
auto channel; with it on, PROG CH IN's channel (AUTO, then channel 3) went on
to the stock with `CORE_MIDI_PC_STOCK` set, and another channel did not. The
TypeScript engine's build of the seven is byte for byte the Python's.

4.1.1 was checked by a first boot in digikit's emulator (`first_run`, which
waits for the UI to settle, 3000M instructions at most), with digihealth's
rows and its key-observer test fixture. With 4.1, one row settled at 1450M,
none at 1200M, and two (1.43 and 1.44), or one row and 0x64 bytes allocated
at the same point, never did: the upgrade's 14 steps took 14M instructions
each, then 48M, then 250M and more, while the UI task spent three times as
long in the allocator (3.3 splits a malloc against 0.8). With 4.1.1, two
rows on 1.43 and two and three on 1.44 settle at 1200-1250M, as none do; in
SETTINGS each row draws after SYSTEM and YES toggles it (SYSTEM INFO's
readout shows, REMOTE KEYS clears its box). `tests/test_sdk.py` checks that
4.1.1 keeps all of the released 4.1, with `core_additem` where it was, and
the Digitakt's core is the same build without `ROW_POOL`.
