/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.11: the sequencer's events and getters (README, "The
 * sequencer (0.11)"). Every address is OS 1.74's.
 *
 * The sequencer clock (0x4009b0b4) is an exception handler, one call a tick
 * (96 a quarter note, also while stopped). On a tick where a track reaches
 * its next step's boundary, its loop over the 13 tracks calls trig_fire
 * (0x4009aa28) for the track's next step: trig_fire fills the trig record
 * (0x42ac1704; its first word is 0 for no trig, a failed condition or
 * probability, a trigless step) and the clock copies it to the trig queue,
 * which the audio interrupt plays when it is due. Core's two sites (seq.s):
 *
 *   - 0x4009b68a, after the copy: the track stepped, and trigs if the
 *     record's first word isn't 0;
 *   - 0x4009c00a, the clock's exit: ev_transport when the play state
 *     changed, ev_seq_step(steps, trigs) when any track stepped this tick,
 *     then ev_seq_tick. All three run in the clock's interrupt.
 *
 * trig_fire's entry isn't hooked: the UI task calls it too (0x4009c02a).
 *
 * The first step after PLAY: its trig record was made before PLAY, so the
 * clock doesn't call trig_fire for it and core's trig-locks site
 * (0x4009a954) doesn't see it on the first loop. At the tick the play state
 * becomes 1, core seeds it: each track with a trig waiting in the trig
 * queue (stock made it, its condition evaluated) takes step 1's mod locks
 * in the playing pattern (core_lock_value), and ev_seq_step(0x1fff |
 * CORE_SEQ_FIRST, trigs) comes first, trigs being those tracks.
 */

#include <stdint.h>

#define PLAY_STATE (*(volatile int *)0x405c8c90)       /* 1 playing, 0 stopped */
#define PATTERN_NOW (*(volatile signed char *)0x405c8d04)
/* the queue of patterns to come (a chain's): entries of 20 bytes, each
 * with its pattern's number as a 16-bit word at +0; the entry playing and
 * the next one's index (-1: none queued) */
#define QUEUE_PATTERN(i) (*(volatile unsigned short *)(0x412f3e91u + 20u * (unsigned)(i)))
#define QUEUE_NEXT (*(volatile int *)0x412f5a03)
#define QUEUE_ENTRIES 64
#define TEMPO (*(volatile unsigned *)0x40287ffc)        /* BPM x 120 */
#define TRACK_STEP ((volatile signed char *)0x405c8cea)  /* + track: the step starting now */
#define TRACK_SPEED ((volatile signed char *)0x405c8d25) /* + track: 0..6, as the clock plays it */
#define TRIG_RECORD (*(volatile unsigned *)0x42ac1704)
#define PATTERNS 0x407dd8e3u                    /* live pattern p's object; the clock indexes them so */
#define PATTERN_SIZE 0x14f00u
#define LOCKS 0x2e91u                           /* the object's lock store, after its 13 track records */
#define TRACK_LOCKS 5674u
#define STEP_LOCKS 88u
#define QUEUED ((volatile unsigned char *)0x800067a4) /* + track: the trig queue's slots in use */
#define TRACKS 13
#define ALL ((1u << TRACKS) - 1)
#define SEQ_FIRST 0x80000000u                  /* in steps: the first step after PLAY */

void core_rows_trig(int track, unsigned step);

typedef void (*ev_seq_step_t)(uint32_t steps, uint32_t trigs);
typedef void (*ev_seq_tick_t)(void);
typedef void (*ev_transport_t)(int32_t state);
extern const ev_seq_step_t ev_seq_step[];
extern const ev_seq_tick_t ev_seq_tick[];
extern const ev_transport_t ev_transport[];

static uint32_t steps, trigs;                   /* this tick's, from the fired site */
static int state;                               /* the play state the last tick saw */
static int seeded;                              /* the first step after PLAY was seeded */
static uint32_t seed_trigs;

#ifdef CORE_SEQ_MEASURE
/* a measuring build only (-DCORE_SEQ_MEASURE): DMA timer 0 ticks core's
 * parts took on the ticks with a step, the subscribers included */
#define DTCN0 (*(volatile unsigned *)0xfc07000c)
struct { char magic[4]; uint32_t n, sum, max, fired, idle_n, idle_sum; } core_seq_cost = {{'S', 'Q', 'C', 'T'}};
#define T0() unsigned t0 = DTCN0
#define FIRED_DONE() (core_seq_cost.fired += DTCN0 - t0)
#else
#define T0() do {} while (0)
#define FIRED_DONE() do {} while (0)
#endif

/* the first step after PLAY: its mod locks and its trigs. The clock's
 * PLAY tick also makes step 2's trigs, and the trig-locks site sets their
 * locks at once, so the seed runs before the first of them: at the first
 * of the trig-locks site (rows.c), the fired site and the exit on that
 * tick, while the trig queue holds only step 1's trigs. */
void core_seq_edge(void)
{
    int p, t;
    unsigned obj;
    uint32_t m = 0;
    if (seeded || state == 1 || PLAY_STATE != 1)
        return;
    seeded = 1;
    p = PATTERN_NOW;
    if ((unsigned)p < 128) {
        obj = PATTERNS + PATTERN_SIZE * (unsigned)p;
        for (t = 0; t < TRACKS; t++)
            if (QUEUED[t]) {
                m |= 1u << t;
                core_rows_trig(t, obj + LOCKS + TRACK_LOCKS * t);
            }
    }
    seed_trigs = m;
}

/* 0x4009b68a (seq.s): the clock made track's next step's trig record */
void core_seq_fired(int track)
{
    T0();
    if ((unsigned)track >= TRACKS)
        return;
    core_seq_edge();
    steps |= 1u << track;
    if (TRIG_RECORD)
        trigs |= 1u << track;
    FIRED_DONE();
}

/* 0x4009c00a (seq.s): the clock's exit */
void core_seq_exit(void)
{
    const ev_seq_step_t *s;
    const ev_seq_tick_t *k;
    const ev_transport_t *x;
    int now, first;
    uint32_t st, tr;
    T0();
    core_seq_edge();
    now = PLAY_STATE;
    st = steps;
    tr = trigs;
    steps = trigs = 0;
    first = seeded && state != 1;
    if (now != 1)
        seeded = 0;
    if (now != state) {
        state = now;
        for (x = ev_transport; *x; x++)
            (*x)(now);
    }
    if (first)
        for (s = ev_seq_step; *s; s++)
            (*s)(ALL | SEQ_FIRST, seed_trigs);
    if (st)
        for (s = ev_seq_step; *s; s++)
            (*s)(st, tr);
    for (k = ev_seq_tick; *k; k++)
        (*k)();
#ifdef CORE_SEQ_MEASURE
    {
        unsigned d = DTCN0 - t0;
        if (st) {
            d += core_seq_cost.fired;
            core_seq_cost.n++;
            core_seq_cost.sum += d;
            if (d > core_seq_cost.max)
                core_seq_cost.max = d;
        } else {
            core_seq_cost.idle_n++;
            core_seq_cost.idle_sum += d;
        }
        core_seq_cost.fired = 0;
    }
#endif
}

/* ---- getters, from any task or interrupt ---- */

int core_transport(void)
{
    return PLAY_STATE;
}

int core_pattern_now(void)
{
    int p = PATTERN_NOW;
    return (unsigned)p < 128 ? p : -1;
}

int core_pattern_next(void)
{
    int i = QUEUE_NEXT, p;
    if (i < 0)
        return core_pattern_now();
    if (i >= QUEUE_ENTRIES)
        return -1;
    p = QUEUE_PATTERN(i);
    return (unsigned)p < 128 ? p : -1;
}

unsigned core_tempo(void)
{
    return TEMPO;
}

int core_track_speed(int track)
{
    return (unsigned)track < TRACKS ? TRACK_SPEED[track] : -1;
}

int core_track_step(int track)
{
    return (unsigned)track < TRACKS ? TRACK_STEP[track] : -1;
}
