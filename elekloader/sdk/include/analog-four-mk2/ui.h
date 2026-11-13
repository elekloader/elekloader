/* SPDX-License-Identifier: GPL-2.0-or-later */
/* std1 on the Analog Four MKII (core-a4mk2 0.7, OS 1.56): what the A4's UI
 * and system events pass (elekloader/sdk/include/std1.h;
 * mods/core-a4mk2/README.md, "Standard events (0.7)"), and the A4's own turn
 * event, ev_knob_turn. std1 has not numbered modes, mute scopes or clipboard
 * kinds yet, so these are the A4's. */
#ifndef ELEKLOADER_A4_UI_H
#define ELEKLOADER_A4_UI_H

#include <stdint.h>

/* ---- ev_mode(mode, old, new): the UI state's mode bytes, old and new as
 * stock stores them (0 off). Compared at each tick, so a mode set and
 * cleared within one tick (1/CORE_TICK_HZ) is not heard. GRID_REC and
 * STEP_REC are stored while armed; LIVE_REC takes effect only while playing. */
#define MODE_KEYBOARD 0                 /* the keyboard mode (UI state +0x160) [read] */
#define MODE_GRID_REC 1                 /* RECORD: grid recording (+0x168) */
#define MODE_LIVE_REC 2                 /* RECORD + PLAY: live recording (+0x169) */
#define MODE_STEP_REC 3                 /* step recording (+0x16a) */
#define MODE_COUNT 4

/* ---- ev_mute(track, muted, scope): the A4's mutes are the project's */
#define MUTE_SCOPE_PROJECT 0

/* ---- ev_clip(pre, c) */
#define CLIP_PATTERN 0                  /* a pattern (clear, paste) */
#define CLIP_TRACK 1                    /* a track's sequence: FUNC+REC/STOP/PLAY in grid record */
#define CLIP_KIT 2
#define CLIP_SOUND 3                    /* a sound in the project's pool */
#define CLIP_SONG 4
#define CLIP_TRACK_SOUND 5              /* the track's sound in the kit; track says which */
#define CLIP_COPY 0                     /* to the clipboard */
#define CLIP_PASTE 1                    /* from the clipboard */
#define CLIP_CLEAR 2
#define CLIP_NONE 0xffu                 /* track or slot: not known */
struct core_clip {
    uint8_t kind;                       /* CLIP_PATTERN .. CLIP_TRACK_SOUND */
    uint8_t op;                         /* CLIP_COPY, CLIP_PASTE or CLIP_CLEAR */
    uint8_t track;                      /* params.h's track, or CLIP_NONE */
    uint8_t slot;                       /* CLIP_NONE on the A4 so far */
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
 * changes the pattern at once), TEMP JUMP PATTERN_START. A select while the
 * view edits a chain reports PATTERN_QUEUE in every mode. */
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

/* ---- ev_analog_frame(words), a native event (not std1): the analog
 * control frame, 100 words, after stock built it and before it is packed
 * for the analog board, in the audio interrupt, every block. A handler may
 * change the words; it must be short. Which word drives what is not mapped.
 * A mod subscribes by contributing to the "ev_analog_frame" table. */
typedef void (*ev_analog_frame_fn)(uint16_t *words);

#endif
