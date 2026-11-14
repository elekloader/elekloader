/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: the sequencer's events and play state (README, "The
 * sequencer"; analog-four-mk2/seq.h), core-ar2 0.11's at the Analog
 * Four MKII's addresses. Every address is OS 1.56's.
 *
 * The tick (0x4007a9b0, vector 121) runs 96 times a quarter note, also while
 * stopped; the sequencer works on every 4th (24 a quarter note). On such a
 * tick, for each of the 6 tracks at a step boundary, the tick's loop calls
 * trig_fire (0x4007a1d2) for the track's next step: trig_fire fills the trig
 * record (0x42872474; its first word 0 for no trig, a failed condition, a
 * trigless step) and the tick copies it to the track's trig queue. Core's
 * sites:
 *
 *   - 0x4007b0e8 (seq.s), after the copy: the track stepped, and trigs if
 *     the record's first word isn't 0;
 *   - 0x4007badc, the tick's exit (stdmidi.s): core_std_seq_tick calls
 *     core_seq_exit, then ev_seq_tick. ev_transport when the play state
 *     changed, ev_seq_step(steps, trigs) when any track stepped this tick.
 *
 * PLAY from stop: the same tick sets every track's first-step flag
 * (0x80006b1a) and fires every track's first step, so the tick's own call
 * carries CORE_SEQ_FIRST. A resume (PLAY after a pause, a MIDI Continue,
 * 0x400797da) plays the step it stopped on from records stock made at the
 * pause (0x4007bafc, 0x428722bc + track x 0x44) and fires only the step
 * after it: core seeds that step as its own call first,
 * ev_seq_step(0x3f | CORE_SEQ_FIRST, trigs), trigs the tracks whose record
 * holds a trig. A pause sets the play state to 2.
 */

#include <stdint.h>

#define PLAY_STATE (*(volatile int *)0x80006a88)        /* 0 stopped, 1 playing, 2 paused */
#define FIRST_FLAGS (*(volatile unsigned short *)0x80006b1a) /* bit t: track t's first step after PLAY */
#define TRIG_RECORD (*(volatile unsigned *)0x42872474)
#define RESUME_RECORD(t) (*(volatile unsigned *)(0x428722bcu + 0x44u * (unsigned)(t)))
#define TRACKS 6
#define ALL ((1u << TRACKS) - 1)
#define SEQ_FIRST 0x80000000u                           /* in steps: the first step after PLAY */

typedef void (*ev_seq_step_t)(uint32_t steps, uint32_t trigs);
typedef void (*ev_transport_t)(int32_t state);
extern const ev_seq_step_t ev_seq_step[];
extern const ev_transport_t ev_transport[];

static uint32_t steps, trigs;                   /* this tick's, from the fired site */
static int state;                               /* the play state the last tick saw */
static int edged;                               /* this play's first tick was seen */
static uint32_t first_flag;                     /* SEQ_FIRST for the edge tick's own call */
static uint32_t seed_steps, seed_trigs;         /* a resume's seeded step */

/* the first tick after the play state became 1: a start fires every track's
 * first step itself (its first-step flag is still set at the fired site); a
 * resume (the edge seen at the exit, or at a fired site without the flag)
 * plays stock's records */
static void edge(int track)
{
    int t;
    uint32_t m = 0;
    if (edged || state == 1 || PLAY_STATE != 1)
        return;
    edged = 1;
    if (track >= 0 && (FIRST_FLAGS & (1u << track))) {
        first_flag = SEQ_FIRST;
        seed_steps = seed_trigs = 0;
        return;
    }
    first_flag = 0;
    for (t = 0; t < TRACKS; t++)
        if (RESUME_RECORD(t))
            m |= 1u << t;
    seed_steps = ALL | SEQ_FIRST;
    seed_trigs = m;
}

/* 0x4007b0e8 (seq.s): the tick made track's next step's trig record */
void core_seq_fired(int track)
{
    if ((unsigned)track >= TRACKS)
        return;
    edge(track);
    steps |= 1u << track;
    if (TRIG_RECORD)
        trigs |= 1u << track;
}

/* 0x4007badc, from core_std_seq_tick (stdmidi.c): the tick's exit, before
 * ev_seq_tick */
void core_midi_irq_drain(void);

void core_seq_exit(void)
{
    const ev_seq_step_t *s;
    const ev_transport_t *x;
    int now, first;
    uint32_t st, tr;
    edge(-1);
    now = PLAY_STATE;
    st = steps;
    tr = trigs;
    steps = trigs = 0;
    first = edged && state != 1;
    if (now != 1)
        edged = 0;
    if (now != state) {
        state = now;
        for (x = ev_transport; *x; x++)
            (*x)(now);
    }
    if (first && seed_steps)
        for (s = ev_seq_step; *s; s++)
            (*s)(seed_steps, seed_trigs);
    if (st) {
        if (first)
            st |= first_flag;
        for (s = ev_seq_step; *s; s++)
            (*s)(st, tr);
    }
    core_midi_irq_drain();              /* what waited in core_midi_try_send's ring (send.c) */
}

/* the play state, from any task or interrupt: 0 stopped, 1 playing, 2 paused */
int core_transport(void)
{
    return PLAY_STATE;
}
