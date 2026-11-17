/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.9: the sequencer's events and getters (mods/core-a4mk2/README.md,
 * "The sequencer"), the same names and arguments as the Analog Rytm
 * MKII's (analog-rytm-mk2/seq.h) for the A4's 6 tracks: synth 0..3, FX 4,
 * CV 5. Events are subscribed in mod.json, {"event": "ev_seq_step", "fn":
 * "my_step"}; calls are core's exports. The events run in the sequencer
 * tick's interrupt: keep them short and never wait.
 */
#ifndef ELEKLOADER_A4MK2_SEQ_H
#define ELEKLOADER_A4MK2_SEQ_H

#include <stdint.h>

/* ev_seq_step: void f(uint32_t steps, uint32_t trigs), at the end of a tick
 * where any track reached a step boundary. Bit t of steps: track t (0..5)
 * made its next step's trig, one step ahead of the sound; bit t of trigs:
 * that step plays a trig, a trigless lock trig too. A step without a trig,
 * a failed condition and a failed probability are no trig. Microtiming and
 * retrigs don't add calls. Each track keeps its own speed, so the masks
 * hold only the tracks at a boundary this tick (the sequencer steps on
 * every 4th tick, 24 a quarter note: a 1x step is 24 ticks).
 * The first step after PLAY carries CORE_SEQ_FIRST: from stop, the PLAY
 * tick's own call (every track starts there). A resume (PLAY after a
 * pause, MIDI Continue) plays the step it stopped on from trigs stock made
 * at the pause; it comes as its own call first, steps 0x3f |
 * CORE_SEQ_FIRST, trigs the tracks whose step plays a trig, and the same
 * tick's ordinary call follows it. */
#define CORE_SEQ_FIRST 0x80000000u

/* ev_seq_tick (std1): each tick (96 a quarter note), playing or stopped,
 * after ev_seq_step. */

/* ev_transport: void f(int32_t state), at the first tick the play state
 * differs from the last tick's: core_transport()'s new value. A stop or
 * pause made between ticks is seen at the next tick; with an external
 * clock that stops arriving, there is no next tick until it comes back. */

/* the play state: 1 playing, 0 stopped, 2 paused (the Rytm's has no 2) */
#define CORE_TRANSPORT_STOPPED 0
#define CORE_TRANSPORT_PLAYING 1
#define CORE_TRANSPORT_PAUSED 2
int core_transport(void);

/* the pattern playing (0..127), and the one it changes to next: a direct
 * switch waiting for its moment, else what the pattern's end will choose,
 * read without changing anything: the queued pattern, else in CHAIN mode the
 * chain's next (its head after the last), in SONG mode the same within the
 * row and, once the row's repeats are done, the next row's first; with
 * none of them the one playing. -1 for none, or a song that ends there
 * (playback stops) */
int core_pattern_now(void);
int core_pattern_next(void);
/* the tempo, BPM x 120 */
unsigned core_tempo(void);
/* track t's speed as the tick plays it, 0..6: 0 2x, 1 3/2x, 2 1x, 3 3/4x,
 * 4 1/2x, 5 1/4x, 6 1/8x (a 1x step is 6 sequencer clocks, 24 ticks); a
 * change takes effect at the track's next step. And the step sounding now
 * (0..63); -1 also from PLAY's setup until the first step sounds, and for a
 * track out of range. The step a trig plays may differ from it: PLAYING
 * PAGES and a euclidean track's map choose another step's trig */
int core_track_speed(int track);
int core_track_step(int track);
/* the length track t's steps wrap at as the tick plays the pattern now
 * playing: the track's own in per-track scale mode, else the pattern's,
 * as stored. -1 for a track out of range or no pattern playing */
int core_track_length(int track);

/* step (0..63) of track t in live pattern p (0..127), as stored:
 * CORE_STEP_NOTE a note trig, CORE_STEP_LOCK a trigless lock trig, 0 none
 * (a step stock marks not to play counts as none), -1 out of range. A track
 * in euclidean mode answers from its map (note trigs only). Before
 * conditions and probability, unlike ev_seq_step's trigs, and at the step's
 * own place: PLAYING PAGES can make the tick play another step's trig.
 * ev_seq_step's steps are one ahead of the sound, so at a pattern change a
 * step it reports may be in core_pattern_next(), not core_pattern_now():
 * pass the one the step belongs to. Any context. */
#define CORE_STEP_NOTE 1
#define CORE_STEP_LOCK 2
int core_step_trig(int pattern, int track, int step);

#endif
