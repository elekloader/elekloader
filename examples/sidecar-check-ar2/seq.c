/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sidecar-check (Analog Rytm MKII), part 4: the sequencer (core 0.11),
 * checked in RAM (README). Every ev_seq_step and ev_transport is logged with
 * the getters' values at that moment; ev_seq_tick counts the ticks and keeps
 * the getters' last values. Three ev_seq_step subscribers, of which CMD4_SUBS
 * sets how many log (1 by default; the others return at once), for the cost
 * with 1 and 3 subscribers. */
#include <stdint.h>
#include "analog-rytm-mk2/seq.h"

#define RING 64

enum {
    CMD4_NONE,
    CMD4_SUBS,          /* arg (1..3) ev_seq_step subscribers do the work */
    CMD4_CLEAR,         /* the counters and logs back to 0 */
    CMD4_QUIET,         /* arg 1: ev_seq_tick only counts (no getters), for the cost */
};

struct sc4_step {
    uint32_t tick;
    uint16_t steps, trigs;
    int8_t step0, step8, state, pattern;
};

struct {
    char magic[4];                              /* SCK4 */
    volatile int32_t cmd, arg;
    int32_t done;
    int32_t subs;
    uint32_t ticks, step_events, seeds;
    uint32_t steps_n[13], trigs_n[13];          /* per track, from ev_seq_step (seeds apart) */
    uint32_t seed_trigs;                        /* the last seed's trigs */
    /* the getters at the last tick */
    int32_t state, now, next, tempo;
    int8_t speed[13], step[13];
    uint16_t pad;
    /* ev_transport */
    uint32_t tr_n;
    struct { uint32_t tick; int32_t state; } tr[8];
    uint32_t n;
    struct sc4_step e[RING];
    uint32_t work;                              /* the extra subscribers' calls */
    int32_t quiet;
} sc4 = {{'S', 'C', 'K', '4'}, 0, 0, 0, 1};
const unsigned sc4_size = sizeof sc4;          /* remote.c */

void sc_seq_step(uint32_t steps, uint32_t trigs)
{
    struct sc4_step *e = &sc4.e[sc4.n++ % RING];
    int t;
    e->tick = sc4.ticks;
    e->steps = (uint16_t)(steps | (steps >> 16));    /* CORE_SEQ_FIRST shows as 0x8000 */
    e->trigs = (uint16_t)trigs;
    e->step0 = (int8_t)core_track_step(0);
    e->step8 = (int8_t)core_track_step(8);
    e->state = (int8_t)core_transport();
    e->pattern = (int8_t)core_pattern_now();
    if (steps & CORE_SEQ_FIRST) {               /* the PLAY seed */
        sc4.seeds++;
        sc4.seed_trigs = trigs;
        return;
    }
    sc4.step_events++;
    for (t = 0; t < 13; t++) {
        if (steps >> t & 1)
            sc4.steps_n[t]++;
        if (trigs >> t & 1)
            sc4.trigs_n[t]++;
    }
}

static void extra(uint32_t steps, uint32_t trigs, int k)
{
    volatile uint32_t s = 0;
    int t;
    if (sc4.subs < k)
        return;
    for (t = 0; t < 13; t++)                    /* as much work as the first one's counting */
        s += (steps >> t & 1) + (trigs >> t & 1);
    sc4.work += s ? 1 : 0;
}

void sc_seq_step2(uint32_t steps, uint32_t trigs)
{
    extra(steps, trigs, 2);
}

void sc_seq_step3(uint32_t steps, uint32_t trigs)
{
    extra(steps, trigs, 3);
}

void sc_transport(int32_t state)
{
    sc4.tr[sc4.tr_n++ % 8].tick = sc4.ticks;
    sc4.tr[(sc4.tr_n - 1) % 8].state = state;
}

void sc_seq_tick(void)
{
    int t;
    sc4.ticks++;
    if (sc4.quiet)
        return;
    sc4.state = core_transport();
    sc4.now = core_pattern_now();
    sc4.next = core_pattern_next();
    sc4.tempo = (int32_t)core_tempo();
    for (t = 0; t < 13; t++) {
        sc4.speed[t] = (int8_t)core_track_speed(t);
        sc4.step[t] = (int8_t)core_track_step(t);
    }
}

/* ev_tick: the commands */
void sc_seq_cmd(void *ctrl)
{
    int i;
    (void)ctrl;
    switch (sc4.cmd) {
    case CMD4_SUBS:
        sc4.subs = sc4.arg;
        break;
    case CMD4_QUIET:
        sc4.quiet = sc4.arg;
        break;
    case CMD4_CLEAR:
        sc4.step_events = sc4.seeds = sc4.n = sc4.tr_n = sc4.work = 0;
        for (i = 0; i < 13; i++)
            sc4.steps_n[i] = sc4.trigs_n[i] = 0;
        break;
    default:
        return;
    }
    sc4.done = sc4.cmd;
    sc4.cmd = CMD4_NONE;
}
