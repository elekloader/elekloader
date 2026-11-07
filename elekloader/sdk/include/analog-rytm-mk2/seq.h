/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: the sequencer's events and getters (docs/ADAPTING.md, "The
 * sequencer (core-ar2 0.11, Analog Rytm MKII)"). Events are subscribed in
 * mod.json, {"event": "ev_seq_step", "fn": "my_step"}; calls are core's
 * exports. The events run in the sequencer clock's interrupt: keep them
 * short, never wait, and send MIDI only with core_midi_try_send (sent at
 * once when the sender has room, else queued).
 */
#ifndef ELEKLOADER_AR2_SEQ_H
#define ELEKLOADER_AR2_SEQ_H

#include <stdint.h>

/* ev_seq_step: void f(uint32_t steps, uint32_t trigs), at the end of a clock
 * tick where any track reached a step boundary. Bit t of steps: track t
 * (0..12, 12 the FX track) made its next step's trig, one step ahead of the
 * sound (the step after core_track_step(t)); bit t of trigs: that step
 * plays a trig, a trigless lock trig too (core_step_trig tells them
 * apart). A step without a trig, a failed condition (FILL, PRE, a ratio)
 * and a failed probability are no trig. Microtiming and retrigs don't add
 * calls: they ride in the trig. Each track keeps its own speed, so the
 * masks hold only the tracks at a boundary this tick.
 * The first step after PLAY comes as its own call first, at the tick the
 * play state becomes 1: steps 0x1fff | CORE_SEQ_FIRST, trigs the tracks
 * whose step-1 trig stock had made before PLAY (its condition evaluated).
 * Their mod locks are set then too (core_lock_value). The same tick's
 * ordinary call, for step 2, follows it. */
#define CORE_SEQ_FIRST 0x80000000u

/* ev_seq_tick: void f(void), each clock tick (96 a quarter note), playing or
 * stopped, after ev_seq_step. */

/* ev_transport: void f(int32_t state), at the first tick the play state
 * differs from the last tick's: core_transport()'s new value. */

/* the play state: 1 playing, 0 stopped */
int core_transport(void);
/* the pattern playing (0..127), and the one it changes to next: a pattern
 * selected while playing, or a chain's next; the one playing when none is
 * queued. -1 for none */
int core_pattern_now(void);
int core_pattern_next(void);
/* the tempo, BPM x 120 */
unsigned core_tempo(void);
/* track t's speed as the clock plays it, 0..6: 0 2x, 1 3/2x, 2 1x, 3 3/4x,
 * 4 1/2x, 5 1/4x, 6 1/8x (a 1x step is 24 ticks, a 1/2x one 48); a change
 * takes effect at the track's next step. And the step starting now (0..63).
 * -1 for a track out of range */
int core_track_speed(int track);
int core_track_step(int track);
/* the length track t's steps wrap at as the clock plays the pattern now
 * playing: the track's own in per-track scale mode, else the pattern's.
 * -1 for a track out of range or no pattern playing */
int core_track_length(int track);
/* step (0..63) of track t in live pattern p (0..127), as stored:
 * CORE_STEP_NOTE a trig that triggers the sound, CORE_STEP_LOCK a trigless
 * lock trig, 0 none, -1 out of range. A track in euclidean mode answers
 * from its map (note trigs only). Before conditions and probability, unlike
 * ev_seq_step's trigs. ev_seq_step's steps are one ahead of the sound, so
 * at a pattern change a step it reports may be in core_pattern_next(), not
 * core_pattern_now(): pass the one the step belongs to. While the change
 * copies the new pattern in, a read may see either pattern's step. Any
 * context. */
#define CORE_STEP_NOTE 1
#define CORE_STEP_LOCK 2
int core_step_trig(int pattern, int track, int step);

/* audio blocks: core_block in analog-rytm-mk2/load.h */

#endif
