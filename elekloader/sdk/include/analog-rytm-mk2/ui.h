/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-ar2 0.16: the UI skip's switch, and from 0.17 what std1's UI and
 * system events pass (below). The skip's switch (mods/core-ar2/cast.s,
 * mods/core-ar2/SKIPS.md, "The UI skip (0.16)").
 *
 * The skip runs only when a mod asks for it and no mod holds it off:
 * core_ui_skip_want nonzero and core_ui_skip_hold 0. It is off at boot.
 * It changes nothing the unit does or shows, only the CPU the UI takes, so
 * the mod that offers it as a setting writes the user's choice into
 * core_ui_skip_want; nothing else writes it. A byte, read once per cast, so
 * any context may write it at any time.
 *
 * A mod that measures stock's UI load calls core_ui_skip_hold_add() when it
 * starts and core_ui_skip_hold_release() when it ends, once each. Each call
 * is one instruction, so any context may call them.
 */
#ifndef ELEKLOADER_AR2_UI_H
#define ELEKLOADER_AR2_UI_H

#include <stdint.h>

extern volatile uint8_t core_ui_skip_want;
extern volatile uint32_t core_ui_skip_hold;
void core_ui_skip_hold_add(void);
void core_ui_skip_hold_release(void);

/* casts answered from the memo, and casts that ran stock's search, since
 * boot (counted only while the skip is on) */
extern volatile uint32_t core_ui_skip_hits;
extern volatile uint32_t core_ui_skip_misses;

/* ---- std1's UI and system events (core-ar2 0.17; mods/core-ar2/ui.c,
 * README, "Standard events (0.17)"). std1 leaves modes, mute scopes and
 * clipboard kinds to each device; the Rytm's follow core-a4mk2's
 * (analog-four-mk2/ui.h) where the Rytm has the same thing. */

/* ev_mode(mode, old, new): the UI state's mode bytes, old and new as stock
 * stores them (0 off), at the A4's offsets (decided 2026-11-19). Compared at
 * each tick, so a mode set and cleared within one tick (1/CORE_TICK_HZ) is
 * not heard. [read] the setters; what each shows on the unit is to confirm. */
#define MODE_TRIG_HELD 0                /* UI state +0x160 */
#define MODE_GRID_REC 1                 /* RECORD: grid recording (+0x168) */
#define MODE_LIVE_REC 2                 /* RECORD + PLAY: live recording (+0x169) */
#define MODE_STEP_REC 3                 /* +0x16a: its setter also turns grid recording on
                                         * and clears 13 per-track words */
#define MODE_COUNT 4

/* ev_mute(track, muted, scope): track 0..12 (12 the FX track), the bits
 * core_track_mutes reads (moments.h) */
#define MUTE_SCOPE_TRACKS 0

/* ev_clip(pre, c). pre 1 before stock's routine, nonzero takes it (stock
 * does nothing and shows nothing); pre 0 after it, only when it acted. An
 * UNDO PASTE or UNDO CLEAR brings neither. Not reported yet on the Rytm:
 * sounds, pages, songs and chains, scenes, performances, projects. */
#define CLIP_PATTERN 0                  /* a pattern: the pattern view's COPY, PASTE, CLEAR, and the
                                         * pattern select's, once per selected pattern */
#define CLIP_TRACK 1                    /* the active track's sequence, in grid record; track says which */
#define CLIP_KIT 2                      /* a kit in the kit list, on YES (paste and clear); slot says which */
#define CLIP_COPY 0
#define CLIP_PASTE 1
#define CLIP_CLEAR 2
#define CLIP_NONE 0xffu
struct core_clip {
    uint8_t kind;                       /* CLIP_PATTERN, CLIP_TRACK or CLIP_KIT */
    uint8_t op;                         /* CLIP_COPY, CLIP_PASTE or CLIP_CLEAR */
    uint8_t track;                      /* CLIP_TRACK: params.h's track; else CLIP_NONE */
    uint8_t slot;                       /* CLIP_KIT: 0..127; else CLIP_NONE */
};

/* ev_view(op, mgr, view, 0): the view stack, in the UI task, under the
 * manager's lock (a handler must not push or remove a view). mgr is the view
 * manager, view the view. Every op is reported, and the return value is not
 * used (a push cannot be taken here). Reported: a push, after the view is in
 * the stack and before its own on-push; a remove (close, the sweep of views
 * that are done, a replace's old view) before its on-remove, and the stack's
 * last view popped. A replace's new view comes as a VIEW_PUSH. Moving a view
 * to the top brings nothing. */
#define VIEW_PUSH 0
#define VIEW_POP 2

/* ev_pattern(pre, pattern, how): pre 0 only on the Rytm so far, when the
 * playing pattern changed (how PATTERN_CHANGED), pattern 0..127, in the UI
 * task; the requests (pre 1) are not reported yet. */
#define PATTERN_CHANGED 0
#define PATTERN_NONE 0xffu

/* the USB mode of the last ev_usb message (stock's own number; 0 before the
 * first) */
unsigned core_usb_mode(void);

#endif
