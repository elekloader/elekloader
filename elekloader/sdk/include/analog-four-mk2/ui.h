/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): what the A4's UI
 * and system events pass (elekloader/sdk/include/std1.h;
 * mods/core-a4mk2/README.md, "Standard events (0.7)"), and the A4's own turn
 * event, ev_knob_turn, and (0.8) ev_track_key, ev_voice_params and
 * ev_sound_setup. std1 has not numbered modes, mute scopes or clipboard
 * kinds yet, so these are the A4's. */
#ifndef ELEKLOADER_A4_UI_H
#define ELEKLOADER_A4_UI_H

#include <stdint.h>

/* ---- ev_mode(mode, old, new): the UI state's mode bytes, old and new as
 * stock stores them (0 off). Compared at each tick, so a mode set and
 * cleared within one tick (1/CORE_TICK_HZ) is not heard. GRID_REC and
 * STEP_REC are stored while armed; LIVE_REC takes effect only while playing. */
#define MODE_TRIG_HELD 0                /* a trig held in grid record (UI state +0x160); the A4
                                         * has no keyboard mode: KB keys always play notes */
#define MODE_GRID_REC 1                 /* RECORD: grid recording (+0x168) */
#define MODE_LIVE_REC 2                 /* RECORD + PLAY: live recording (+0x169) */
#define MODE_STEP_REC 3                 /* step recording (+0x16a) */
#define MODE_COUNT 4

/* ---- ev_mute(track, muted, scope): the A4's mutes are the project's */
#define MUTE_SCOPE_PROJECT 0

/* ---- ev_clip(pre, c) */
#define CLIP_PATTERN 0                  /* a pattern (copy, paste, clear) */
#define CLIP_TRACK 1                    /* a track's sequence: FUNC+REC/STOP/PLAY in grid record */
#define CLIP_KIT 2
#define CLIP_SOUND 3                    /* a sound in the project's pool */
#define CLIP_SONG 4
#define CLIP_TRACK_SOUND 5              /* the track's sound in the kit; track says which */
#define CLIP_PAGE 6                     /* the active track's current 16-step page, grid record: PAGE held + REC copy,
                                         * + STOP paste, + PLAY clear (an UNDO brings none) */
#define CLIP_TRIGS 7                    /* the held trigs of the active track: trigs held + REC copies, + STOP pastes (no clear) */
#define CLIP_CHAIN 8                    /* a SONG edit row (chain): the song editor's copy, clear and paste (an UNDO brings none) */
#define CLIP_PROJECT 9                  /* a project, slot says which: the PROJECT list's copy, paste and clear */
#define CLIP_SLOT_WORKING 0xfeu         /* slot: the working project */
#define CLIP_ROW_END 0xfdu              /* slot, CLIP_CHAIN: the song's unnumbered end row "__:" */
#define CLIP_COPY 0                     /* to the clipboard */
#define CLIP_PASTE 1                    /* from the clipboard */
#define CLIP_CLEAR 2
#define CLIP_NONE 0xffu                 /* track or slot: not known */
struct core_clip {
    uint8_t kind;                       /* CLIP_PATTERN .. CLIP_PROJECT */
    uint8_t op;                         /* CLIP_COPY, CLIP_PASTE or CLIP_CLEAR */
    uint8_t track;                      /* params.h's track, or CLIP_NONE */
    uint8_t slot;                       /* CLIP_PROJECT: 0..127 (list slot 1..128 less 1) or CLIP_SLOT_WORKING;
                                         * CLIP_CHAIN: the row number the song editor shows (00 is 0) or
                                         * CLIP_ROW_END; else CLIP_NONE */
};

/* ---- ev_view(op, ctrl, view, 0): the view stack, in the UI task, under
 * the stack's lock (a handler must not push or close a view). ctrl is the
 * view controller, view the view. On the A4 every op is reported, and the
 * return value is not used (a push cannot be taken here yet). A page key
 * swaps the page's view: a VIEW_POP of the old page, then a VIEW_PUSH of the
 * new. Boot pushes MainScreenView's children in a burst, before
 * ev_boot_done. core_view_name(view) is the view's class name, e.g.
 * "MainScreenView". */
#define VIEW_PUSH 0                     /* in the stack, before its own on-push */
#define VIEW_POP 2                      /* leaving the stack, before its own on-remove */
const char *core_view_name(const void *view);

/* ---- ev_pattern(pre, pattern, how). pre 0: the playing pattern changed
 * (how PATTERN_CHANGED, at the boundary). pre 1: a change is asked for,
 * before stock does it; nonzero takes it (stock does not). pattern 0..127,
 * or PATTERN_NONE. In the UI task, except PATTERN_SET from MIDI's multi map
 * and PATTERN_SPP, which come in the MIDI task. A stop drops a queued change
 * without an event. The pattern view's select reports once, before it
 * touches the song's row, so a take there leaves the playing pattern.
 * Playing, its report follows the pattern mode: PTN: SEQUENTIAL
 * reports PATTERN_QUEUE, DIRECT START and DIRECT JUMP PATTERN_SET (stock
 * changes the pattern at once), TEMP JUMP PATTERN_START. In CHAIN or SONG
 * mode, or with CHAIN held, a select writes the playing row's chain at the
 * view's chain cursor instead; it reports only when the cursor is on the
 * playing element: PATTERN_NOW stopped, PATTERN_SET in a direct mode,
 * PATTERN_QUEUE in SEQUENTIAL when that element plays again next (nothing
 * queued, and CHAIN mode off or a one-element chain); any other chain edit
 * changes no pattern and reports nothing. */
#define PATTERN_CHANGED 0               /* pre 0 */
#define PATTERN_QUEUE 1                 /* playing: the change waits for the boundary */
#define PATTERN_CANCEL 2                /* playing: a queued change is dropped (the playing pattern selected again, or PATTERN_NONE) */
#define PATTERN_NOW 3                   /* stopped: the pattern changes at once */
#define PATTERN_SET 4                   /* playing: the change happens at once: a pattern select with PTN: DIRECT START or DIRECT JUMP [G], or MIDI's multi map (a PAT.PLAY entry with CHG MODE D.START or D.JUMP, a note on the PERF channel) [G] */
#define PATTERN_START 5                 /* playing: a pattern select in the view with TEMP JUMP on (FUNC+BANK_D) [G] */
#define PATTERN_SPP 6                   /* stopped: a song position (MIDI SPP, a stop's return); pattern PATTERN_NONE */
#define PATTERN_NONE 0xffu

/* ---- the USB mode of the last ev_usb: 4 USB MIDI, 6 USB AUDIO/MIDI,
 * 7 OVERBRIDGE (0 before the first) */
unsigned core_usb_mode(void);

/* ---- ev_knob_turn, a native event (not std1): every turn of a parameter
 * page's knob that stock is about to apply, whichever edit it becomes, in
 * the UI task:
 *     int f(void *view, int id, int delta, int knob, int how)
 * id the parameter (stock's 0..PARAM_STOCK_LAST, or a mod's), delta in 8.8
 * (0x100 a whole step; a turn under the accelerator's step makes no call),
 * knob ENC_A..ENC_J, how below. Nonzero takes the turn: no sound edit, no
 * lock, no multi-track edit, and no later handler. A std1 mod subscribes to
 * it by contributing to the "ev_knob_turn" table. Not seen: a list
 * parameter's turn (its list opens first) and the views that handle their
 * knobs themselves. */
#define TURN_SOUND 0                    /* into the track's sound (then ev_param_turn) */
#define TURN_LOCK 1                     /* a held trig's or live record's p-lock */
#define TURN_MULTI 2                    /* every held track key's track (synth tracks) */
typedef int (*ev_knob_turn_fn)(void *view, int id, int delta, int knob, int how);
/* The track LEVEL encoder and the mixer page (the view of key 0xb3, its
 * knobs A..E for tracks 1..4 and FX) report on ev_knob_turn too, with the id
 * KNOB_ID_LEVEL(track): knob ENC_LEVEL or ENC_A + track, delta in 8.8, how
 * TURN_MULTI while a track key is held on the LEVEL encoder, else
 * TURN_SOUND. view is MainScreenView or the mixer page. Taken: the level
 * stays (the mixer page still shows its level popup). These are not std1 parameters (std1 has no track level id). */
#define KNOB_ID_LEVEL(t) (0x10000 + (t))

/* ---- ev_live_rec, a native event (not std1): live recording about to write
 * into track's pattern (UI task):
 *     int f(unsigned track, unsigned what)
 * Nonzero takes it: stock writes nothing, and no later handler runs. A mod
 * subscribes by contributing to the "ev_live_rec" table. Knob turns that
 * become live p-locks come on ev_knob_turn (TURN_LOCK) first. */
#define LIVE_REC_NOTE 1                 /* a played note becomes a trig (its length follows at the note-off) */
#define LIVE_REC_VALUE 2                /* note, velocity or length into the step's trig */
#define LIVE_REC_LOCK 3                 /* a live p-lock */
typedef int (*ev_live_rec_fn)(unsigned track, unsigned what);

/* ---- ev_analog_frame(words), a native event (not std1): the analog
 * control frame, 100 words, after stock built it and before it is packed
 * for the analog board, in the audio interrupt, every block. A handler may
 * change the words, and the pack sends what it leaves (checked on a unit by
 * frame-check-a4mk2); it must be short. Which word drives what is not mapped.
 * A mod subscribes by contributing to the "ev_analog_frame" table. */
typedef void (*ev_analog_frame_fn)(uint16_t *words);

/* ---- ev_mod_set(pre, m), a native event (not std1): a modulation slot's
 * destination or depth on the sound's VELOCITY, MOD WHEEL, PITCH BEND,
 * BREATH and AFTERTOUCH setups and the kit's PERF macros, at the six stock
 * setters every such edit goes through: the setup page's knobs (A..E the
 * destination, F..J the depth, FUNC+knob), its destination list, its keys
 * that move a slot's destination to another track's parameter. A turn of
 * knob A..E opens the destination list and each step in it is reported; YES
 * closes it with no call, NO (or another knob) writes the old destination
 * back, reported too [unit, T311]. In the UI task. pre 1 comes before the write, with the value asked for; nonzero
 * takes it (nothing is written, the page still shows its popup). pre 0
 * comes after, with the value stored. Not reported: PASTE MODULATIONS and
 * its undo (stock writes whole slots another way), project, kit and sound
 * loads (they don't go through these setters), and a PERF macro's depths
 * received over SysEx. A mod subscribes by contributing to the "ev_mod_set"
 * table. */
#define MODSRC_VELOCITY 0               /* track: the sound's track */
#define MODSRC_MOD_WHEEL 1
#define MODSRC_PITCH_BEND 2
#define MODSRC_BREATH 3
#define MODSRC_AFTERTOUCH 4
#define MODSRC_PERF 5                   /* track: the macro, 0..11 */
#define MODSRC_NONE 0xffu               /* a sound not in the active kit (none seen) */
#define MOD_DEST 0                      /* value: the parameter (0..PARAM_STOCK_LAST) */
#define MOD_DEPTH 1                     /* value: signed 8.8 (0x100 one step), -0x8000..0x7f00 */
struct core_mod_set {
    uint8_t source;                     /* MODSRC_* */
    uint8_t slot;                       /* 0..4, the page's knob column */
    uint8_t kind;                       /* MOD_DEST or MOD_DEPTH */
    uint8_t track;                      /* params.h's track (0..3, TRACK_FX, TRACK_CV), the macro for MODSRC_PERF, or 0xff */
    int32_t value;                      /* pre: asked for (depth before the clamp); post: stored (destination as asked) */
};
typedef int (*ev_mod_set_fn)(int pre, const struct core_mod_set *m);

/* ---- ev_track_key, a native event (not std1, core-a4mk2 0.8): a key the
 * TrackSelectionView (the track keys' view) gets, at its key virtual
 * (0x400c8b90), before stock acts on it, in the UI task:
 *     int f(void *view, void *event, int key, int pressed)
 * view that view, event stock's KeyEvent, key its code (keys.h's ids),
 * pressed 1 for a press, 0 for a release. Views are asked newest first and
 * the first to take a key stops the walk, so this sees the keys the views
 * pushed after it pass on: a menu (GLOBAL, KIT, SOUND SETUP) takes 0x40..0x45
 * and 0x70..0x7b but passes the track keys; MainScreenView's later children
 * (QuickPerformance, PatternSelection, QuickMuteMenu, HelpBubble) come first.
 * Core's ev_key, at the panel, sees every key before any view. Nonzero takes the key: stock
 * does nothing with it, reports it handled, and no later handler sees it. A
 * mod subscribes by contributing to the "ev_track_key" table.
 * The helpers take that view (from ev_track_key, in the UI task):
 *   core_track_held  the track keys held: bit k = track k (0..5), set
 *                         on key 0x20 + k's press, cleared on its release
 *   core_track_current        the current track, params.h's (0..3, TRACK_FX,
 *                         TRACK_CV), or -1
 *   core_track_pattern  the current track's TrackPattern, what stock's
 *                         COPY, PASTE and CLEAR TRACK work on, or 0
 *   core_popup            stock's pop-up on the view, showing text */
typedef int (*ev_track_key_fn)(void *view, void *event, int key, int pressed);
unsigned core_track_held(void *view);
int core_track_current(void *view);
void *core_track_pattern(void *view);
void core_popup(void *view, const char *text);

/* ---- ev_voice_params, a native event (not std1, core-a4mk2 0.8): the four
 * voices' parameter words after stock added their modulation (knob, p-locks,
 * LFOs, envelopes) and clamped them to 0..0x7fff, before it builds the VCA,
 * pan and filter words of the analog frame from them, in the audio
 * interrupt, every block:
 *     void f(uint16_t *words, const int32_t *voice_track)
 * A handler may change the words; the frame is built from what it leaves.
 * It must be short. VOICE_WORD(words, p, v) is parameter p of voice v (0..3);
 * voice_track[v] is the voice's track. A mod subscribes by contributing to
 * the "ev_voice_params" table. */
#define VOICE_WORD(w, p, v) ((w)[4 * (p) + (v)])
#define VP_F1_FREQ 24
#define VP_F1_RES 25
#define VP_F2_FREQ 28
#define VP_F2_RES 29
typedef void (*ev_voice_params_fn)(uint16_t *words, const int32_t *voice_track);

/* ---- ev_sound_setup, a native event (not std1, core-a4mk2 0.8): SOUND
 * SETUP (FUNC + OSC1) being built, after F1 RESONANCE BOOST's row, in the UI
 * task:
 *     void f(void *menu)
 * A handler adds rows with core_sound_setup_toggle(menu, t): a checkbox row
 * named t->name (as core_setting's names), drawn checked when t->get(t)
 * returns 1, unchecked on 0, with no box below 0; YES flips it and the
 * encoder sets it, through t->set(t, on). Rows follow BOOST's in the
 * handlers' order. Keep t (and its name) in static storage. A mod subscribes
 * by contributing to the "ev_sound_setup" table. */
struct core_toggle {
    const char *name;
    int (*get)(const struct core_toggle *t);
    void (*set)(const struct core_toggle *t, int on);
};
typedef void (*ev_sound_setup_fn)(void *menu);
void core_sound_setup_toggle(void *menu, const struct core_toggle *t);

/* ---- Audio events (native, not std1, core-a4mk2 0.9), in the audio interrupt,
 * every block of 32 samples. Handlers follow the C ABI, must be short, and
 * leave the EMAC unit (accumulators, MACSR, MASK) as they found it.
 * voice_track[v] is voice v's track (0..3 synth).
 *
 * ev_fx_in(frames, out, voice_track): before the effects block. frames holds
 * CORE_FX_FRAMES frames of CORE_FX_FRAME_SLOTS int32; voice v (0..3) at slot
 * CORE_FX_VOICE_SLOT + v, the inputs at slots 6 and 7. A handler may rewrite
 * the voice slots: the sends (chorus, delay, reverb) hear what it leaves.
 * Handlers chain by order. out still holds the last block's output.
 * ev_fx_out(out, frames, voice_track): after the effects block. out is its
 * output, CORE_FX_FRAMES stereo pairs (L, R int32). Handlers add to it and
 * saturate (sats.l), so their order does not matter; frames may already be
 * overwritten, so a handler that needs its ev_fx_in input keeps a copy.
 *
 * ev_noise_fade(env, voice_track): in the noise generator, after the fade
 * envelopes (env[v], 0..0x3fffffff) are updated, before the levels are taken
 * from them; a handler may set them.
 * ev_noise_mix(bufs, voice_track): at the start of the noise generator's final
 * mix; bufs + CORE_NOISE_VSTRIDE / 4 * v is voice v's record: a 12-byte filter
 * history, then three sources of 32 int32 at +0, +0x230 and +0x460 bytes from
 * bufs (+ the voice's offset), which the mix crossfades by COL, scaled by the
 * noise level and the fade. A handler may write them.
 *
 * core_dry_mute: bit v set = voice v's dry VCA words get volume 0 in the next
 * block (as VOL 0; its sends and capture are kept). Core clears it in
 * ev_render_out at order 0; a mod sets its bits in its own ev_render_out
 * handler. Bits are ORed between mods.
 *
 * Mods subscribe by contributing to the "ev_fx_in", "ev_fx_out",
 * "ev_noise_fade" and "ev_noise_mix" tables. */
#define CORE_FX_FRAMES 32
#define CORE_FX_FRAME_SLOTS 8
#define CORE_FX_VOICE_SLOT 2
#define CORE_NOISE_VSTRIDE 140
typedef void (*ev_fx_in_fn)(int32_t *frames, int32_t *out, const int32_t *voice_track);
typedef void (*ev_fx_out_fn)(int32_t *out, int32_t *frames, const int32_t *voice_track);
typedef void (*ev_noise_fade_fn)(int32_t *env, const int32_t *voice_track);
typedef void (*ev_noise_mix_fn)(int32_t *bufs, const int32_t *voice_track);
extern volatile uint8_t core_dry_mute;

/* ---- core_knob_names (core-a4mk2 0.9): a mod's names for knob ids of one
 * parameter set kind (0 synth, 1 FX, 2 CV), stock ids included,
 * asked before core's lock-param names and stock's, in contribution order.
 * name(id, long_name) returns the name, or 0 for an id the entry leaves to the
 * next. Called in the UI task. A mod contributes a pointer to its entry. */
struct core_knob_name {
    uint16_t first_id, last_id;
    uint8_t kind;
    uint8_t pad[3];
    const char *(*name)(int32_t id, int32_t long_name);
};

#endif
