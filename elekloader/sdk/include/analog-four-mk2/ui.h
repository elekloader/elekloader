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
#define CLIP_PAGE 6                     /* a page of the pattern's trigs (TRIG PAGE copy) */
#define CLIP_CHAIN 7                    /* the pattern chain */
#define CLIP_PROJECT 8                  /* a project slot (the PROJECT menu's copy) */
#define CLIP_COPY 0                     /* to the clipboard */
#define CLIP_PASTE 1                    /* from the clipboard */
#define CLIP_CLEAR 2
#define CLIP_NONE 0xffu                 /* track or slot: not known */
struct core_clip {
    uint8_t kind;                       /* CLIP_PATTERN .. CLIP_PROJECT */
    uint8_t op;                         /* CLIP_COPY, CLIP_PASTE or CLIP_CLEAR */
    uint8_t track;                      /* params.h's track, or CLIP_NONE */
    uint8_t slot;                       /* CLIP_NONE on the A4 so far */
};

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

#endif
