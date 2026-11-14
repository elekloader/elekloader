/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: the sequencer's events and getters (mods/core-a4mk2/README.md,
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

#endif
