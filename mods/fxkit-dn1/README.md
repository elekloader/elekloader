# fxkit (Digitone mk1)

What the effect mods for the Digitone mk1 and Digitone Keys share, OS 1.43
and 1.44: one effect at a time has the audio (exclusive, the synths muted),
with a mixer around it, two LFOs on its knobs and p-locks that follow the
pattern. Each effect stays a mod of its own, with its own Mod Menu entry
and page; fxkit plays it, and draws its MIXER and LFO pages and keeps its
LOCK mode. It is built on core-dn1 4.1 ([../core-dn1](../core-dn1)). On its
own it adds nothing to the screen; [examples/dn-fx-gain](../../examples/dn-fx-gain)
is the effect to start from (docs/ADAPTING.md, "Effects").

| | |
|---|---|
| needs | core-dn1 4.1 (`resources.core`) |
| sites | none: it uses core's `ev_note_queue`, `ev_tick`, `ev_render_in` and `ev_render_out`, `core_audio` and `core_projdata`, and the stock LFO page's own drawers through `fw_uirecs` |
| table | `fx_effects`: an effect mod contributes a pointer to its `struct fx_effect` (`digitone-mk1/fxkit.h`) |
| exports | `fx_activate`, `fx_deactivate`, `fx_active`, `fx_chain`, `fx_get`, `fx_set`, `fx_set_later`, `fx_view`, `fx_turned`, the `fx_lock_*` calls, the `fx_page_*` calls, `fx_draw_controls`, `fx_knob_turn`, `fx_knob_push`, `fx_text`, `fx_pack`, `fx_unpack`, `fx_serial`, `fx_overload`, `fx_load` |
| project data | `FXKT`, 4 bytes: the lock track |
| RAM | 19 KB (13 KB code, 5.6 KB the effects' values, the lock queue and the buffers) |

## How it works

- **The active effect.** An effect's entry calls `fx_activate`: fxkit's
  `core_audio` owner plays it, the inputs through the mixer's input stage
  (IN L, IN R, L PAN, R PAN; the levels on the stock mixer's law for its
  inputs: 32 unity, 127 +24 dB), the effect, and its output stage (MIX,
  WDTH, BAL, VOL). Activating one puts the one before it off; `fx_deactivate`
  (the effect's NO) gives the synths back.
- **Its values**, each block at the render's entry: its own (fxkit keeps
  every effect's, by slot), the p-lock of the lock track's trig that plays,
  and the two LFOs on them, with the stock LFO's arithmetic.
- **The p-locks** are kept in the active pattern, in the lock track's lock
  table (T4 unless the effect's SETUP says otherwise), as the firmware's
  lock setter keeps them: the value, the step's count, the slot's used
  byte, and a lock trig on a step with no trig. The stock saves, copies
  and plays them with the pattern; `ev_note_queue` gives fxkit each trig of
  the lock track with its time, and its locks hold until the next.
- **The MIXER and LFO pages** (`fx_page_*`): the LFO pages are the stock
  LFO page, its own knob graphics (the stock's drawers, from the UI records
  core exports), WAVE and SPH as one wave, DEST's box and list (YES picks,
  NO closes), SPD and DEP in fine steps; a turned knob's value shows under
  it a while, in the stock's text. An effect gives them its keys first
  (`fx_page_key`) and redraws when `fx_page_state` moves.
- **LOCK mode** (`fx_lock_key`, from an effect's key handler): the trig
  keys are the steps of a 16-step page, PAGE the next; hold a trig and turn
  a knob for that step's lock, FUNC + a trig clears it.
- **Saving**: an effect packs its mixer and LFOs into its own project data
  (`fx_pack`, 23 bytes) when `fx_serial` says its values moved (packing
  every UI tick, seven effects slowed the UI task enough to stretch the
  first boot's +Drive work); fxkit saves the lock track.

## Not yet

- The trig LEDs do not show the locked steps: the page draws them in its
  title bar (`fx_lock_strip`).
- The lock track's own FM notes take the effect's locks too, and an
  arpeggiator on it turns its trigs into the arpeggiator's events: use a
  track that plays nothing in a pattern with an effect's locks (SETUP's
  LOCK TRACK).
