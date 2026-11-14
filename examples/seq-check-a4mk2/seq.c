/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-check: core-a4mk2's ev_seq_step, ev_transport and core_transport
 * (analog-four-mk2/seq.h), recorded in seq_check (an exported symbol) for an
 * emulator's or a unit's memory read. 724 bytes, big-endian:
 *   magic        0x53514134 ("SQA4") once an event ran
 *   steps_calls  ev_seq_step's calls; first_calls those with CORE_SEQ_FIRST
 *   tr_n         ev_transport's calls
 *   n            ev_seq_step calls logged (the ring e holds the last 64, at n % 64)
 *   steps_n[t], trigs_n[t]  per track, steps and trigs reported
 *   dmin[t], dmax[t]        the sequencer clocks (24 a quarter note) between
 *                two steps of track t while playing, from its first step
 *                after PLAY (a resume restarts it; two calls in one clock, a
 *                resume's seed and its next step, are not timed); 0 / 0 for none
 *   last[t]      the clock of track t's last step
 *   tr[8]        the last 8 ev_transport calls, at tr_n % 8: {clock, state}
 *   e[64]        the last 64 ev_seq_step calls: {clock, steps, trigs,
 *                bit 0 CORE_SEQ_FIRST, core_transport()}
 * The clock is stock's sequencer time 0x4045c3f0 / 900000 (one a MIDI
 * clock), read in the event. */
#include <stdint.h>
#include "analog-four-mk2/seq.h"

#define CLOCK (*(volatile uint32_t *)0x4045c3f0 / 900000u)
#define TRACKS 6

struct ent { uint32_t clock; uint8_t steps, trigs, flags; int8_t state; };
struct trn { uint32_t clock; int32_t state; };

struct seq_check {
    uint32_t magic, steps_calls, first_calls, tr_n, n;
    uint32_t steps_n[TRACKS], trigs_n[TRACKS], dmin[TRACKS], dmax[TRACKS], last[TRACKS];
    struct trn tr[8];
    struct ent e[64];
    uint32_t spare[2];
} seq_check;

static uint32_t running;               /* bit t: track t's steps since its first are timed */

void sq_step(uint32_t steps, uint32_t trigs)
{
    struct ent *x = &seq_check.e[seq_check.n % 64];
    uint32_t c = CLOCK;
    int t;
    seq_check.magic = 0x53514134;
    seq_check.steps_calls++;
    if (steps & CORE_SEQ_FIRST) {
        seq_check.first_calls++;
        running = 0;
    }
    x->clock = c;
    x->steps = (uint8_t)steps;
    x->trigs = (uint8_t)trigs;
    x->flags = steps & CORE_SEQ_FIRST ? 1 : 0;
    x->state = (int8_t)core_transport();
    seq_check.n++;
    for (t = 0; t < TRACKS; t++) {
        if (!(steps & (1u << t)))
            continue;
        seq_check.steps_n[t]++;
        if (trigs & (1u << t))
            seq_check.trigs_n[t]++;
        if ((running & (1u << t)) && c != seq_check.last[t]) {
            uint32_t d = c - seq_check.last[t];
            if (!seq_check.dmax[t] || d < seq_check.dmin[t])
                seq_check.dmin[t] = d;
            if (d > seq_check.dmax[t])
                seq_check.dmax[t] = d;
        }
        running |= 1u << t;
        seq_check.last[t] = c;
    }
}

void sq_transport(int32_t state)
{
    struct trn *r = &seq_check.tr[seq_check.tr_n % 8];
    seq_check.magic = 0x53514134;
    r->clock = CLOCK;
    r->state = state;
    seq_check.tr_n++;
}
