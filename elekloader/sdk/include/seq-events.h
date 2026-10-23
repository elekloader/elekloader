/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-events: a sequencer's step fire as events, so several mods that change what it plays combine
 * (docs/ADAPTING.md, "Step-fire events"). The names are the same on every device seq-events serves;
 * include the device's header, which gives its sizes and includes this one:
 *
 *     #include "digitone-mk1/seq.h"
 *
 * with "requires": ["core", "seq-events"] and a "subscribe" entry per event. Every event runs at
 * interrupt level, in the sequencer's step fire: keep it short and call no firmware. track: the
 * audio tracks from 0 (SQ_AUDIO of them), then the MIDI tracks, up to SQ_TRACKS; step: 0 to
 * SQ_STEPS - 1.
 *
 * A stage with two phases runs every generator (ev_gen_: what the step is) before
 * every effect (ev_fire_: what is done to it), whatever their order. Each handler changes in place
 * what the one before left, the first what the stock step made; seq-events checks the value after
 * each handler. first: 1 at the trig test's read of the step, 0 at the fire's second read; both
 * reads of a step must get the same answer. */
#ifndef ELEKLOADER_SEQ_EVENTS_H
#define ELEKLOADER_SEQ_EVENTS_H

#include <stdint.h>

#if !defined(SQ_AUDIO) || !defined(SQ_TRACKS) || !defined(SQ_STEPS) || !defined(SQ_NOTES) || \
    !defined(SQ_SLOTS)
#error "include the device's seq.h (digitone-mk1/seq.h), which gives its sizes"
#endif

#define SQ_TRIG   1u                    /* *trig: a note trig */
#define SQ_LOCK   2u                    /* a lock trig; seq-events keeps only these two bits and maps
                                         * them to the firmware's own */

/* ev_fire_step: the step the fire reads. step: the step so far (the first handler gets the real
 * one); len: the track's length, 1 to SQ_STEPS. -> the step to read; one outside 0..len-1 is
 * ignored */
typedef int32_t (*ev_fire_step_fn)(int32_t track, int32_t step, int32_t len);

/* ev_gen_trig, ev_fire_trig: does the step fire. *trig: its bits so far, the first generator
 * gets the stored step's */
typedef void (*ev_gen_trig_fn)(int32_t track, int32_t step, int32_t first, uint32_t *trig);
typedef void (*ev_fire_trig_fn)(int32_t track, int32_t step, int32_t first, uint32_t *trig);

/* ev_fire_cond: a trig condition's or probability's outcome. *pass: 1 passed, 0 failed, -1 a
 * probability not asked yet; leave -1 and the probability decides */
typedef void (*ev_fire_cond_fn)(int32_t track, int32_t step, int32_t *pass);

/* ev_gen_notes, ev_fire_notes: what a fired note trig plays, in seq-events' buffer. notes[0] the
 * note (-1 none), notes[1..SQ_NOTES-1] that are not 0 intervals added to it (a chord). The first
 * generator gets the stored notes, or the track's NOTE alone for a generated trig (it fires with
 * no stored note trig) */
typedef void (*ev_gen_notes_fn)(int32_t track, int32_t step, int32_t generated,
                                int8_t notes[SQ_NOTES]);
typedef void (*ev_fire_notes_fn)(int32_t track, int32_t step, int8_t notes[SQ_NOTES]);

/* ev_fire_vel: the velocity, 0-127 (clamped after each handler) */
typedef void (*ev_fire_vel_fn)(int32_t track, int32_t step, int32_t *vel);

/* ev_fire_locks: parameter locks added to the step (audio tracks). out: room free entries.
 * -> the entries written. A slot outside 0..SQ_SLOTS-1 is dropped; of a slot written twice, the
 * later wins; SQ_LOCKS a step, from all handlers */
#define SQ_LOCKS 8
struct sq_lock { uint16_t slot; int16_t value; };      /* a sound slot, its value 8.8 */
typedef int32_t (*ev_fire_locks_fn)(int32_t track, int32_t step, struct sq_lock *out, int32_t room);
/* ev_fire_len (0.3, the Digitakt mk1 on OS 1.53): the note's length, the firmware's LEN as an index,
 * 0-126 (0.125 steps up to 128, as the TRIG page lists them) or 127 INF; *len: the step's own, or the
 * track's default when it has none (clamped after each handler) */
#define SQ_LEN_INF 127
typedef void (*ev_fire_len_fn)(int32_t track, int32_t step, int32_t *len);

/* The step clock: per track the last real step the fire loaded and a parity that flips on each
 * wrap (a smaller step than the last; every step on a 1-step track), so mods agree on which loop
 * plays, whatever a step handler reads. Written at interrupt level, the parity before the step;
 * read it in the UI task. */
#define SQ_CLOCK_NONE 0xffffffffu       /* last: no step seen yet */
struct sq_clock {
    volatile uint32_t parity;
    volatile uint32_t last;
};
extern struct sq_clock sq_clock[SQ_TRACKS];

/* A track of the pattern the sequencer plays, read when called: UI task or interrupt level, a few
 * loads. In a fire, the pattern that fire reads. Fills at most size bytes of out (a mod built
 * against a shorter struct gets its fields). -> 0, or -1: no pattern yet (out untouched) */
#define SQ_ROOT_NONE  0xff              /* root: the device keeps no key for the track */
#define SQ_SCALE_NONE (-2)              /* scale: the same (-1 is chromatic) */
struct sq_track {
    int32_t len;                        /* 1 to SQ_STEPS, as it plays now */
    int32_t master;                     /* the steps after which the pattern restarts (normal mode:
                                         * the length), 0 never (per-track mode's INF) */
    uint8_t note;                       /* NOTE */
    uint8_t root;                       /* the keyboard's ROOT, a MIDI note; SQ_ROOT_NONE: none (a
                                         * mod that needs a key checks for it) */
    int8_t scale;                       /* the keyboard's SCALE: -1 chromatic, from 0 the device's
                                         * scales; SQ_SCALE_NONE with SQ_ROOT_NONE */
    uint8_t pad;
};
extern int32_t sq_track_info(int32_t track, struct sq_track *out, uint32_t size);
/* track's stored note trigs, bit s = step s; 0 with no pattern. UI task: a loop over the steps */
extern uint64_t sq_stored(int32_t track);
/* UI task: these tracks' next steps changed (bit t: track t). While stopped, the sequencer
 * prepares the first step again at the end of this UI tick (ev_tick order 90), once whatever
 * asked */
extern void sq_rearm(uint32_t tracks);
/* The pattern buffer track's fire reads (0.2): in a fire handler, the one this fire is reading (at a
 * pattern switch possibly the next pattern, not yet the active one); outside a fire, the one its last
 * fire read; 0 before any. For per-pattern settings: a pattern is known by its buffer */
extern const void *sq_fire_pattern(int32_t track);

#endif
