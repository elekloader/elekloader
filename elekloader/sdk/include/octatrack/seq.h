/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-events (mods/seq-events-ot) for the Octatrack: its sizes, then the API every device shares
 * (seq-events.h; docs/ADAPTING.md, "Step-fire events"):
 *
 *     #include "octatrack/seq.h"
 *
 * Tracks 0-7 are the audio tracks T1-T8, 8-15 the MIDI tracks M1-M8. An audio track plays a
 * sample, not a note: ev_gen_notes, ev_fire_notes and ev_fire_vel run on the MIDI tracks only,
 * and an audio track's lock slots are the page-1 knobs (below). The Octatrack keeps no keyboard
 * key: sq_track_info gives SQ_ROOT_NONE and SQ_SCALE_NONE. */
#ifndef ELEKLOADER_OCTATRACK_SEQ_H
#define ELEKLOADER_OCTATRACK_SEQ_H

#define SQ_AUDIO  8                     /* tracks 0-7: T1-T8 */
#define SQ_TRACKS 16                    /* then 8-15: M1-M8 */
#define SQ_STEPS  64
#define SQ_NOTES  4                     /* a MIDI track's notes: NOTE and NOT2-NOT4 as intervals */
#define SQ_SLOTS  30                    /* a step's lock bytes, 0 to SQ_SLOTS - 1: the page-1 knobs
                                         * in order, PLAYBACK 0-5, LFO 6-11, AMP 12-17 (VOL is 15),
                                         * FX1 18-23, FX2 24-29; value >> 8 is the byte, 0-127 (a
                                         * bipolar knob stores its panel value + 64) */

#include "seq-events.h"

/* ev_step_built (the Octatrack's only): the stock builder has staged a track's next step. The tick
 * calls builder(track, bank, pattern, step, n) for every track at the start of the step before the
 * step; the builder writes the record the voice will play into the sequencer's staging words, and
 * the tick copies them into the pending slot n and clears them (docs/ADAPTING.md, "Step-fire events";
 * mods/seq-events-ot/README.md). This event runs between the two, for every build of every track, at
 * interrupt level or in the UI task (an edit of a step builds in a task): after the builder, after
 * the step stages (ev_fire_*) and with the pattern as stored again. It runs also when no step
 * stage has a handler. Keep it short and call no firmware.
 *
 *     void f(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n, const struct sq_built *rec)
 *
 * track: 0-15 as everywhere; bank, pattern: the ones the build read, 0-15; step: the step it read (what
 * ev_fire_step answered); n: the pending slot it fills, 0-2, or -1 for the first step at PLAY and for an
 * edit of a step (a staging row of its own). rec points at the words the build staged, where the tick
 * reads them: a change in place is a change in the record. Handlers run in order; each sees what the one
 * before left.
 *
 * Only *kind and *recorder are current for every build: the tick clears them after it copies them. The
 * other words (the audio and MIDI tracks of one number share them) are the last staged build's when the
 * build staged nothing: read them when *kind is not 0 (locks, deltas, length, span, time), or *recorder
 * is not 0 (rec_time). A build is not given a record it did not stage: setting *kind from 0 is not
 * supported, setting it to 0 is. A build from a task can be interrupted by the tick's, which stages the
 * same words (the firmware does not mask it): edit a word only when *kind or *recorder says the build
 * staged it, as an edit of an unstaged word is not seen. */
struct sq_built {
    uint8_t *locks;             /* the 32 lock bytes, 0xff no lock. An audio track's are the page-1 knobs, slot
                                 * 0 to SQ_SLOTS - 1 (15 is AMP VOL); a MIDI track's, its NOTE page first
                                 * (0 NOTE, 1 VEL, 2 LEN, 3-5 NOT2-NOT4) */
    int8_t *deltas;             /* audio tracks: for a slide, each lock's difference to the next trig's lock, a
                                 * signed byte (32 of them; 0 where there is none); MIDI tracks: null */
    uint32_t *kind;             /* 0: nothing staged (no trig, or its condition failed); 0x1d a trigger trig,
                                 * 0x19 a trigless trig, 0x3d an audio track's one-shot trig, 1 a lock trig */
    uint32_t *length;           /* the length index: the timing halfword's length bits + 1 (1 for the other
                                 * kinds of trig) */
    int32_t *span;              /* the trig's length in the sequencer's time units: a step's, divided by the
                                 * length index */
    int32_t *time;              /* the trig's time from its step's start, in the same units: its micro-timing and
                                 * its swing; 0 for neither */
    uint32_t *recorder;         /* audio tracks: the recorder trigs staged for the step, 0 none: 0x1000, 0x2000
                                 * and 0x4000 for the three recorder trig masks, and 0x20 with any (0x100
                                 * too with a fourth mask); MIDI tracks: null. Set it to 0 and no recording
                                 * starts */
    int32_t *rec_time;          /* audio tracks: the recorder trig's time, like *time; MIDI tracks: null */
};
typedef void (*ev_step_built_fn)(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n,
                                 const struct sq_built *rec);

#endif
